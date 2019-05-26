/*
 *  Pushbullet Push Notification 1.1 PL (esp8266)
 *
 *  Processes the security system status and demonstrates how to send a push notification when the status has changed.
 *  This example sends notifications via Pushbullet: https://www.pushbullet.com
 *
 *  Release notes:
 *  1.1 - New: Set authentication method for BearSSL in esp8266 Arduino Core 2.5.0+
 *        New: Added notifications - Keybus connected, armed status, zone alarm status
 *  1.0 - Initial release
 *
 *  Wiring:
 *      DSC Aux(+) ---+--- esp8266 NodeMCU Vin pin
 *                    |
 *                    +--- 5v voltage regulator --- esp8266 Wemos D1 Mini 5v pin
 *
 *      DSC Aux(-) --- esp8266 Ground
 *
 *                                         +--- dscClockPin (esp8266: D1, D2, D8)
 *      DSC Yellow --- 15k ohm resistor ---|
 *                                         +--- 10k ohm resistor --- Ground
 *
 *                                         +--- dscReadPin (esp8266: D1, D2, D8)
 *      DSC Green ---- 15k ohm resistor ---|
 *                                         +--- 10k ohm resistor --- Ground
 *
 *  Virtual keypad (optional):
 *      DSC Green ---- NPN collector --\
 *                                      |-- NPN base --- 1k ohm resistor --- dscWritePin (esp8266: D1, D2, D8)
 *            Ground --- NPN emitter --/
 *
 *  Virtual keypad uses an NPN transistor to pull the data line low - most small signal NPN transistors should
 *  be suitable, for example:
 *   -- 2N3904
 *   -- BC547, BC548, BC549
 *
 *  Issues and (especially) pull requests are welcome:
 *  https://github.com/taligentx/dscKeybusInterface
 *
 *  This example code is in the public domain.
 */

#include <ESP8266WiFi.h>
#include <dscKeybusInterface.h>

// WiFi settings
const char* wifiSSID = "";
const char* wifiPassword = "";

// Pushbullet settings
const char* pushToken = "";  // Set the access token generated in the Pushbullet account settings

WiFiClientSecure pushClient;

// Configures the Keybus interface with the specified pins.
#define dscClockPin D1  // esp8266: D1, D2, D8 (GPIO 5, 4, 15)
#define dscReadPin D2   // esp8266: D1, D2, D8 (GPIO 5, 4, 15)
dscKeybusInterface dsc(dscClockPin, dscReadPin);


void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println();

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID, wifiPassword);
  while (WiFi.status() != WL_CONNECTED) delay(100);
  Serial.print(F("WiFi connected: "));
  Serial.println(WiFi.localIP());

  // Sends a push notification on startup to verify connectivity
  #ifdef AXTLS_DEPRECATED
    pushClient.setInsecure();  // Sets authentication method for BearSSL in esp8266 Arduino Core 2.5.0+
  #endif
  if (sendPush("System DSC uruchamianie")) Serial.println(F("Initialization push notification sent successfully."));
  else Serial.println(F("Initialization push notification failed to send."));

  // Starts the Keybus interface
  dsc.begin();

  Serial.println(F(" System DSC magistrali Keybus jest podłączony."));
}


void loop() {
  if (dsc.handlePanel() && dsc.statusChanged) {  // Processes data only when a valid Keybus command has been read
    dsc.statusChanged = false;                   // Resets the status flag

    // If the Keybus data buffer is exceeded, the sketch is too busy to process all Keybus commands.  Call
    // handlePanel() more often, or increase dscBufferSize in the library: src/dscKeybusInterface.h
    if (dsc.bufferOverflow) Serial.println(F("Przepełnienie bufora magistrali KEYBUS"));
    dsc.bufferOverflow = false;

    // Checks if the interface is connected to the Keybus
    if (dsc.keybusChanged) {
      dsc.keybusChanged = false;  // Resets the Keybus data status flag
      if (dsc.keybusConnected) sendPush("System DSC podłączony");
      else sendPush("System DSC odłączony");
    }

    // Checks status per partition
    for (byte partition = 0; partition < dscPartitions; partition++) {

      // Checks armed status
      if (dsc.armedChanged[partition]) {
        dsc.armedChanged[partition] = false;  // Resets the partition armed status flag
        if (dsc.armed[partition]) {

          char pushMessage[40] = "System DSC ";
          if (dsc.armedAway[partition]) {
            char armedState[31] = "Włączenie DOMOWE: podsystem ";
            strcat(pushMessage, armedState);
          }
          else if (dsc.armedStay[partition]) {
            char armedState[33] = "Włączenie NORMALNE: podsystem ";
            strcat(pushMessage, armedState);
          }
          char partitionNumber[2];
          itoa(partition + 1, partitionNumber, 10);
          strcat(pushMessage, partitionNumber);
          sendPush(pushMessage);

        }
        else {
          char pushMessage[34] = "System DSC rozbrojony: podsystem ";
          char partitionNumber[2];
          itoa(partition + 1, partitionNumber, 10);
          strcat(pushMessage, partitionNumber);
          sendPush(pushMessage);
        }
      }

      // Checks alarm triggered status
      if (dsc.alarmChanged[partition]) {
        dsc.alarmChanged[partition] = false;  // Resets the partition alarm status flag

        char pushMessage[34] = "System DSC w alarmie: podsystem ";
        char partitionNumber[2];
        itoa(partition + 1, partitionNumber, 10);
        strcat(pushMessage, partitionNumber);

        if (dsc.alarm[partition]) sendPush(pushMessage);
        else sendPush("System DSC rozbrojony po alarmie");
      }

      // Checks fire alarm status
      if (dsc.fireChanged[partition]) {
        dsc.fireChanged[partition] = false;  // Resets the fire status flag

        char pushMessage[42] = "System DSC alarm POŻAROWY: podsystem ";
        char partitionNumber[2];
        itoa(partition + 1, partitionNumber, 10);
        strcat(pushMessage, partitionNumber);

        if (dsc.fire[partition]) sendPush(pushMessage);
        else sendPush("System DSC koniec alarmu POŻAROWEGO");
      }
    }

    // Checks for zones in alarm
    // Zone alarm status is stored in the alarmZones[] and alarmZonesChanged[] arrays using 1 bit per zone, up to 64 zones
    //   alarmZones[0] and alarmZonesChanged[0]: Bit 0 = Zone 1 ... Bit 7 = Zone 8
    //   alarmZones[1] and alarmZonesChanged[1]: Bit 0 = Zone 9 ... Bit 7 = Zone 16
    //   ...
    //   alarmZones[7] and alarmZonesChanged[7]: Bit 0 = Zone 57 ... Bit 7 = Zone 64
    if (dsc.alarmZonesStatusChanged) {
      dsc.alarmZonesStatusChanged = false;                           // Resets the alarm zones status flag
      for (byte zoneGroup = 0; zoneGroup < dscZones; zoneGroup++) {
        for (byte zoneBit = 0; zoneBit < 8; zoneBit++) {
          if (bitRead(dsc.alarmZonesChanged[zoneGroup], zoneBit)) {  // Checks an individual alarm zone status flag
            bitWrite(dsc.alarmZonesChanged[zoneGroup], zoneBit, 0);  // Resets the individual alarm zone status flag
            if (bitRead(dsc.alarmZones[zoneGroup], zoneBit)) {       // Zone alarm
              char pushMessage[24] = "Alarm linia: ";
              char zoneNumber[3];
              itoa((zoneBit + 1 + (zoneGroup * 8)), zoneNumber, 10); // Determines the zone number
              strcat(pushMessage, zoneNumber);
              sendPush(pushMessage);
            }
            else {
              char pushMessage[33] = "Powrót po alarmie linii: ";
              char zoneNumber[3];
              itoa((zoneBit + 1 + (zoneGroup * 8)), zoneNumber, 10); // Determines the zone number
              strcat(pushMessage, zoneNumber);
              sendPush(pushMessage);
            }
          }
        }
      }
    }

    // Checks for AC power status
    if (dsc.powerChanged) {
      dsc.powerChanged = false;  // Resets the battery trouble status flag
      if (dsc.powerTrouble) sendPush("Brak zasilania z sieci AC");
      else sendPush("Zasilanie z sieci AC wróciło");
    }

    // Checks for keypad fire alarm status
    if (dsc.keypadFireAlarm) {
      dsc.keypadFireAlarm = false;  // Resets the keypad fire alarm status flag
      sendPush("DSC Klawiatura - wciśnięto POŻAR");
    }

    // Checks for keypad aux auxiliary alarm status
    if (dsc.keypadAuxAlarm) {
      dsc.keypadAuxAlarm = false;  // Resets the keypad auxiliary alarm status flag
      sendPush("DSC Klawiatura - wciśnięto POMOC");
    }

    // Checks for keypad panic alarm status
    if (dsc.keypadPanicAlarm) {
      dsc.keypadPanicAlarm = false;  // Resets the keypad panic alarm status flag
      sendPush("DSC Klawiatura - wciśnięto PANIKĘ");
    }
  }
}


bool sendPush(const char* pushMessage) {

  // Connects and sends the message as JSON
  if (!pushClient.connect("api.pushbullet.com", 443)) return false;
  pushClient.println(F("POST /v2/pushes HTTP/1.1"));
  pushClient.println(F("Host: api.pushbullet.com"));
  pushClient.println(F("User-Agent: ESP8266"));
  pushClient.println(F("Accept: */*"));
  pushClient.println(F("Content-Type: application/json"));
  pushClient.print(F("Content-Length: "));
  pushClient.println(strlen(pushMessage) + 25);  // Length including JSON data
  pushClient.print(F("Access-Token: "));
  pushClient.println(pushToken);
  pushClient.println();
  pushClient.print(F("{\"body\":\""));
  pushClient.print(pushMessage);
  pushClient.print(F("\",\"type\":\"note\"}"));

  // Waits for a response
  unsigned long previousMillis = millis();
  while (!pushClient.available()) {
    dsc.handlePanel();
    if (millis() - previousMillis > 3000) {
      Serial.println(F("Connection timed out waiting for a response."));
      pushClient.stop();
      return false;
    }
    yield();
  }

  // Reads the response until the first space - the next characters will be the HTTP status code
  while (pushClient.available()) {
    if (pushClient.read() == ' ') break;
  }

  // Checks the first character of the HTTP status code - the message was sent successfully if the status code
  // begins with "2"
  char statusCode = pushClient.read();

  // Successful, reads the remaining response to clear the client buffer
  if (statusCode == '2') {
    while (pushClient.available()) pushClient.read();
    pushClient.stop();
    return true;
  }

  // Unsuccessful, prints the response to serial to help debug
  else {
    Serial.println(F("Push notification error, response:"));
    Serial.print(statusCode);
    while (pushClient.available()) Serial.print((char)pushClient.read());
    Serial.println();
    pushClient.stop();
    return false;
  }
}

#include <WiFi.h>
#include <WebSocketsClient.h>
#include <MFRC522.h>
#include <SPI.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <LinkedList.h>

// WiFi Credentials
const char* ssid = "MinhDuc";
const char* password = "tett2020";

// WebSocket Server Details
const char* ws_server = "192.168.2.74";
const uint16_t ws_port = 8080;

// RFID Pins
#define SS_PIN 21
#define RST_PIN 22

// LED Pins
const int redPin = 25;
const int greenPin = 26;

// NTP Client Setup
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7*3600, 60000);

// RFID and WebSocket Objects
MFRC522 mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;
WebSocketsClient webSocket;

struct CardInfo {
  byte uid[4];
  unsigned long lastScanTime;
  bool isTimeIn;
};

LinkedList<CardInfo*> cards;
const unsigned long SCAN_INTERVAL = 12 * 60 * 60; // 12 hours

bool isWritingData = false; // Variable to check if data is being written to the card
String dataToWrite = ""; // Data to be written to the card

void connectWebSocket() {
  webSocket.begin(ws_server, ws_port, "/");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000); // Reconnect every 5 seconds
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.println("WebSocket disconnected, trying to reconnect...");
      webSocket.begin(ws_server, ws_port, "/");
      break;
    case WStype_CONNECTED:
      Serial.println("WebSocket connected");
      break;
    case WStype_TEXT:
      Serial.printf("Received text: %s\n", payload);
      dataToWrite = String((char*)payload); // Store received data
      isWritingData = true; // Start the process to write data to the card
      break;
    case WStype_BIN:
      Serial.printf("Received binary data\n");
      break;
  }
}

void setup() {
  Serial.begin(115200);
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.print("WiFi connected. ESP32 IP address: ");
  Serial.println(WiFi.localIP());

  SPI.begin();
  mfrc522.PCD_Init();

  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);

  connectWebSocket();
  Serial.println("WebSocket server started");

  timeClient.begin();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection lost. Reconnecting...");
    WiFi.reconnect();
    delay(1000);
    return;
  }

  webSocket.loop();
  timeClient.update();

  if (isWritingData) {
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
      writeDataToCard();
      isWritingData = false; // Finish writing data to the card
      mfrc522.PICC_HaltA();
      delay(1000);
      mfrc522.PCD_StopCrypto1();
    }
    return;
  }

  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    unsigned long currentTime = timeClient.getEpochTime();
    byte* uid = mfrc522.uid.uidByte;

    if (isCardAllowedToScan(uid, currentTime)) {
      readingData();
      updateCardLastScanTime(uid, currentTime);
    } else {
      Serial.println("Card scan interval limit. Try again later.");
      digitalWrite(redPin, HIGH);
      delay(1000);
      digitalWrite(redPin, LOW);
    }

    mfrc522.PICC_HaltA();
    delay(1000);
    mfrc522.PCD_StopCrypto1();
  }
}

bool isCardAllowedToScan(byte *uid, unsigned long currentTime) {
  for (int i = 0; i < cards.size(); i++) {
    if (memcmp(cards.get(i)->uid, uid, 4) == 0) {
      if (currentTime - cards.get(i)->lastScanTime < SCAN_INTERVAL && cards.get(i)->isTimeIn) {
        return true;
      }
      if (currentTime - cards.get(i)->lastScanTime < SCAN_INTERVAL && !cards.get(i)->isTimeIn) {
        return false;
      }
    }
  }
  return true;
}

void updateCardLastScanTime(byte *uid, unsigned long currentTime) {
  for (int i = 0; i < cards.size(); i++) {
    if (memcmp(cards.get(i)->uid, uid, 4) == 0) {
      cards.get(i)->lastScanTime = currentTime;
      cards.get(i)->isTimeIn = !cards.get(i)->isTimeIn;
      sendDataToWebSocket(uid, cards.get(i)->isTimeIn ? "time_in" : "time_out");
      return;
    }
  }

  CardInfo* newCard = new CardInfo;
  memcpy(newCard->uid, uid, 4);
  newCard->lastScanTime = currentTime;
  newCard->isTimeIn = true;
  cards.add(newCard);
  sendDataToWebSocket(uid, "time_in");
}

void readingData() {
  mfrc522.PICC_DumpDetailsToSerial(&(mfrc522.uid));

  byte buffer[18] = {0};
  byte block = 1;
  byte size = 18;

  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }

  MFRC522::StatusCode status;
  for (int i = 0; i < 5; i++) { // Try 5 times
    status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(mfrc522.uid));
    if (status == MFRC522::STATUS_OK) {
        break;
    }
    delay(500); // Increase delay between attempts
  }
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("Authentication failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    digitalWrite(redPin, HIGH);
    delay(1000);
    digitalWrite(redPin, LOW);
    return;
  }

  status = mfrc522.MIFARE_Read(block, buffer, &size);
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("Reading failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    digitalWrite(redPin, HIGH);
    delay(1000);
    digitalWrite(redPin, LOW);
    return;
  } else {
    digitalWrite(greenPin, HIGH);
    delay(1000);
    digitalWrite(greenPin, LOW);
  }

  Serial.print(F("\nData from block ["));
  Serial.print(block);
  Serial.print(F("]: "));

  String dataStr = "";
  for (uint8_t i = 0; i < 16; i++) {
    Serial.write(buffer[i]);
    dataStr += (char)buffer[i];
  }
  Serial.println(" ");
  Serial.print("Sending data to WebSocket: ");
  Serial.println(dataStr);

  webSocket.sendTXT(dataStr);
}

String getFormattedTime(unsigned long epochTime) {
  setTime(epochTime);
  char buffer[20];
  sprintf(buffer, "%02d-%02d-%04d %02d:%02d:%02d", day(), month(), year(), hour(), minute(), second());
  return String(buffer);
}

void sendDataToWebSocket(byte *uid, String type) {
  String uidStr = "";
  for (int i = 0; i < 4; i++) {
    uidStr += String(uid[i], HEX);
  }
  String currentTimeStr = getFormattedTime(timeClient.getEpochTime());
  String message = uidStr + ";" + type + ";" + currentTimeStr;
  Serial.print("Sending data to WebSocket: ");
  Serial.println(message);
  webSocket.sendTXT(message);
}

void writeDataToCard() {
  byte block = 1;
  byte buffer[18];
  dataToWrite.getBytes(buffer, sizeof(buffer));

  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }

  MFRC522::StatusCode status;
  for (int i = 0; i < 5; i++) { // Try 5 times
    status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(mfrc522.uid));
    if (status == MFRC522::STATUS_OK) {
        break;
    }
    delay(500); // Increase delay between attempts
  }
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("Authentication failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    digitalWrite(redPin, HIGH);
    delay(1000);
    digitalWrite(redPin, LOW);
    return;
  }

  status = mfrc522.MIFARE_Write(block, buffer, 16);
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("Writing failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    digitalWrite(redPin, HIGH);
    delay(1000);
    digitalWrite(redPin, LOW);
    return;
  } else {
    Serial.println(F("Writing data successful"));
    digitalWrite(greenPin, HIGH);
    delay(1000);
    digitalWrite(greenPin, LOW);
  }
}

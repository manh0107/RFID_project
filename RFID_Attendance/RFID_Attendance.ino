#include <WiFi.h>
#include <WebSocketsClient.h>
#include <MFRC522.h>
#include <SPI.h>

// Thông tin mạng WiFi
const char* ssid = "MinhDuc";
const char* password = "tett2020";

// Địa chỉ IP và cổng của WebSocket server
const char* ws_server = "192.168.2.74"; // Thay thế bằng địa chỉ IP đúng của server WebSocket
const uint16_t ws_port = 8080;          // Cổng của server WebSocket

// Các chân kết nối RFID
#define SS_PIN 21
#define RST_PIN 22

// Chân LED
const int redPin = 25;
const int greenPin = 26;

// Khai báo biến và đối tượng
MFRC522 mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;
WebSocketsClient webSocket;

// Hàm kết nối tới WebSocket server
void connectWebSocket() {
  webSocket.begin(ws_server, ws_port, "/");

  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000); // Thời gian thử lại kết nối sau 5 giây
}

// Hàm xử lý sự kiện WebSocket
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.println("WebSocket disconnected");
      break;
    case WStype_CONNECTED:
      Serial.println("WebSocket connected");
      break;
    case WStype_TEXT:
      Serial.printf("Received text: %s\n", payload);
      break;
    case WStype_BIN:
      Serial.printf("Received binary data\n");
      break;
  }
}

void setup() {
  // Khởi động Serial
  Serial.begin(115200);

  // Khởi động kết nối WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Đang kết nối với WiFi...");
  }
  Serial.print("Đã kết nối với WiFi. Địa chỉ IP của ESP32: ");
  Serial.println(WiFi.localIP());

  // Khởi động SPI và RFID
  SPI.begin();
  mfrc522.PCD_Init();

  // Thiết lập các chân LED
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);

  // Kết nối tới WebSocket server
  connectWebSocket();
  Serial.println("Máy chủ WebSocket đã khởi động");
}

void loop() {
  webSocket.loop();

  // Đọc thẻ RFID
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    readingData();
    mfrc522.PICC_HaltA();
    delay(1000); // Thêm một khoảng delay để ổn định quá trình đọc
    mfrc522.PCD_StopCrypto1(); // Dừng quá trình mã hóa
  }

  // Kiểm tra và thực hiện ghi dữ liệu nếu có yêu cầu từ Serial
  if (Serial.available()) {
    int op = Serial.parseInt();
    if (op == 1) {
      writingData();
    }
    // Clear any remaining input
    while (Serial.available()) {
      if (Serial.read() == '\n') break;
    }
  }
}

void readingData() {
  mfrc522.PICC_DumpDetailsToSerial(&(mfrc522.uid));

  byte buffer[18] = {0};
  byte block = 1;
  byte size = 18;

  // Use the default key (usually 0xFFFFFFFFFFFF)
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }

  MFRC522::StatusCode status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(mfrc522.uid));
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("Xác thực thất bại: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    digitalWrite(redPin, HIGH);
    delay(1000);
    digitalWrite(redPin, LOW);
    return;
  }

  status = mfrc522.MIFARE_Read(block, buffer, &size);
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("Đọc thất bại: "));
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

  Serial.print(F("\nDữ liệu từ khối ["));
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

void writingData() {
  mfrc522.PICC_DumpDetailsToSerial(&(mfrc522.uid));

  Serial.setTimeout(30000L);
  Serial.println(F("Nhập dữ liệu cần ghi với ký tự '#' ở cuối \n[tối đa 16 ký tự]:"));

  byte buffer[16] = "";
  byte block = 1;
  byte dataSize;

  dataSize = Serial.readBytesUntil('#', (char*)buffer, 16);
  for (byte i = dataSize; i < 16; i++) {
    buffer[i] = ' ';
  }

  String str = (char*)buffer;
  Serial.println(str);

  webSocket.sendTXT(str);

  MFRC522::StatusCode status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(mfrc522.uid));
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("PCD_Authenticate() thất bại: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    return;
  }

  status = mfrc522.MIFARE_Write(block, buffer, 16);
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("MIFARE_Write() thất bại: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    return;
  }

  Serial.println(F("Ghi dữ liệu thành công"));
}

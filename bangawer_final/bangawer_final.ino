/////////////////////////////////////////////////////////////////////////
//////////////////////////////////Field(OLD)
/////////////////////////////////////////////////////////////////////////
#include <SPI.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <Arduino.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "u8g2_korea_kang4.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

U8G2_SH1106_128X64_NONAME_F_4W_HW_SPI u8g2(U8G2_R0, /* cs=*/ 5, /* dc=*/ 14, /* reset=*/ 15); //scl=18, sda=23  SPI로 변경

#define SERVICE_UUID           "4fafc201-1fb5-459e-8fcc-c5c9c331914b" // UART service UUID
#define CHARACTERISTIC_UUID_RX "beb5483e-36e1-4688-b7f5-ea07361b26a8" // 
#define CHARACTERISTIC_UUID_TX "beb5483e-36e1-4688-b7f5-ea07361b26a9" // 

BLEServer *pServer = NULL;
BLECharacteristic * pTxCharacteristic;
BLECharacteristic * pRxCharacteristic;
bool deviceConnected = false;
bool oldDeviceConnected = false;
uint8_t txValue = 0;
String recentMessage = "";

int nowCallback = 0;
int previousCallback = 0;
int deviceWidth = 128;
//긴 텍스트를 위한 스크롤 기능
int maxCursorY = 0;
int currentCursorY = 0;
int gapWithTextLines = 24;
unsigned long scrollStartTime = 0;
unsigned long accumTimeForScroll = 0;
long previousMillis = 0; 
//아래의 두개 수정가능
int scrollStartDelayTime = 3000; // (3000이면 3초있다가 스크롤 시작)
int scrollDelay = 200;// (이값이 클수록 스크롤 속도가 느려짐)




/////////////////////////////////////////////////////////////////////////
//////////////////////////////////Field(New)
/////////////////////////////////////////////////////////////////////////
#include <SPIFFS.h>

#define LED_PIN 23  

#define RECORDING_TIME 6
#define SAMPLE_RATE 4000

// Calculate the recording data size based on the recording time and sample rate
#define RECORDING_DATA_SIZE RECORDING_TIME * SAMPLE_RATE


const int headerSize = 44;
char filename[20] = "/sound1.wav";
byte header[headerSize];
unsigned long write_data_count = 0;
File file;
unsigned long start_millis;
uint8_t *buffer;
int recordMode = 0;



/////////////////////////////////////////////////////////////////////////
//////////////////////////////////서버테스트(New)
/////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////
//////////////////////////////////BLUETOOTH
/////////////////////////////////////////////////////////////////////////
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
  };

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
  }
};
class MyRXCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string rxValue = pCharacteristic->getValue();
    if (rxValue.length() > 0) {
      Serial.println("********");
      Serial.print("Received Value: ");
      for (int i = 0; i < rxValue.length(); i++)
        Serial.print(rxValue[i]);

      Serial.println();

      recentMessage = rxValue.c_str(); 

      if(recentMessage == "rStart"){  
        Serial.println("record message 도착");
        recordMode = 1;
      }
      else if(recentMessage == "rStop"){  
        Serial.println("record message 도착");
        recordMode = 0;
      }
      else{
        recordMode = 0;
      }

      nowCallback++;
    }
  }

};
void initBLEDevice()
{
  // Create the BLE Device
  BLEDevice::init("banGawer");
  // Create the BLE Server
  pServer = BLEDevice::createServer();
  // pServer->setMtu(256);
  pServer->setCallbacks(new MyServerCallbacks());
  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  // Create a BLE Characteristic for TX
  pTxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_TX,
    BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_WRITE
  );
  pTxCharacteristic->addDescriptor(new BLE2902());

  //Create a BLE Characteristic for RX
  pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX,
    BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_WRITE
  );
  pRxCharacteristic->addDescriptor(new BLE2902());
  pRxCharacteristic->setCallbacks(new MyRXCallbacks());

  // Start the service
  pService->start();

  // Start advertising
  pServer->getAdvertising()->start();
  Serial.println("Waiting for a client connection...");
}


void bluetoothListener(){
  unsigned long currentMillis = millis();
  unsigned long deltaTime = currentMillis - previousMillis;
  
  if (deviceConnected) {
    pRxCharacteristic->setValue(&txValue, 1);
    pRxCharacteristic->notify();
    txValue++;
    delay(10); // bluetooth stack will go into congestion, if too many packets are sent
  }
  // disconnecting
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // give the bluetooth stack the chance to get things ready
    pServer->startAdvertising(); // restart advertising
    Serial.println("start advertising");
    oldDeviceConnected = deviceConnected;
  }
  // connecting
  if (deviceConnected && !oldDeviceConnected) {
  // do stuff here on connecting
    oldDeviceConnected = deviceConnected;

    Serial.print("연결완료");
    clearSerialBuffer();
  }
}

///////////////////////////////////////////////////////////////////////////
//////////////////////////////////U8G2
///////////////////////////////////////////////////////////////////////////
void initU8G2(){
  u8g2.begin();
  u8g2.enableUTF8Print();
  u8g2.setFontDirection(0);
  u8g2.clearBuffer();
   openingMent();
}
void clearSerialBuffer() {
  while (Serial.available() > 0) {
    Serial.read();
  }
}
void openingMent()
{
  String str =  "banGawer";
  u8g2.clearBuffer(); // 버퍼 초기화
  u8g2.setFont(u8g2_font_prospero_bold_nbp_tr);
  u8g2.drawUTF8(34, 36, str.c_str());
  u8g2.sendBuffer();
}
void connectedMent()
{
  String str2 =  "Device Connected";
  u8g2.clearBuffer(); // 버퍼 초기화
  u8g2.setFont(u8g2_font_lubBI08_te);
  u8g2.drawUTF8(13, 34, str2.c_str());
  u8g2.sendBuffer();
}

/////////////////////////////////////////////////////////////////////////
//////////////////////////////////Recording
/////////////////////////////////////////////////////////////////////////

void spiffFormat(){
  Serial.println("FORMAT START");
  SPIFFS.format();
  Serial.println("FORMAT COMPLETED");
}
void spiffInfo(){
   if (!SPIFFS.begin(true)) {
    Serial.println("An error occurred while mounting SPIFFS");
    return;
  }

  size_t totalBytes = SPIFFS.totalBytes();
  size_t usedBytes = SPIFFS.usedBytes();
  size_t freeBytes = totalBytes - usedBytes;

  Serial.print("Total SPIFFS space: ");
  Serial.print(totalBytes);
  Serial.println(" bytes");

  Serial.print("Used SPIFFS space: ");
  Serial.print(usedBytes);
  Serial.println(" bytes");

  Serial.print("Free SPIFFS space: ");
  Serial.print(freeBytes);
  Serial.println(" bytes");
}
void initRecording(){

  if (!SPIFFS.begin(true))
  {
    Serial.println("SPIFFS Mount Failed");
    while (1);
  }
  spiffInfo();
  buffer = (uint8_t *)malloc(RECORDING_DATA_SIZE);
  memset(buffer, 0, RECORDING_DATA_SIZE);
}

void CreateWavHeader(byte *header, int waveDataSize)
{
  header[0] = 'R';
  header[1] = 'I';
  header[2] = 'F';
  header[3] = 'F';
  unsigned int fileSizeMinus8 = waveDataSize + 44 - 8;
  header[4] = (byte)(fileSizeMinus8 & 0xFF);
  header[5] = (byte)((fileSizeMinus8 >> 8) & 0xFF);
  header[6] = (byte)((fileSizeMinus8 >> 16) & 0xFF);
  header[7] = (byte)((fileSizeMinus8 >> 24) & 0xFF);
  header[8] = 'W';
  header[9] = 'A';
  header[10] = 'V';
  header[11] = 'E';
  header[12] = 'f';
  header[13] = 'm';
  header[14] = 't';
  header[15] = ' ';
  header[16] = 0x10;  // linear PCM
  header[17] = 0x00;
  header[18] = 0x00;
  header[19] = 0x00;
  header[20] = 0x01;  // linear PCM
  header[21] = 0x00;
  header[22] = 0x01;  // monoral
  header[23] = 0x00;
  // header[24] = 0x40;  // sampling rate 8000
  // header[25] = 0x1F;
  // header[26] = 0x00;
  // header[27] = 0x00;
  int sampleRateValue = 4000;
  header[24] = sampleRateValue & 0xFF;
  header[25] = (sampleRateValue >> 8) & 0xFF;
  header[26] = (sampleRateValue >> 16) & 0xFF;
  header[27] = (sampleRateValue >> 24) & 0xFF;
  header[28] = 0x40;  // Byte/sec = 8000x1x1 = 16000
  header[29] = 0x1F;
  header[30] = 0x00;
  header[31] = 0x00;
  header[32] = 0x01;  // 8bit monoral
  header[33] = 0x00;
  header[34] = 0x08;  // 8bit
// 8비트: header[34] = 0x08
// 16비트: header[34] = 0x10
// 24비트: header[34] = 0x18
// 32비트: header[34] = 0x20

  header[35] = 0x00;
  header[36] = 'd';
  header[37] = 'a';
  header[38] = 't';
  header[39] = 'a';
  header[40] = (byte)(waveDataSize & 0xFF);
  header[41] = (byte)((waveDataSize >> 8) & 0xFF);
  header[42] = (byte)((waveDataSize >> 16) & 0xFF);
  header[43] = (byte)((waveDataSize >> 24) & 0xFF);

}


//format bytes
String formatBytes(size_t bytes)
{
  if (bytes < 1024)
  {
    return String(bytes) + "B";
  }
  else if (bytes < (1024 * 1024))
  {
    return String(bytes / 1024.0) + "KB";
  }
  else if (bytes < (1024 * 1024 * 1024))
  {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  }
  else
  {
    return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
  }
}
void print_file_list()
{
  {
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while (file)
    {
      String fileName = file.name();
      size_t fileSize = file.size();
      Serial.printf("FS File: % s, size: % s\n", fileName.c_str(), formatBytes(fileSize).c_str());
      file = root.openNextFile();
    }
    Serial.printf("\n");
    file.close();
  }
}
void setup()
{
  Serial.begin(230400);

  initRecording();
  initU8G2();
  initBLEDevice();

  pinMode(LED_PIN, OUTPUT);  // LED_PIN을 출력으로 설정
  digitalWrite(LED_PIN, LOW);   // LED ON

}
void loop()
{

  bool recentMessageExist = nowCallback != previousCallback;
  if(recentMessageExist)
  {
    currentCursorY = 0;
    maxCursorY = 0;
    previousCallback = nowCallback;

    accumTimeForScroll = 0;
  }
  if (recordMode == 0)
  {
    bluetoothListener();
    digitalWrite(LED_PIN, LOW);   // LED ON
  }
  else if (recordMode == 1)
  {
    Serial.println("recordMode 1");
    recordMode = 2;
    write_data_count = 0;
    strcpy(filename, "/sound1.wav");
    start_millis = millis();
    digitalWrite(LED_PIN, HIGH);   // LED ON
    delay(10);
  }
  else if (recordMode == 2)
  {
    uint16_t val = analogRead(36);
    val = val >> 4;
    buffer[write_data_count] = val;
    write_data_count++;
    if (write_data_count >= RECORDING_DATA_SIZE)
    {
      recordMode = 3;
    }
    delayMicroseconds(16);
  }
  else if(recordMode == 3)
  {
    Serial.println("RECORDING COMPLETED");
    Serial.println("START SAVING");
    Serial.println(millis() - start_millis);
    SPIFFS.remove(filename);
    delay(100);
    file = SPIFFS.open(filename, "w");
    if (file == 0)
    {
      Serial.println("FILE WRITE FAILED");
    }
    CreateWavHeader(header, RECORDING_DATA_SIZE);
    Serial.println(headerSize);
    int sum_size = 0;
    while (sum_size < headerSize)
    {
      sum_size = sum_size + file.write(header + sum_size, headerSize - sum_size);
    }
    Serial.println(RECORDING_DATA_SIZE);
    sum_size = 0;
    while (sum_size < RECORDING_DATA_SIZE)
    {
      sum_size = sum_size + file.write(buffer + sum_size, RECORDING_DATA_SIZE - sum_size);
    }
    file.flush();
    file.close();
    Serial.println("SAVING COMPLETED");
    print_file_list();
    if (deviceConnected) {
      Serial.println("Sending WAV file to the app");

      File wavFile = SPIFFS.open(filename, "r");
      if (!wavFile) {
        Serial.println("Failed to open WAV file");
        return;
      }

      int chunkSize = 20;  // 한 번에 전송할 데이터 크기
      int numChunks = wavFile.size() / chunkSize;  // 전체 데이터를 나눌 청크 수

      for (int i = 0; i < numChunks; i++) {
        uint8_t data[chunkSize];
        int bytesRead = wavFile.read(data, chunkSize);
        if (bytesRead > 0) {
          if(deviceConnected)
          {
            pTxCharacteristic->setValue(data, bytesRead);
            pTxCharacteristic->notify();
            delay(5);  
          }// 전송 간 지연시간
        }
      }
      

      // 남은 데이터 전송 (나누어 떨어지지 않는 경우)
      int remainingSize = wavFile.size() % chunkSize;
      if (remainingSize > 0) {
        uint8_t remainingData[remainingSize];
        int bytesRead = wavFile.read(remainingData, remainingSize);
        if (bytesRead > 0) {
          pTxCharacteristic->setValue(remainingData, bytesRead);
          pTxCharacteristic->notify();
        }
      }
      
      delay(10);  
      uint8_t endPattern[] = {0x45, 0x4E, 0x44}; // "END"의 ASCII 코드
      pTxCharacteristic->setValue(endPattern, sizeof(endPattern));
      pTxCharacteristic->notify();

      wavFile.close();
    }
    recordMode = 0;
  }
}


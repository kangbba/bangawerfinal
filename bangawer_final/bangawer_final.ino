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

U8G2_SSD1325_NHD_128X64_F_4W_HW_SPI u8g2(U8G2_R0, /* cs=*/ 5, /* dc=*/ 2, /* reset=*/ 16); //scl=18, sda=23  SPI로 변경

#define SERVICE_UUID           "4fafc201-1fb5-459e-8fcc-c5c9c331914b" // UART service UUID
#define CHARACTERISTIC_UUID_RX "beb5483e-36e1-4688-b7f5-ea07361b26a8" // 
#define CHARACTERISTIC_UUID_TX "beb5483e-36e1-4688-b7f5-ea07361b26a9" // 

BLEServer *pServer = NULL;
BLECharacteristic * pTxCharacteristic;
BLECharacteristic * pRxCharacteristic;
uint8_t txValue = 0;

// rx callback 
bool deviceConnected = false;
bool oldDeviceConnected = false;
bool newRecentMsgExist = false;
bool isRecentMsgGood  = false;

/// u8g2 text 출력
String recentMessage = "";
int maxCursorY = 0;
int currentCursorY = 0;
int gapWithTextLines = 24;


/////////////////////////////////////////////////////////////////////////
//////////////////////////////////Field(Recording)
/////////////////////////////////////////////////////////////////////////
#include <SPIFFS.h>

#define RECORD_MODE_READY 0 
#define RECORD_MODE_PRE_RECORDING 1
#define RECORD_MODE_RECORDING 2
#define RECORD_MODE_SENDING 3



#define LED_PIN_RECORDING 22 
#define LED_PIN_SENDING 21

#define RECORDING_TIME 5
#define SAMPLE_RATE 8000
#define SAMPLE_SIZE 1  
#define NUM_CHANNELS 1 // Assume mono audio (1 channel)
#define CHUNK_SIZE 80
#define MTU_SIZE 247
// Calculate the recording data size based on the recording time and sample rate
#define RECORDING_DATA_SIZE (RECORDING_TIME * SAMPLE_RATE * SAMPLE_SIZE * NUM_CHANNELS)
#define MICROSECOND_DELAY 30

const int headerSize = 44;
char filename[20] = "/sound1.wav";
byte header[headerSize];
unsigned long write_data_count = 0;
File file;
unsigned long recordStartMilis;
uint8_t *buffer;
int recordMode = 0;

void setup()
{
  Serial.begin(115200);
  initRecording();
  initU8G2();
  initBLEDevice();
  // pinMode(LED_PIN_RECORDING, OUTPUT);  // LED_PIN을 출력으로 설정
  // pinMode(LED_PIN_SENDING, OUTPUT);  // LED_PIN을 출력으로 설정
  // digitalWrite(LED_PIN_RECORDING, LOW);   
  // digitalWrite(LED_PIN_SENDING, HIGH);  
}


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

      String msg = rxValue.c_str(); 

      if(msg == "r0"){   //r0 대기
        Serial.println("r0 도착 -> recordMode0 돌입");
        delay(100);
        recordMode = RECORD_MODE_READY;
      }
      else if(msg == "r1"){  //r1 녹음전 세팅
        Serial.println("r1 도착 -> recordMode1 돌입");
        delay(100);
        if(recordMode == RECORD_MODE_READY){
          recordMode = RECORD_MODE_PRE_RECORDING;
        }
      }
      else{
        recentMessage = msg;
        newRecentMsgExist = true;
        isRecentMsgGood = msg.length() > 0 && msg.indexOf(":") != -1 && msg.indexOf(";") != -1;
      }

      // uint8_t endPattern[] = {0x4F, 0x4B}; // 'O'와 'K'의 ASCII 코드
    }
  }
};
void initBLEDevice()
{
  // Create the BLE Device
  BLEDevice::init("TamiOn");
  // Create the BLE Server
  pServer = BLEDevice::createServer();
  BLEDevice::setMTU(MTU_SIZE);

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
  String str = "TamiOn";
  u8g2.clearBuffer(); // 버퍼 초기화
  u8g2.setFont(u8g2_font_prospero_bold_nbp_tr);
  u8g2.drawUTF8(34, 36, str.c_str());
  u8g2.sendBuffer();
}
void centerText(String str)
{
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
void Message(int langCode, String str)
{
  u8g2.clearBuffer(); // 버퍼 초기화
  ChangeUTF(langCode);
  u8g2.setFlipMode(0);

  u8g2PrintWithEachChar(langCode, str);
  u8g2.sendBuffer();
}
int getCharSize(char c) {
  if ((c & 0x80) == 0) {
    // ASCII 문자인 경우 크기는 1
    return 1;
  } else if ((c & 0xE0) == 0xC0) {
    // 2바이트 문자인 경우 크기는 2
    return 2;
  } else if ((c & 0xF0) == 0xE0) {
    // 3바이트 문자인 경우 크기는 3
    return 3;
  } else if ((c & 0xF8) == 0xF0) {
    // 4바이트 문자인 경우 크기는 4
    return 4;
  } else {
    // 그 외의 경우 잘못된 문자이므로 크기는 1
    return 1;
  }
}
bool isPunctuation(char c) {
    if (c >= 32 && c <= 47) return true; // sp!"#$%&'()*+,-./
    if (c >= 58 && c <= 64) return true; // :;<=>?@
    if (c >= 91 && c <= 96) return true; // [\]^_`
    if (c >= 123 && c <= 126) return true; // {|}~
    return false;
}
bool isAlphabet(char c) {
    return ((c >= 65 && c <= 90) || (c >= 97 && c <= 122));
}
int getCharWidth(char c, int langCode) {

  bool _isPunctuation = isPunctuation(c);
  bool _isAlphabet = isAlphabet(c);
  String s = String(c);
  if (langCode == 5) {  // if language is Chinese
    if (_isPunctuation){
      return 4; 
    }
    else if(_isAlphabet)
    {
      return 8;
    } 
    else {
      return 13;
    }
  } 
  else if (langCode == 12) {
    if (_isPunctuation){
      return 4; 
    }
    else {
      return 16; 
    }
  } 
  else if (langCode == 10) {
    if (_isPunctuation){
      return 4; 
    }
    else {
      return 14; 
    }
  } 
  else { 
    if (_isPunctuation){
      return 4; 
    }
    else {
      return 8; 
    }
   // return u8g2.getUTF8Width(s.c_str());
  }
}

void u8g2PrintWithEachChar(int langCode, String str)
{
  String str_obj = String(str);
  int initialHeight = 5;
  int padding = 2;
  int height = initialHeight + gapWithTextLines;
  u8g2_uint_t x = 0, y = height;
  u8g2.setCursor(padding, y - currentCursorY);
  for (int i = 0; i < str.length();) {
    int charSize = getCharSize(str[i]); // 다음 문자의 크기 계산
    String currentCharStr = str.substring(i, i+charSize); // 다음 문자 추출
    char currentChar = currentCharStr.charAt(0);
    int charWidth = getCharWidth(currentChar, langCode);
    if (x + charWidth >  108) {
        x = padding;
        y += gapWithTextLines;
        u8g2.setCursor(x, y - currentCursorY);
    }
    u8g2.print(currentCharStr);
    x += charWidth;
    i += charSize; 
  }
  maxCursorY = y + gapWithTextLines * 2;
}

void ChangeUTF(int langCodeInt)
{
  int CHARCOUNT_ENGLISH = 17;
  int CHARCOUNT_STANDARD = 15;
  int CHARCOUNT_CHINA = 27;
  int CHARCOUNT_EUROPE = 17;
  int CHARCOUNT_RUSSIA = 27;
  const uint8_t *FONT_ENGLISH = u8g2_font_ncenR12_tr; 
  const uint8_t *FONT_KOREA = u8g2_korea_kang4; 
  const uint8_t *FONT_STANDARD = u8g2_font_unifont_t_symbols; 
  const uint8_t *FONT_EUROPE = u8g2_font_7x14_tf; 
  const uint8_t *FONT_CHINA = u8g2_font_wqy14_t_gb2312a; 
  const uint8_t *FONT_JAPAN = u8g2_font_unifont_t_japanese1; 
  const uint8_t *FONT_RUSSIA = u8g2_font_cu12_t_cyrillic; 
  switch (langCodeInt) {
    case 1: // English
        u8g2.setFont(FONT_ENGLISH);
        break;
    case 2: // Spanish
        u8g2.setFont(FONT_EUROPE); 
        break;
    case 3: // French
        u8g2.setFont(FONT_EUROPE); 
        break;
    case 4: // German
        u8g2.setFont(FONT_EUROPE); 
        break;
    case 5: // Chinese
         u8g2.setFont(FONT_CHINA); //중국어 4040자 133,898바이트
        break;
    case 6: // Arabic
        u8g2.setFont(u8g2_font_cu12_t_arabic); 
        break;
    case 7: // Russian
        u8g2.setFont(FONT_RUSSIA); 
        break;
    case 8: // Portuguese
        u8g2.setFont(FONT_STANDARD); 
        break;
    case 9: // Italian
        u8g2.setFont(FONT_EUROPE); 
        break;
    case 10: // Japanese
        u8g2.setFont(FONT_JAPAN);
        break;
    case 11: // Dutch
        u8g2.setFont(FONT_EUROPE); 
        break;
    case 12: // Korean
        u8g2.setFont(u8g2_korea_kang4);
        break;
    case 13: // Swedish å 
        u8g2.setFont(FONT_EUROPE); 
        break;
    case 14: // Turkish
        u8g2.setFont(FONT_EUROPE); 
        break;
    case 15: // Polish
        u8g2.setFont(u8g2_font_helvR12_te); 
        break;
    case 16: // Danish å 
        u8g2.setFont(FONT_EUROPE); 
        break;
    case 17: // Norwegian å 
        u8g2.setFont(FONT_EUROPE); 
        break;
    case 18: // Finnish
        u8g2.setFont(FONT_EUROPE); 
        break;
    case 19: // Czech
        u8g2.setFont(u8g2_font_helvR12_te); 
        break;
    case 20: // Thai
        u8g2.setFont(u8g2_font_etl24thai_t);
        break;
    case 21: // Greek
        u8g2.setFont(u8g2_font_unifont_t_greek); 
        break;
    case 22: // Hungarian
        u8g2.setFont(FONT_EUROPE); 
        break;
    case 23: // Hebrew
        u8g2.setFont(u8g2_font_cu12_t_hebrew); 
        break;
    case 24: // Romanian
        u8g2.setFont(FONT_EUROPE); 
        break;
    case 25: // Ukrainian
        u8g2.setFont(FONT_RUSSIA); 
        break;
    case 26: // Vietnamese
        u8g2.setFont(u8g2_font_unifont_t_vietnamese2);
        break;
    case 27: // Icelandic
        u8g2.setFont(FONT_EUROPE); 
        break;
    case 28: // Bulgarian
        u8g2.setFont(FONT_RUSSIA); 
        break;
    case 29: // Lithuanian
        u8g2.setFont(u8g2_font_helvR12_te); 
        break;
    case 30: // Latvian
        u8g2.setFont(u8g2_font_helvR12_te); 
        break;
    case 31: // Slovenian
        u8g2.setFont(u8g2_font_helvR12_te); 
        break;
    case 32: // Croatian
        u8g2.setFont(u8g2_font_helvR12_te); 
        break;
    case 33: // Estonian
        u8g2.setFont(FONT_STANDARD); 
        break;
    case 41: // Indonesian
        u8g2.setFont(FONT_EUROPE); 
        break;
    default:
        u8g2.setFont(FONT_STANDARD); 
        break;
  }
}

String replaceChinesePunctuations(String str) {
  const char* punctuations[] = {"，", "。", "！", "？", "；", "：", "、", "（", "）"};
  const char* punctuationsForReplace[] = {", ", "。", "!", "?", ";", ":", "、", "(", ")"};
  for (int i = 0; i < sizeof(punctuations)/sizeof(punctuations[0]); i++) {
    str.replace(punctuations[i], punctuationsForReplace[i]);
  }
  return str;
}
void parseLangCodeAndMessage(String input, int &langCode, String &someMsg) {
  int separatorIndex = input.indexOf(":");
  langCode = input.substring(0, separatorIndex).toInt();
  someMsg = input.substring(separatorIndex + 1, input.indexOf(";"));
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
  header[22] = NUM_CHANNELS;  // monoral
  header[23] = 0x00;
  int sampleRateValue = SAMPLE_RATE;
  header[24] = sampleRateValue & 0xFF;
  header[25] = (sampleRateValue >> 8) & 0xFF;
  header[26] = (sampleRateValue >> 16) & 0xFF;
  header[27] = (sampleRateValue >> 24) & 0xFF;
  int byteRate = SAMPLE_RATE * NUM_CHANNELS * SAMPLE_SIZE;
  header[28] = byteRate & 0xFF;        // Byte/sec
  header[29] = (byteRate >> 8) & 0xFF;
  header[30] = (byteRate >> 16) & 0xFF;
  header[31] = (byteRate >> 24) & 0xFF;
  header[32] = NUM_CHANNELS * SAMPLE_SIZE;  // Block align
  header[33] = 0x00;
  header[34] = 8 * SAMPLE_SIZE;  // Bits per sample

  header[35] = 'd';
  header[36] = 'a';
  header[37] = 't';
  header[38] = 'a';
  header[39] = (byte)(waveDataSize & 0xFF);
  header[40] = (byte)((waveDataSize >> 8) & 0xFF);
  header[41] = (byte)((waveDataSize >> 16) & 0xFF);
  header[42] = (byte)((waveDataSize >> 24) & 0xFF);
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
void loop()
{
  if (recordMode == RECORD_MODE_READY) // r0 대기상태
  {
    bluetoothListener();
    if(newRecentMsgExist)
    {
      Serial.println("새로운 recentMessage가 있습니다");
      newRecentMsgExist = false;
      if (isRecentMsgGood) 
      {
        int langCode;
        String someMsg;
        parseLangCodeAndMessage(recentMessage, langCode, someMsg);
        if(langCode == 5)
        {
          someMsg = replaceChinesePunctuations(someMsg);
        }
        Message(langCode, someMsg);
      } 
    }
  }
  else if (recordMode == RECORD_MODE_PRE_RECORDING) // r1 녹음전 세팅
  { 
    centerText("PRE_RECORDING");
    Serial.println("PRE_RECORDING");
    write_data_count = 0;
    strcpy(filename, "/sound1.wav");
    recordStartMilis = millis();// LED ON)
    delay(1000);
    recordMode = RECORD_MODE_RECORDING;
    centerText("RECORDING");
  }
  else if (recordMode == RECORD_MODE_RECORDING) // r2 녹음
  {
    // digitalWrite(LED_PIN_RECORDING, HIGH);   // LED ON
    // digitalWrite(LED_PIN_SENDING, LOW);   
    uint16_t val = analogRead(36);
    val = val >> 4;
    buffer[write_data_count] = val;
    write_data_count++;
    if (write_data_count >= RECORDING_DATA_SIZE)
    {
     recordMode = RECORD_MODE_SENDING;
    }
    delayMicroseconds(MICROSECOND_DELAY);
  }
  else if(recordMode == RECORD_MODE_SENDING) // r3. 전송
  {
    Serial.println("RECORD_MODE_SENDING");
    Serial.println("START SAVING");
    Serial.println("설정된 microSecond delay");
    Serial.println(MICROSECOND_DELAY);
    Serial.println("목표 녹음시간");
    Serial.println(RECORDING_TIME * 1000);
    Serial.println("실제 녹음시간");
    Serial.println(millis() - recordStartMilis);
    delay(10);
    
    centerText("Ready..");
    ////////전처리
    SPIFFS.remove(filename);
    delay(100);
    file = SPIFFS.open(filename, "w");
    if (file == 0)
    {
      centerText("FILE WRITE FAILED");
      Serial.println("FILE WRITE FAILED");
      recordMode = RECORD_MODE_READY;
      return;
    }
    CreateWavHeader(header, RECORDING_DATA_SIZE);

    Serial.println(headerSize);
    int sum_size = 0;
    while (sum_size < headerSize && recordMode == RECORD_MODE_SENDING)
    {
      sum_size = sum_size + file.write(header + sum_size, headerSize - sum_size);
    }
    Serial.println(RECORDING_DATA_SIZE);
    sum_size = 0;
    while (sum_size < RECORDING_DATA_SIZE && recordMode == RECORD_MODE_SENDING)
    {
      sum_size = sum_size + file.write(buffer + sum_size, RECORDING_DATA_SIZE - sum_size);
    }
    file.flush();
    file.close();
    centerText("Completed");
    Serial.println("SAVING COMPLETED");
    print_file_list();
    Serial.println("Sending WAV file to the app");

    ////////전송작업
    centerText("Sending..");
    sendingProcess();
    delay(10);
    for(int i = 0 ; i < 10 ; i++)
    {
      sendMsgToFlutter("END");
      delay(10);
    }
    recordMode = RECORD_MODE_READY;
    centerText(recentMessage);
  }
  

}
void sendingProcess() {
  if(recordMode != RECORD_MODE_SENDING){
    return;
  }
  // 파일 오픈
  File wavFile = SPIFFS.open(filename, "r");
  if (!wavFile) {
    Serial.println("Failed to open WAV file");
    return;
  }

  // 파일 크기 얻기
  size_t fileSize = wavFile.size();
  Serial.print("FILE SIZE IS: ");
  Serial.println(fileSize);

  int bytesRead;
  int chunkSize = CHUNK_SIZE;
  while (fileSize > 0 && recordMode == 3) {
    int bytesRead = wavFile.read(buffer, chunkSize);
    if (bytesRead > 0) {
      // 딜리미터를 추가하여 청크의 끝을 표시
      pTxCharacteristic->setValue(buffer, bytesRead);
      pTxCharacteristic->notify();

      // // 로그로 전송한 데이터의 첫 번째 값과 마지막 값을 출력
      // Serial.print("Data sent: [");
      // for (int i = 0; i < bytesRead; i++) {
      //   Serial.print(buffer[i]);
      //   if (i < bytesRead - 1) {
      //     Serial.print(", ");
      //   }
      // }
      // Serial.println("]");
    }
    fileSize -= bytesRead;
    delay(20);
  }
  // 파일 닫기
  wavFile.close();
}

void sendMsgToFlutter(const String &data) {

  // 문자열 데이터를 바이트 배열로 변환하여 전송
  uint8_t* byteArray = (uint8_t*)data.c_str();
  size_t byteLength = data.length();
  
  pTxCharacteristic->setValue(byteArray, byteLength);
  pTxCharacteristic->notify();
}

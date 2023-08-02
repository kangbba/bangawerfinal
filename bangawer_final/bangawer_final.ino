/////////////////////////////////////////////////////////////////////////
//////////////////////////////////Field(OLD)
/////////////////////////////////////////////////////////////////////////

#include "BluetoothA2DPSink.h"
BluetoothA2DPSink a2dp_sink;

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
//////////////////////////////////Field(Record Switch)
/////////////////////////////////////////////////////////////////////////


const int SWITCH_PIN = 15; // 스위치가 연결된 디지털 핀 번호 (원하는 핀 번호로 변경 가능)
int prevSwitchState = HIGH; // 이전 스위치 상태를 저장하는 변수, 초기 상태는 HIGH(눌리지 않은 상태)로 설정
unsigned long prevMillis = 0; // 이전 상태를 측정한 시간을 저장하는 변수
const unsigned long debounceDelay = 1000; // 중복 처리 방지를 위한 디바운싱 딜레이 (1000ms, 1초)


void switchReading(){
  int switchState = digitalRead(SWITCH_PIN); // 스위치 상태를 읽어옵니다.
  // 시간 차이를 측정합니다.
  unsigned long currentMillis = millis();
  unsigned long timeDiff = currentMillis - prevMillis;

  // 이전 상태와 현재 상태를 비교하여 스위치 상태가 변했고, 시간 차이가 1000ms 이상인 경우에만 처리를 수행합니다.
  if (switchState != prevSwitchState && timeDiff >= debounceDelay) {
    // 스위치의 눌림 상태가 변경되었을 때 실행할 코드를 작성합니다.
    if (switchState == LOW) { // 스위치가 눌렸을 때(내부 풀업 사용 시, LOW는 눌림 상태를 의미합니다.)
      // 스위치가 눌렸을 때 실행할 코드를 작성합니다.

      Serial.println("스위치 눌림");
      sendMsgToFlutter("SWITCH");
      
    } else {
      // 스위치가 눌리지 않았을 때 실행할 코드를 작성합니다.

      Serial.println("스위치 눌리지 않음");
    }

    // 현재 상태와 시간을 이전 상태와 시간으로 업데이트합니다.
    prevSwitchState = switchState;
    prevMillis = currentMillis;
  }
}

/////////////////////////////////////////////////////////////////////////
//////////////////////////////////Field(Recording)
/////////////////////////////////////////////////////////////////////////

#include <SPIFFS.h>

#define RECORD_MODE_READY 0 
#define RECORD_MODE_PRE_RECORDING 1
#define RECORD_MODE_RECORDING 2
#define RECORD_MODE_SENDING 3

#define LED_PIN_RECORDING 21
#define LED_PIN_SENDING 22

//  (1초당 전송하는 패킷의 양) => PACKET_PER_SEC
//  (예상 소요 시간(초)) => (RECORDING_DATA_SIZE / PACKET_AMOUNT_PER_SEC

//데이터 전송속도 옵션
#define CHUNK_SIZE 220
#define CHUNK_DELAY 13

//용량 옵션   
#define RECORDING_TIME 5000
#define SAMPLE_RATE 8000
#define MICROSECOND_DELAY 30


//건들면 안되는 옵션
#define MTU_SIZE 247
#define SAMPLE_SIZE 1  
#define NUM_CHANNELS 1 // Assume mono audio (1 channel)
#define RECORDING_DATA_SIZE (RECORDING_TIME * SAMPLE_RATE * SAMPLE_SIZE * NUM_CHANNELS / 1000)
#define PACKET_AMOUNT_PER_SEC ((float)CHUNK_SIZE / CHUNK_DELAY) * 1000
#define PREDICTING_SEC  ((float)RECORDING_DATA_SIZE / PACKET_AMOUNT_PER_SEC)

//실제 녹음시간이 RECORDING_TIME보다 많이나오면 MICROSECOND_DELAY를 줄여야함


// (5초기준) 확인된 정보들
// SAMPLE_RATE => MICROSECOND_DELAY => 실제녹음시간(ms) => 데이터길이 (DATA 길이)
// 8000 => 30 => 5000 => 40000
// 6000 => 70 => 4988 => 30000
// 4000 => 100 => 4978 => 20044  


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
  initBluetoothSpeaker();

  
  pinMode(SWITCH_PIN, INPUT_PULLUP); // 스위치 핀을 내부 풀업으로 설정
  pinMode(LED_PIN_RECORDING, OUTPUT);  // LED_PIN을 출력으로 설정
  pinMode(LED_PIN_SENDING, OUTPUT);  // LED_PIN을 출력으로 설정
  digitalWrite(LED_PIN_RECORDING, LOW);   
  digitalWrite(LED_PIN_SENDING, LOW);  
}


/////////////////////////////////////////////////////////////////////////
//////////////////////////////////BLUETOOTH
/////////////////////////////////////////////////////////////////////////

class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    centerText("CONNECTED");
  };

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    centerText("DISCONNECTED");
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
        Serial.print("새 메세지 도착 : ");
        Serial.println(newRecentMsgExist);
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
//////////////////////////////////Speaker
/////////////////////////////////////////////////////////////////////////
void initBluetoothSpeaker(){
  i2s_pin_config_t my_pin_config = {
          .bck_io_num = 27,//BCLK
          .ws_io_num = 26,//LRC
          .data_out_num = 25,//DIN
          
          .data_in_num = I2S_PIN_NO_CHANGE
      };
      a2dp_sink.set_pin_config(my_pin_config);
      a2dp_sink.start("TamiOn");
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

    switchReading();
    delay(10);
  }
  else if (recordMode == RECORD_MODE_PRE_RECORDING) // r1 녹음전 세팅
  { 
    clearSerialBuffer();
    Serial.print("RECORDING_DATA_SIZE : ");
    Serial.println(RECORDING_DATA_SIZE);
    Serial.print("PACKET_AMOUNT_PER_SEC : ");
    Serial.println(PACKET_AMOUNT_PER_SEC);
    Serial.print("PREDICTING_SEC : ");
    Serial.println(PREDICTING_SEC);

    digitalWrite(LED_PIN_RECORDING, LOW);   // LED ON
    digitalWrite(LED_PIN_SENDING, LOW);  
    Serial.println("PRE_RECORDING");
    write_data_count = 0;
    strcpy(filename, "/sound1.wav");
    ////////전처리
    SPIFFS.remove(filename);
    delay(100);
    

    centerText("RECORDING");
    digitalWrite(LED_PIN_RECORDING, HIGH);   // LED ON
    digitalWrite(LED_PIN_SENDING, LOW);  

    recordStartMilis = millis();// LED ON)
    recordMode = RECORD_MODE_RECORDING;

  }
  else if (recordMode == RECORD_MODE_RECORDING) // r2 녹음
  { 
    uint16_t val = analogRead(36);
    val = val >> 4;
    buffer[write_data_count] = val;
    write_data_count ++;
    if (write_data_count >= RECORDING_DATA_SIZE)
    {
     recordMode = RECORD_MODE_SENDING;
    }
    delayMicroseconds(MICROSECOND_DELAY);
  }
  else if(recordMode == RECORD_MODE_SENDING) // r3. 전송
  {  
    centerText("SENDING");
    digitalWrite(LED_PIN_RECORDING, LOW);   // LED ON
    digitalWrite(LED_PIN_SENDING, HIGH);  
    sendMsgToFlutter("START");
    
    Serial.println("RECORD_MODE_SENDING");
    Serial.println("START SAVING");
    Serial.println("설정된 microSecond delay");
    Serial.println(MICROSECOND_DELAY);
    Serial.println("목표 녹음시간");
    Serial.println(RECORDING_TIME);
    Serial.println("실제 녹음시간");
    Serial.println(millis() - recordStartMilis);

    file = SPIFFS.open(filename, "w");
    if (file == 0)
    {
      centerText("FILE WRITE FAILED");
      Serial.println("FILE WRITE FAILED");
      recordMode = RECORD_MODE_READY;
      return;
    }

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
    centerText("COMPLETED");
    Serial.println("SAVING COMPLETED");
    print_file_list();
    Serial.println("Sending WAV file to the app");

    ////////전송작업
    centerText("SENDING");
    sendingProcess();
    delay(10);
    sendMsgToFlutter("END");
    centerText("-");
    
    digitalWrite(LED_PIN_RECORDING, LOW);   // LED ON
    digitalWrite(LED_PIN_SENDING, LOW);  
    recordMode = RECORD_MODE_READY;
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

  int bytesToSkip = 0;

  if(fileSize < bytesToSkip * 2){
    recordMode = RECORD_MODE_READY;
  }
  else {
    //여기에 넣어줘
    int bytesRead;
    int chunkSize = CHUNK_SIZE;
    while (fileSize > 0 && recordMode == RECORD_MODE_SENDING) {
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
      delay(CHUNK_DELAY);
    }
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

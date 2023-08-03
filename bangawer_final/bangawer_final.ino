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

const int SWITCH_PIN = 4; // 스위치가 연결된 디지털 핀 번호 (원하는 핀 번호로 변경 가능)
int prevSwitchState = HIGH; // 이전 스위치 상태를 저장하는 변수, 초기 상태는 HIGH(눌리지 않은 상태)로 설정
bool isPressed = false; // 스위치가 눌린 상태를 저장하는 변수

void switchReading() {
  int switchState = digitalRead(SWITCH_PIN); // 스위치 상태를 읽어옵니다.

  if (switchState == LOW && prevSwitchState == HIGH) {
    // 스위치가 눌린 순간에 실행할 코드를 작성합니다.
    isPressed = true;
    Serial.println("스위치 눌림");
    sendMsgToFlutter("SWITCH");
  } else if (switchState == HIGH && prevSwitchState == LOW) {
    // 스위치를 놓았을 때 실행할 코드를 작성합니다.
    isPressed = false;
  }

  prevSwitchState = switchState;
}






/////////////////////////////////////////////////////////////////////////
//////////////////////////////////Field(Recording)
/////////////////////////////////////////////////////////////////////////


#define RECORD_MODE_READY 0 
#define RECORD_MODE_PRE_RECORDING 1
#define RECORD_MODE_RECORDING 2
#define RECORD_MODE_COMPLETED 3

#define LED_PIN_RECORDING 22
#define LED_PIN_SENDING 21

//  (1초당 전송하는 패킷의 양) => PACKET_PER_SEC
//  (예상 소요 시간(초)) => (RECORDING_DATA_SIZE / PACKET_AMOUNT_PER_SEC

//데이터 전송속도 옵션
#define CHUNK_SIZE 200

//용량 옵션   
#define RECORDING_TIME 5000
#define SAMPLE_RATE 6000
#define MICROSECOND_DELAY 50

//RECORDING_TIME / SAMPLE_RATE / MICROSECOND_DELAY
//5000 / 6000 / 50


//건들면 안되는 옵션
#define MTU_SIZE 247
#define SAMPLE_SIZE 1  
#define NUM_CHANNELS 1 // Assume mono audio (1 channel)
#define RECORDING_DATA_SIZE (RECORDING_TIME * SAMPLE_RATE * SAMPLE_SIZE * NUM_CHANNELS / 1000)

//실제 녹음시간이 RECORDING_TIME보다 많이나오면 MICROSECOND_DELAY를 줄여야함


// (5초기준) 확인된 정보들
// SAMPLE_RATE => MICROSECOND_DELAY => 실제녹음시간(ms) => 데이터길이 (DATA 길이)
// 8000 => 30 => 5000 => 40000
// 6000 => 70 => 5170 => 30000
// 6000 => 80 => 5472 => 30000
// 6000 => 60 => 5472 => 30000
// 4000 => 100 => 4978 => 20044  


unsigned long write_data_count = 0;
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
        digitalWrite(LED_PIN_RECORDING, LOW);   // LED ON
        digitalWrite(LED_PIN_SENDING, LOW);  
        centerText("READY");
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
    clearSerialBufferRX();
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
void clearSerialBufferTX() {
  while (Serial.availableForWrite() > 0) {
    Serial.write(Serial.read()); // tx 버퍼에서 한 바이트씩 읽어서 tx 버퍼를 비움
  }
}
void clearSerialBufferRX() {
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

void initRecording(){

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



void drawBatteryIcon(int x, int y, int width, int height, int batteryLevel) {
  // Draw battery icon frame
  u8g2.drawFrame(x, y, width, height);

  // Calculate battery level width
  int batteryWidth = map(batteryLevel, 0, 100, 0, width - 2);

  // Draw battery level
  u8g2.drawBox(x + 1, y + 1, batteryWidth, height - 2);
}

void drawScrollableContent() {
  // Content to be scrolled (replace with your content)
  const char* content1 = "Line 1";
  const char* content2 = "Line 2";
  const char* content3 = "Line 3";

  int startY = 30; // Y-coordinate to start drawing content

  u8g2.setFont(u8g2_font_ncenB08_tr); // Set font size
  u8g2.drawStr(0, startY, content1);   // Draw first line
  u8g2.drawStr(0, startY + 10, content2); // Draw second line
  u8g2.drawStr(0, startY + 20, content3); // Draw third line
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
  }
  else if (recordMode == RECORD_MODE_PRE_RECORDING) // r1 녹음전 세팅
  { 
    clearSerialBufferRX();
    centerText("RECORDING");
    digitalWrite(LED_PIN_RECORDING, HIGH);   // LED ON
    digitalWrite(LED_PIN_SENDING, LOW);  
    write_data_count = 0;
    sendMsgToFlutter("START");
    delay(10);
    recordStartMilis = millis();// LED ON)
    recordMode = RECORD_MODE_RECORDING;
  }
  else if (recordMode == RECORD_MODE_RECORDING) // r2 녹음
  { 
    uint16_t val = analogRead(36);
    val = val >> 4;
    buffer[write_data_count] = val;
    write_data_count++;
    if (write_data_count % CHUNK_SIZE == 0 || write_data_count >= RECORDING_DATA_SIZE) {
      // 청크를 txCharacteristic를 통해 보냅니다
      uint16_t chunkStartIndex = write_data_count - CHUNK_SIZE;
      uint16_t chunkLength = min(CHUNK_SIZE, RECORDING_DATA_SIZE - chunkStartIndex);
      pTxCharacteristic->setValue(&buffer[chunkStartIndex], chunkLength * sizeof(uint16_t));
      pTxCharacteristic->notify();

      // 진행 상황을 로그로 출력합니다
      Serial.print(write_data_count);
      Serial.print("/");
      Serial.print(RECORDING_DATA_SIZE);
      Serial.print(" - ");
      Serial.print(millis() - recordStartMilis);
      Serial.println("");

      // 녹음이 끝났을 경우, 끝을 로그로 출력합니다
      if (write_data_count >= RECORDING_DATA_SIZE) {
         recordMode = RECORD_MODE_COMPLETED;
      }
    }

    delayMicroseconds(MICROSECOND_DELAY);
  }
  else if(recordMode == RECORD_MODE_COMPLETED) // r3. 전송
  {  
    delay(10);
    sendMsgToFlutter("END");    
    centerText("COMPLETED");
    digitalWrite(LED_PIN_RECORDING, LOW);   // LED ON
    digitalWrite(LED_PIN_SENDING, LOW);  
    Serial.println("녹음이 완료되었습니다.");
    Serial.print("설정된 microSecond delay : ");
    Serial.println(MICROSECOND_DELAY);
    Serial.print("목표 녹음시간 : ");
    Serial.println(RECORDING_TIME);
    Serial.print("실제 녹음시간 : ");
    Serial.println(millis() - recordStartMilis);
    // 여기서 녹음 종료 후 원하는 동작을 추가하세요.
    delay(1000);
    recordMode = RECORD_MODE_READY;
  }
  

}

void sendMsgToFlutter(const String &data) {
  Serial.println(data);
  // 문자열 데이터를 바이트 배열로 변환하여 전송
  uint8_t* byteArray = (uint8_t*)data.c_str();
  size_t byteLength = data.length();
  
  pTxCharacteristic->setValue(byteArray, byteLength);
  pTxCharacteristic->notify();
}

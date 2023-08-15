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

/// u8g2 text 출력
String recentMessage = "";
const int GAP_BETWEEN_TEXTLINES = 24;
int maxCursorY = 0;

// 함수 정의
int langCode = 0;
String translatedMsg = "";

//긴 텍스트를 위한 스크롤 기능
unsigned int nowCursorY = 0;
unsigned long scrollStartTime = 0;
long accumTimeForScroll = 0;
const unsigned long scrollEndWaitTime = 4000;
unsigned long previousMillis = 0; 

//아래의 두개 수정가능
int scrollStartDelayTime = 3000; // (3000이면 3초있다가 스크롤 시작)
int scrollPeriod = 200;// (이값이 클수록 스크롤 속도가 느려짐)


//원격스위치
const int SWITCH_PIN = 17; // 스위치가 연결된 디지털 핀 번호 (원하는 핀 번호로 변경 가능)
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
#define CHUNK_SIZE 100

//용량 옵션   
#define RECORDING_TIME 5000
#define SAMPLE_RATE 8000
#define MICROSECOND_DELAY 140
//실제 녹음시간이 RECORDING_TIME보다 많이나오면 MICROSECOND_DELAY를 줄여야함
//실제 녹음시간이 RECORDING_TIME보다 조금나오면 MICROSECOND_DELAY를 늘려야함

//CHUNK_SIZE / SAMPLE_RATE / MICROSECOND_DELAY
//240 / 6000 / 120
//240 / 7000 / 115
//240 / 8000 / 100

//건들면 안되는 옵션
#define MTU_SIZE 247
#define SAMPLE_SIZE 1  
#define NUM_CHANNELS 1 // Assume mono audio (1 channel)
#define RECORDING_DATA_SIZE (RECORDING_TIME * SAMPLE_RATE * SAMPLE_SIZE * NUM_CHANNELS / 1000)



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
    centerText("CONNECTED", 34);
  };

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    centerText("DISCONNECTED", 26);
  }
};
bool scrollOn = false;
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

      if(msg == "r0"){   
        
      }
      else if(msg == "r1"){  //r1 녹음전 세팅
        Serial.println("r1 도착 -> recordMode1 돌입");
        if(recordMode == RECORD_MODE_READY){
          recordMode = RECORD_MODE_PRE_RECORDING;
        }
      }
      else{ // 01:sample; 이런 문자 도착
        Serial.println("r0 도착 -> recordMode0 돌입");
        digitalWrite(LED_PIN_RECORDING, LOW);   // LED ON
        digitalWrite(LED_PIN_SENDING, LOW);  
        delay(10);  
        
        parseLangCodeAndMessage(msg);
        Serial.print("새 메세지 도착 : ");
        newRecentMsgExist = true;
        accumTimeForScroll = 0; 
        scrollOn = false;
        nowCursorY = 0;
        recordMode = RECORD_MODE_READY;  
      }

      // uint8_t endPattern[] = {0x4F, 0x4B}; // 'O'와 'K'의 ASCII 코드
    }
  }
};
void parseLangCodeAndMessage(String input) {
  
  int separatorIndex = input.indexOf(":");
  bool isRecentMsgGood = input.length() > 0 && input.indexOf(":") != -1 && input.indexOf(";") != -1;
  if(isRecentMsgGood){
    langCode = input.substring(0, separatorIndex).toInt();
    String tmpMsg = input.substring(separatorIndex + 1, input.indexOf(";"));
    if(langCode == 5)
    {
      translatedMsg = replaceChinesePunctuations(tmpMsg);
    }
    else{
      translatedMsg = tmpMsg;
    }
  }
  else{
    langCode = 0;
    translatedMsg = "";
  }
 
}
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
  u8g2.drawUTF8(36, 36, str.c_str());
  u8g2.sendBuffer();
}
void centerText(String str, int x)
{
  u8g2.clearBuffer(); // 버퍼 초기화
  u8g2.setFont(u8g2_font_prospero_bold_nbp_tr);
  u8g2.drawUTF8(x, 36, str.c_str());
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
  int height = initialHeight + GAP_BETWEEN_TEXTLINES;
  u8g2_uint_t x = 0, y = height;
  u8g2.setCursor(padding, y - nowCursorY);
  for (int i = 0; i < str.length();) {
    int charSize = getCharSize(str[i]); // 다음 문자의 크기 계산
    String currentCharStr = str.substring(i, i+charSize); // 다음 문자 추출
    char currentChar = currentCharStr.charAt(0);
    int charWidth = getCharWidth(currentChar, langCode);
    if (x + charWidth >  108) {
        x = padding;
        y += GAP_BETWEEN_TEXTLINES;
        u8g2.setCursor(x, y - nowCursorY);
    }
    u8g2.print(currentCharStr);
    x += charWidth;
    i += charSize; 
  }
  maxCursorY = y + GAP_BETWEEN_TEXTLINES * 2;
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
unsigned long previousMicros_record = 0;
void loop()
{
  if (recordMode == RECORD_MODE_READY) // r0 대기상태
  {
    bluetoothListener();
    unsigned long currentMillis = millis();
    unsigned long deltaTime = currentMillis - previousMillis;
    if(scrollOn){
      if (currentMillis - scrollStartTime >= scrollPeriod) { // 매 루프 간격이아닌, 일정 주기마다 실행
         //스크롤 중
        int maxLineCount = 4; // 수정가능
        int onePageHeight = GAP_BETWEEN_TEXTLINES * maxLineCount;
        if (maxCursorY >= onePageHeight && nowCursorY < (maxCursorY - onePageHeight)) {
          Message(langCode, translatedMsg);
          nowCursorY++;
        }
        else {
         //스크롤 끝
          scrollStartTime = currentMillis;
          nowCursorY = 0; // 스크롤 다한 후에 멈춰놓고싶으면 여기를 주석처리할것, 
          accumTimeForScroll = -scrollEndWaitTime;
          scrollOn = false;
        }
      }
    }
    else{
      if (accumTimeForScroll >= scrollStartDelayTime){ 
        //스크롤 시작
        scrollOn = true;
      }
      else{
        //스크롤 대기중
        if(newRecentMsgExist)
        {
          Serial.println("일단 한번 프린트하고,");
          newRecentMsgExist = false;
          Message(langCode, translatedMsg);
        }
        accumTimeForScroll += deltaTime;
      }
    }
    previousMillis = currentMillis;
  
    switchReading();
  }
  else if (recordMode == RECORD_MODE_PRE_RECORDING) // r1 녹음전 세팅
  { 
    write_data_count = 0;
    clearSerialBufferRX();
    delay(1); // 1ms의 딜레이를 줍니다.
    centerText("RECORDING", 34);
    digitalWrite(LED_PIN_RECORDING, HIGH);   // LED ON
    digitalWrite(LED_PIN_SENDING, LOW);  
    delay(100);
    sendMsgToFlutter("START");
    delay(10);
    recordStartMilis = millis();// LED ON)
    recordMode = RECORD_MODE_RECORDING;
  }
  else if (recordMode == RECORD_MODE_RECORDING) // r2 녹음
  { 
    unsigned long currentMicros = micros();
    unsigned long timeDiff = currentMicros - previousMicros_record;
    if (timeDiff >= MICROSECOND_DELAY) {
      uint16_t val = analogRead(36);
      val = val >> 4;
      buffer[write_data_count] = val;
      write_data_count++;
      // 원하는 지연 시간이 지났을 때에만 작업 수행
      previousMicros_record = currentMicros;
      // 지연 시간이 지날 때마다 작업 처리
    }

    if (write_data_count >= RECORDING_DATA_SIZE) {
      recordMode = RECORD_MODE_COMPLETED;
    }
  }
  else if (recordMode == RECORD_MODE_COMPLETED) // r3. 완료
  {  

    // 녹음이 끝났을 경우, 끝을 로그로 출력합니다
    Serial.println("녹음이 완료되었습니다.");
    Serial.print("설정된 microSecond delay : ");
    Serial.println(MICROSECOND_DELAY);
    Serial.print("목표 녹음시간 : ");
    Serial.println(RECORDING_TIME);
    Serial.print("실제 녹음시간 : ");
    Serial.println(millis() - recordStartMilis);

    uint16_t chunkStartIndex = 0;
    while (chunkStartIndex < RECORDING_DATA_SIZE) {
      uint16_t chunkLength = min(CHUNK_SIZE, RECORDING_DATA_SIZE - chunkStartIndex);
      pTxCharacteristic->setValue(&buffer[chunkStartIndex], chunkLength * sizeof(uint16_t));
      pTxCharacteristic->notify();
      chunkStartIndex += CHUNK_SIZE;
      delay(5);
      // 추가로 원하는 동작을 수행할 수 있습니다.
      // 예를 들면 LED를 깜빡이거나 특정 지점에서 일시정지 등을 할 수 있습니다.
    }
    Serial.print("전송한 chunkStartIndex 버퍼사이즈 : ");
    Serial.println(chunkStartIndex);

    digitalWrite(LED_PIN_RECORDING, LOW);   // LED ON
    digitalWrite(LED_PIN_SENDING, HIGH);
    Serial.flush();  
    delay(10);
    sendMsgToFlutter("END");    
    centerText("COMPLETED", 34);
    digitalWrite(LED_PIN_RECORDING, LOW);   // LED ON
    digitalWrite(LED_PIN_SENDING, LOW);  

    
    // 여기서 녹음 종료 후 원하는 동작을 추가하세요.
    delay(300);
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

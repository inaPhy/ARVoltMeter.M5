//#include <M5StickC.h>
#include <M5StickCPlus.h>
// 電圧計関係
#include <Wire.h>
#include "voltmeter.h"
// BLE関係
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

#define SERVICE_UUID        "FE57"
#define CHARACTERISTIC_UUID "2B18"
String rand_str = String(random(65535),HEX);
String server_name = "M5VoltMeter-" + rand_str;

/* ================ BLE通信処置 ================ */
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      M5.Lcd.setCursor(5, 35);
      M5.Lcd.setTextSize(2);
      M5.Lcd.setTextColor(YELLOW, ORANGE);
      M5.Lcd.println("Connect!!");
      deviceConnected = true;
    };
    void onDisconnect(BLEServer* pServer) {
      M5.Lcd.setCursor(5, 35);
      M5.Lcd.setTextSize(2);
      M5.Lcd.setTextColor(WHITE, BLACK);
      M5.Lcd.println("See you soon.");
      deviceConnected = false;
      delay(1000);
      esp_restart();
    }
};

//Unityから送受信
class MyCallbacks: public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic *pCharacteristic) {
    std::string value = pCharacteristic->getValue();
//    M5.Lcd.setCursor(5, 120);
//    M5.Lcd.setTextSize(1);
//    M5.Lcd.printf("read");
//    M5.Lcd.printf(value.c_str());
  }
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string value = pCharacteristic->getValue();
//    M5.Lcd.setCursor(5, 120);
//    M5.Lcd.setTextSize(1);
//    M5.Lcd.println("written");
//    M5.Lcd.println(value.c_str());
  }
};

/* ================ 変数 ================ */
//バッテリー残量表示用
double vbat = 0.0;
int8_t bat_charge_p = 0;
/* ================ 電圧計初期値 ================ */
Voltmeter voltmeter;
int16_t volt_raw_list[10];
uint8_t raw_now_ptr = 0;
int16_t adc_raw = 0;
int32_t Volt_data = 0;
int32_t send_data = 0;

/* ================ Arduinoセットアップ ================ */
void setup(void) {
  M5.begin();
  Wire.begin();
  // 画面設定　１行目
  M5.Lcd.setRotation(1);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(5, 5);
  M5.Lcd.setTextSize(2);
  M5.Lcd.println(server_name);
  // ２行目
  M5.Lcd.setCursor(5, 35);
  M5.Lcd.setTextSize(2);
  M5.Lcd.println("Find me!!");

  //電圧計設定
  voltmeter.setMode(SINGLESHOT);
  voltmeter.setRate(RATE_128);
  voltmeter.setGain(PAG_2048);
//   | PAG      | Max Input Voltage(V) |
//   | PAG_6144 |        128           |
//   | PAG_4096 |        64            |
//   | PAG_2048 |        32            |
//   | PAG_512  |        16            |
//   | PAG_256  |        8             |

  //BLE設定
  setupBle();
}

/* ================ Arduino繰り返し ================ */
void loop(void) {
  /* ==電圧取得処置：10回値を取得したものの平均を出力する== */
  //電圧計，電圧取得
  voltmeter.getVoltage();
  //取得した電圧を配列に挿入
  volt_raw_list[raw_now_ptr] = voltmeter.adc_raw;
  raw_now_ptr = (raw_now_ptr == 9) ? 0 : (raw_now_ptr + 1);
  //変数初期化
  int count = 0;
  int total = 0;
  //10個のデータを足す
  for (uint8_t i = 0; i < 10; i++) {
    if (volt_raw_list[i] == 0) {
      continue ;
    }
    total += volt_raw_list[i];
    count += 1;
  }
  //平均を出す
  if (count == 0) {
    adc_raw = 0;
  } else {
    adc_raw = total / count;
  }
  //電圧値（単位はmV）
  Volt_data = -1 * adc_raw * voltmeter.resolution * voltmeter.calibration_factor;
  //
  send_data = Volt_data;
  //測定値BLE送信
  if(deviceConnected){
    pCharacteristic->setValue(send_data);
    pCharacteristic->notify();
  }

  //画面出力
  M5.Lcd.setTextSize(4);
  M5.Lcd.setCursor(10, 65);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.printf("%.2f ", -1 * adc_raw * voltmeter.resolution * voltmeter.calibration_factor/1000);
  M5.Lcd.printf("V   ");
  
  //Aボタン押した
  if(M5.BtnA.wasPressed()) {

  }

  // Bボタン押した（リセット）
  if(M5.BtnB.wasPressed()){
    esp_restart();
  }

  // バッテリー残量表示（簡易的に、線形で4.11Vで100%、3.0Vで0%とする）
  vbat = M5.Axp.GetVbatData() * 1.1 / 1000;
  bat_charge_p = int8_t((vbat - 3.0) / 1.11 * 100);
  if(bat_charge_p > 100){
    bat_charge_p = 100;
  }else if(bat_charge_p < 0){
    bat_charge_p = 0;
  }
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(180, 115);
  M5.Lcd.printf("%3d%%", bat_charge_p);
  // 更新
  M5.update();
}

/* ================ BLEセットアップ ================ */
void setupBle() {
  BLEDevice::init(server_name.c_str());
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_READ |
                                         BLECharacteristic::PROPERTY_WRITE |
                                         BLECharacteristic::PROPERTY_WRITE_NR |
                                         BLECharacteristic::PROPERTY_NOTIFY |
                                         BLECharacteristic::PROPERTY_INDICATE
                                       );
  pCharacteristic->setCallbacks(new MyCallbacks());
  pCharacteristic->addDescriptor(new BLE2902());

  pService->start();
  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->start();  
}

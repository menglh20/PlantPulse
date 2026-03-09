#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// -------------------- UUIDs (use yours) --------------------
static BLEUUID serviceUUID("c29fac7c-fee2-4ee8-9013-b91e598e570d");
static BLEUUID    charUUID("a25db1e7-7caa-47fd-ad5a-2ca734f3c365");

// -------------------- Sensors --------------------
static const uint8_t SHT31_ADDR = 0x44;
Adafruit_SHT31 sht31 = Adafruit_SHT31();
static const int SEN0193_PIN = A0;

// soil calibration (tune to your device)
static const int dryRaw = 3400;
static const int wetRaw = 1800;

// -------------------- Send policy --------------------
static const int SOIL_CHANGE_THRESHOLD_PCT = 10;     // "明显变化"
static const uint32_t READ_PERIOD_MS = 1000;         // read every 1s
static const uint32_t MIN_NOTIFY_GAP_MS = 1500;      // avoid spam on jitter
static const uint32_t PERIODIC_SEND_MS = 60000;      // no change -> send every 60s

// -------------------- BLE globals --------------------
BLEServer* pServer = nullptr;
BLECharacteristic* pChar = nullptr;
bool deviceConnected = false;
bool oldDeviceConnected = false;

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { deviceConnected = true; }
  void onDisconnect(BLEServer*) override { deviceConnected = false; }
};

// -------------------- Simple DSP for soil --------------------
static int median3(int a, int b, int c) {
  if (a > b) { int t=a; a=b; b=t; }
  if (b > c) { int t=b; b=c; c=t; }
  if (a > b) { int t=a; a=b; b=t; }
  return b;
}

static int readSoilAvgN(int n, int delayMs) {
  long sum = 0;
  for (int i = 0; i < n; i++) {
    sum += analogRead(SEN0193_PIN);
    delay(delayMs);
  }
  return (int)(sum / n);
}

static int readSoilRawRobust() {
  int x1 = readSoilAvgN(8, 2);
  int x2 = readSoilAvgN(8, 2);
  int x3 = readSoilAvgN(8, 2);
  return median3(x1, x2, x3);
}

static int soilRawToPercent(int raw) {
  int pct = map(raw, dryRaw, wetRaw, 0, 100);
  return constrain(pct, 0, 100);
}

// -------------------- BLE init --------------------
void bleInitServer() {
  BLEDevice::init("SensingDevice_Server");

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* svc = pServer->createService(serviceUUID);
  pChar = svc->createCharacteristic(
    charUUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pChar->addDescriptor(new BLE2902());

  pChar->setValue("boot");
  svc->start();

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(serviceUUID);
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);
  adv->setMinPreferred(0x12);

  BLEDevice::startAdvertising();
  Serial.println("[BLE] Advertising started.");
}

// -------------------- Build + send payload --------------------
void sendNow(float t, float h, int soilPct, const char* reason) {
  char payload[80];
  float tSend = isnan(t) ? -999.0f : t;
  float hSend = isnan(h) ? -999.0f : h;
  snprintf(payload, sizeof(payload), "t=%.2f,h=%.2f,soil=%d", tSend, hSend, soilPct);

  pChar->setValue((uint8_t*)payload, strlen(payload));

  Serial.print("[SEND] ");
  Serial.print(reason);
  Serial.print(" -> ");
  Serial.println(payload);

  if (deviceConnected) {
    pChar->notify();
  }
}

// -------------------- State --------------------
static uint32_t nextReadMs = 0;
static uint32_t lastNotifyMs = 0;
static uint32_t lastPeriodicSendMs = 0;

static bool hasBaseline = false;
static int lastSentSoilPct = -1;

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin();

  if (!sht31.begin(SHT31_ADDR)) {
    Serial.println("ERROR: SHT31 not found. Continue without T/H (will send -999).");
  }

  analogReadResolution(12);
  pinMode(SEN0193_PIN, INPUT);

  bleInitServer();

  // baseline
  int raw = readSoilRawRobust();
  int pct = soilRawToPercent(raw);
  lastSentSoilPct = pct;
  hasBaseline = true;

  float t0 = sht31.readTemperature();
  float h0 = sht31.readHumidity();

  sendNow(t0, h0, pct, "boot");
  uint32_t now = millis();
  lastNotifyMs = now;
  lastPeriodicSendMs = now;

  nextReadMs = now + READ_PERIOD_MS;
}

void loop() {
  // re-advertise on disconnect
  if (!deviceConnected && oldDeviceConnected) {
    delay(200);
    pServer->startAdvertising();
    Serial.println("[BLE] Disconnected -> advertising again");
    oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
    Serial.println("[BLE] Client connected");
    oldDeviceConnected = deviceConnected;
  }

  uint32_t now = millis();

  // 1) periodic sensor read
  if ((int32_t)(now - nextReadMs) >= 0) {
    nextReadMs += READ_PERIOD_MS;

    float t = sht31.readTemperature();
    float h = sht31.readHumidity();

    int raw = readSoilRawRobust();
    int soilPct = soilRawToPercent(raw);

    Serial.printf("[Read] T=%.2f H=%.2f soil=%d%% (raw=%d)\n", t, h, soilPct, raw);

    if (!hasBaseline) {
      lastSentSoilPct = soilPct;
      hasBaseline = true;
      Serial.printf("[Baseline] soil=%d%%\n", lastSentSoilPct);
      // baseline建立后也可以选择立刻发一次：
      // sendNow(t, h, soilPct, "baseline");
      // lastNotifyMs = now;
      // lastPeriodicSendMs = now;
      return;
    }

    // 2) change-triggered send
    int diff = abs(soilPct - lastSentSoilPct);
    if (diff >= SOIL_CHANGE_THRESHOLD_PCT) { // 如果你要严格">10%"，改成 diff > 10
      if (now - lastNotifyMs >= MIN_NOTIFY_GAP_MS) {
        sendNow(t, h, soilPct, "soil_change");
        lastSentSoilPct = soilPct;
        lastNotifyMs = now;
        lastPeriodicSendMs = now; // 变化触发后，周期计时重置
      }
    }
    // 3) else do nothing here; periodic send handled below
  }

  // 4) periodic send every 60s if no major change triggered it
  if (now - lastPeriodicSendMs >= PERIODIC_SEND_MS) {
    float t = sht31.readTemperature();
    float h = sht31.readHumidity();
    int raw = readSoilRawRobust();
    int soilPct = soilRawToPercent(raw);

    // 这次是“保底发送”，不更新 lastSentSoilPct（可选）
    // 如果你希望“60秒发送也算一次lastSent”，把下面注释取消
    // lastSentSoilPct = soilPct;

    sendNow(t, h, soilPct, "periodic_60s");
    lastPeriodicSendMs = now;
    lastNotifyMs = now; // 避免刚发完周期包，紧接着因抖动又触发变化包
  }

  delay(5);
}
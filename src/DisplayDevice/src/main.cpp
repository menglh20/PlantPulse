#include <Arduino.h>
#include <Wire.h>

#include <Adafruit_GFX.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_SSD1306.h>

#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>

// ===================== UUID (use yours) =====================
static BLEUUID serviceUUID("c29fac7c-fee2-4ee8-9013-b91e598e570d");
static BLEUUID charUUID("a25db1e7-7caa-47fd-ad5a-2ca734f3c365");

// ===================== Pins =====================
static const int BTN_PIN = D7;  // button to GND
static const int LED_PIN = D9;  // WS2812B
static const int LED_COUNT = 1;

// X27 coil pairs confirmed
static const int A_PLUS = A0;
static const int A_MINUS = A1;
static const int B_PLUS = A2;
static const int B_MINUS = A3;

// ===================== OLED =====================
static const int SCREEN_W = 128;
static const int SCREEN_H = 64;
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);

// ===================== NeoPixel =====================
Adafruit_NeoPixel pixels(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
enum LedColor { LED_RED,
                LED_GREEN,
                LED_BLUE,
                LED_OFF };

void ledSetColor(LedColor c, uint8_t brightness = 30) {
    pixels.setBrightness(brightness);
    uint32_t col = 0;
    switch (c) {
        case LED_RED:
            col = pixels.Color(255, 0, 0);
            break;
        case LED_GREEN:
            col = pixels.Color(0, 255, 0);
            break;
        case LED_BLUE:
            col = pixels.Color(0, 0, 255);
            break;
        case LED_OFF:
        default:
            col = pixels.Color(0, 0, 0);
            break;
    }
    pixels.setPixelColor(0, col);
    pixels.show();
}

// soil thresholds
static const int SOIL_DRY_PCT = 10;
static const int SOIL_WET_PCT = 80;

void updateLedBySoil(int soilPct) {
    if (soilPct < 0) {
        ledSetColor(LED_OFF, 5);
        return;
    }
    if (soilPct <= SOIL_DRY_PCT)
        ledSetColor(LED_RED, 30);
    else if (soilPct >= SOIL_WET_PCT)
        ledSetColor(LED_BLUE, 30);
    else
        ledSetColor(LED_GREEN, 30);
}

// ===================== OLED UI =====================
void oledShowCenteredBig(const String& text, uint8_t textSize = 2) {
    display.clearDisplay();
    display.setTextSize(textSize);
    display.setTextColor(SSD1306_WHITE);

    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    int16_t x = (SCREEN_W - (int16_t)w) / 2;
    int16_t y = (SCREEN_H - (int16_t)h) / 2;

    display.setCursor(max<int16_t>(0, x), max<int16_t>(0, y));
    display.print(text);
    display.display();
}

enum ScreenMode { SHOW_TEMP = 0,
                  SHOW_HUM = 1,
                  SHOW_SOIL = 2 };
static ScreenMode screenMode = SHOW_TEMP;

// Latest values from BLE
static float gTempC = NAN;
static float gHumPct = NAN;
static int gSoilPct = -1;

void showCurrentScreen() {
    if (screenMode == SHOW_TEMP) {
        if (isnan(gTempC))
            oledShowCenteredBig("Wait For Sensor Data", 2);
        else
            oledShowCenteredBig(String(gTempC, 1) + "C", 2);
    } else if (screenMode == SHOW_HUM) {
        if (isnan(gHumPct))
            oledShowCenteredBig("Wait For Sensor Data", 2);
        else
            oledShowCenteredBig("Room:" + String((int)roundf(gHumPct)) + "%", 2);
    } else {
        if (gSoilPct < 0)
            oledShowCenteredBig("Wait For Sensor Data", 2);
        else
            oledShowCenteredBig("Soil:" + String(gSoilPct) + "%", 2);
    }
}

// ===================== Button short/long press (resettable) =====================
static const uint32_t DEBOUNCE_MS = 30;
static const uint32_t LONG_PRESS_MS = 800;
static const uint32_t CLICK_COOLDOWN_MS = 250;

struct ButtonEvent {
    bool click = false;
    bool longPress = false;
};

// internal state (was static inside function)
static uint8_t btn_lastRead = HIGH;
static uint8_t btn_stable = HIGH;
static uint32_t btn_lastChange = 0;

static bool btn_pressing = false;
static uint32_t btn_pressStart = 0;
static bool btn_longFired = false;
static uint32_t btn_lastClick = 0;

static bool btn_needRelease = false;  // <--- key: after longPress, ignore until released

void resetButtonState() {
    btn_lastRead = HIGH;
    btn_stable = HIGH;
    btn_lastChange = millis();

    btn_pressing = false;
    btn_pressStart = 0;
    btn_longFired = false;
    btn_lastClick = millis();

    btn_needRelease = false;
}

ButtonEvent pollButton() {
    ButtonEvent ev;

    uint8_t r = digitalRead(BTN_PIN);

    // after a long press action, require full release before accepting any click
    if (btn_needRelease) {
        if (r == HIGH) {
            btn_needRelease = false;
            // reset baseline to avoid false edge
            btn_lastRead = HIGH;
            btn_stable = HIGH;
            btn_lastChange = millis();
        }
        return ev;
    }

    if (r != btn_lastRead) {
        btn_lastRead = r;
        btn_lastChange = millis();
    }

    if (millis() - btn_lastChange > DEBOUNCE_MS) {
        if (btn_stable != btn_lastRead) {
            btn_stable = btn_lastRead;

            if (btn_stable == LOW) {  // pressed
                btn_pressing = true;
                btn_pressStart = millis();
                btn_longFired = false;
            } else {  // released
                if (btn_pressing && !btn_longFired) {
                    if (millis() - btn_lastClick > CLICK_COOLDOWN_MS) {
                        ev.click = true;
                        btn_lastClick = millis();
                    }
                }
                btn_pressing = false;
            }
        }
    }

    if (btn_pressing && !btn_longFired && (millis() - btn_pressStart >= LONG_PRESS_MS)) {
        ev.longPress = true;
        btn_longFired = true;
        btn_needRelease = true;  // <--- key
    }

    return ev;
}

// ===================== X27.168 driver =====================
static const uint8_t PHASE[4][4] = {
    {1, 0, 1, 0},
    {0, 1, 1, 0},
    {0, 1, 0, 1},
    {1, 0, 0, 1}};

static int phaseIdx = 0;
static long currentSteps = 0;

static const float MAX_DEG = 180.0f;
static const int STEPS_PER_315_DEG = 600;
static const float STEPS_PER_DEG = (float)STEPS_PER_315_DEG / 315.0f;

static const uint16_t STEP_DELAY_US = 15000;
static const int MOTOR_DIR_SIGN = +1;

static inline void applyPhase(int idx) {
    idx = (idx % 4 + 4) % 4;
    digitalWrite(A_PLUS, PHASE[idx][0] ? HIGH : LOW);
    digitalWrite(A_MINUS, PHASE[idx][1] ? HIGH : LOW);
    digitalWrite(B_PLUS, PHASE[idx][2] ? HIGH : LOW);
    digitalWrite(B_MINUS, PHASE[idx][3] ? HIGH : LOW);
}

void x27Hold() {
    applyPhase(phaseIdx);
}

void x27Release() {
    digitalWrite(A_PLUS, LOW);
    digitalWrite(A_MINUS, LOW);
    digitalWrite(B_PLUS, LOW);
    digitalWrite(B_MINUS, LOW);
}

static inline void stepOnce(int dir) {
    phaseIdx += dir * MOTOR_DIR_SIGN;
    applyPhase(phaseIdx);
    delayMicroseconds(STEP_DELAY_US);
}

void x27MoveSteps(long steps) {
    int dir = (steps >= 0) ? +1 : -1;
    long n = labs(steps);
    for (long i = 0; i < n; i++)
        stepOnce(dir);
    currentSteps += steps;
    Serial.printf("[X27] Moved %ld steps to %.1f deg\n", steps, currentSteps / STEPS_PER_DEG);
}

// HOME: drive motor all the way to zero position on startup
void x27Home() {
    // Drive backward more than full range to hit the physical stop
    int homeSteps = STEPS_PER_315_DEG + 50;
    for (int i = 0; i < homeSteps; i++) {
        phaseIdx -= MOTOR_DIR_SIGN;
        applyPhase(phaseIdx);
        delayMicroseconds(STEP_DELAY_US + 3000);  // slower for homing
    }
    currentSteps = 0;  // we are now at zero
    phaseIdx = 0;
    x27Hold();
    delay(200);
}

void x27SetAngle(float deg) {
    deg = constrain(deg, 0.0f, MAX_DEG);
    Serial.printf("[X27] Move to %.1f deg (current %.1f deg)\n", deg, currentSteps / STEPS_PER_DEG);
    long target = lroundf(deg * STEPS_PER_DEG);
    long delta = target - currentSteps;
    x27MoveSteps(delta);
}

void x27ShowHumidity(float soilPct) {
    if (isnan(soilPct) || soilPct < 0)
        return;
    float deg = constrain(soilPct, 0.0f, 100.0f) * MAX_DEG / 100.0f;
    x27SetAngle(deg);
}

// ===================== BLE Client state (template style) =====================
static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;

static BLERemoteCharacteristic* pRemoteCharacteristic = nullptr;
static BLEAdvertisedDevice* myDevice = nullptr;
static BLEClient* pClient = nullptr;

// standby mode
static bool standby = false;

// Payload parsing: "t=23.45,h=51.20,soil=37"
bool parseTHS(const uint8_t* pData, size_t length, float& t, float& h, int& soil) {
    char buf[96];
    size_t n = (length < sizeof(buf) - 1) ? length : (sizeof(buf) - 1);
    memcpy(buf, pData, n);
    buf[n] = '\0';

    String s(buf);
    s.trim();

    int it = s.indexOf("t=");
    int ih = s.indexOf("h=");
    int is = s.indexOf("soil=");
    if (it < 0 || ih < 0 || is < 0)
        return false;

    auto readFloatAfter = [&](int idx) -> float {
        int comma = s.indexOf(',', idx);
        String sub = (comma >= 0) ? s.substring(idx, comma) : s.substring(idx);
        sub.trim();
        return sub.toFloat();
    };
    auto readIntAfter = [&](int idx) -> int {
        int comma = s.indexOf(',', idx);
        String sub = (comma >= 0) ? s.substring(idx, comma) : s.substring(idx);
        sub.trim();
        return sub.toInt();
    };

    t = readFloatAfter(it + 2);
    h = readFloatAfter(ih + 2);
    soil = readIntAfter(is + 5);
    return true;
}

static void notifyCallback(
    BLERemoteCharacteristic*,
    uint8_t* pData,
    size_t length,
    bool) {
    float t, h;
    int soil;
    if (!parseTHS(pData, length, t, h, soil)) {
        Serial.print("[BLE] Unrecognized payload: ");
        Serial.write(pData, length);
        Serial.println();
        return;
    }

    // -999 used as invalid on sender side
    if (t > -200)
        gTempC = t;
    if (h > -1)
        gHumPct = h;
    gSoilPct = soil;

    Serial.printf("[BLE] RX: T=%.2f H=%.2f Soil=%d\n", gTempC, gHumPct, gSoilPct);

    if (!standby) {
        updateLedBySoil(gSoilPct);
        showCurrentScreen();
        x27ShowHumidity(gSoilPct);
        delay(1000);  // keep motor movement smooth if updates come too fast
    }
}

class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient*) override {}
    void onDisconnect(BLEClient*) override {
        connected = false;
        Serial.println("[BLE] onDisconnect");
    }
};

bool connectToServer() {
    Serial.print("[BLE] Forming a connection to ");
    Serial.println(myDevice->getAddress().toString().c_str());

    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());
    Serial.println("[BLE] - Created client");

    if (!pClient->connect(myDevice)) {
        Serial.println("[BLE] - Connect failed");
        return false;
    }
    Serial.println("[BLE] - Connected to server");
    pClient->setMTU(185);

    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
        Serial.print("[BLE] Failed to find service UUID: ");
        Serial.println(serviceUUID.toString().c_str());
        pClient->disconnect();
        return false;
    }
    Serial.println("[BLE] - Found service");

    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr) {
        Serial.print("[BLE] Failed to find characteristic UUID: ");
        Serial.println(charUUID.toString().c_str());
        pClient->disconnect();
        return false;
    }
    Serial.println("[BLE] - Found characteristic");

    if (pRemoteCharacteristic->canRead()) {
        std::string value = pRemoteCharacteristic->readValue();
        Serial.print("[BLE] Char value: ");
        Serial.println(value.c_str());
    }

    if (pRemoteCharacteristic->canNotify()) {
        pRemoteCharacteristic->registerForNotify(notifyCallback);
        Serial.println("[BLE] - Registered for notify");
    } else {
        Serial.println("[BLE] - Char cannot notify (will rely on reads only)");
    }

    connected = true;
    return true;
}

// Scan for servers advertising our service UUID
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        // Uncomment if you want verbose scan logs
        // Serial.println(advertisedDevice.toString().c_str());

        if (advertisedDevice.haveServiceUUID() &&
            advertisedDevice.isAdvertisingService(serviceUUID)) {
            Serial.println("[BLE] Found sensing server advertising target service.");
            BLEDevice::getScan()->stop();

            myDevice = new BLEAdvertisedDevice(advertisedDevice);
            doConnect = true;
            doScan = true;
        }
    }
};

void startScanSlice(uint32_t seconds = 1) {
    BLEDevice::getScan()->start(seconds, false);
}

// standby enter/exit
void enterStandby() {
    standby = true;
    ledSetColor(LED_OFF, 1);
    display.clearDisplay();
    display.display();

    if (pClient && pClient->isConnected())
        pClient->disconnect();
    connected = false;

    resetButtonState();
    Serial.println("[UI] Standby ON");
}

void exitStandby() {
    standby = false;
    resetButtonState();
    // reset global data
    gTempC = NAN;
    gHumPct = NAN;
    gSoilPct = -1;
    Serial.println("[UI] Standby OFF");

    // restore UI with latest data
    updateLedBySoil(gSoilPct);
    showCurrentScreen();
    x27ShowHumidity(gSoilPct);
    delay(1000);  // keep motor movement smooth if updates come too fast

    // restart scanning
    doConnect = false;
    connected = false;
    if (myDevice) {
        delete myDevice;
        myDevice = nullptr;
    }
    startScanSlice(1);
}

// ===================== setup / loop =====================
void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(BTN_PIN, INPUT_PULLUP);

    // motor pins
    pinMode(A_PLUS, OUTPUT);
    pinMode(A_MINUS, OUTPUT);
    pinMode(B_PLUS, OUTPUT);
    pinMode(B_MINUS, OUTPUT);

    // neopixel
    pixels.begin();
    ledSetColor(LED_GREEN, 5);

    // oled
    Wire.begin();
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("SSD1306 init failed. Try 0x3D.");
    }
    oledShowCenteredBig("SCAN", 2);

    // BLE init
    BLEDevice::init("DisplayDevice_Client");

    BLEScan* scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    scan->setInterval(1349);
    scan->setWindow(449);
    scan->setActiveScan(true);

    startScanSlice(1);
}

void loop() {
    // Button events
    ButtonEvent ev = pollButton();
    if (ev.longPress) {
        if (!standby)
            enterStandby();
        else
            exitStandby();
    }
    if (!standby && ev.click) {
        screenMode = (ScreenMode)(((int)screenMode + 1) % 3);
        showCurrentScreen();
    }

    if (!standby) {
        // Connect if scanned and flagged
        if (doConnect == true) {
            if (connectToServer()) {
                Serial.println("[BLE] We are now connected to the BLE Server.");
                delay(150);
                showCurrentScreen();
            } else {
                Serial.println("[BLE] Failed to connect; will rescan.");
                delay(200);
                startScanSlice(1);
            }
            doConnect = false;
        }

        // Rescan if disconnected
        static uint32_t lastScanMs = 0;
        if (!connected && !doConnect) {
            if (millis() - lastScanMs > 10000) {
                lastScanMs = millis();
                startScanSlice(1);
            }
        }
    }

    delay(10);
}
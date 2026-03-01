#include <Arduino.h>
#include <Wire.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>

// ===================== Pins =====================
// X27.168 coils (your working order2)
static const int COIL_PINS[4] = {A0, A2, A1, A3};

static const int BTN_PIN = D7;     // button to GND
static const int LED_PIN = D9;     // WS2812B
static const int LED_COUNT = 1;

// ===================== OLED =====================
static const int SCREEN_W = 128;
static const int SCREEN_H = 64;
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);

// ===================== NeoPixel =====================
Adafruit_NeoPixel pixels(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// 新增：带蓝色的枚举
enum LedColor { LED_RED, LED_GREEN, LED_YELLOW, LED_BLUE, LED_OFF };

void ledSetColor(LedColor c, uint8_t brightness = 30) {
  pixels.setBrightness(brightness);
  uint32_t col = 0;
  switch (c) {
    case LED_RED:    col = pixels.Color(255, 0, 0); break;
    case LED_GREEN:  col = pixels.Color(0, 255, 0); break;
    case LED_BLUE:   col = pixels.Color(0, 0, 255); break;
    case LED_YELLOW: col = pixels.Color(255, 255, 0); break;
    case LED_OFF:
    default:         col = pixels.Color(0, 0, 0); break;
  }
  pixels.setPixelColor(0, col);
  pixels.show();
}

void updateLedByAngle(int deg, uint8_t brightness = 30) {
  deg = constrain(deg, 0, 180);
  int percentage = (int)roundf((float)deg / 180.0f * 100.0f);
  if (percentage <= 10) {
    ledSetColor(LED_RED, brightness);
  } else if (percentage <= 30) {
    ledSetColor(LED_YELLOW, brightness);
  } else if (percentage >= 80) {
    ledSetColor(LED_BLUE, brightness);
  } else {
    ledSetColor(LED_GREEN, brightness);
  }
}

void oledShowCenteredBig(const String& text, uint8_t textSize = 3) {
  display.clearDisplay();
  display.setTextSize(textSize);
  display.setTextColor(SSD1306_WHITE);

  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int16_t x = (SCREEN_W - (int16_t)w) / 2;
  int16_t y = (SCREEN_H - (int16_t)h) / 2;

  display.setCursor(max<int16_t>(0, x), max<int16_t>(0, y));
  display.print(text);
  display.display();
}

void showDegreeOnOled(int deg) {
  float percentage = (float)deg / 180.0f * 100.0f;
  oledShowCenteredBig(String((int)percentage) + "%", 3);
}

// ===================== Button (debounced press edge) =====================
bool buttonClick() {
  static uint8_t last = HIGH;
  static uint32_t lastClickMs = 0;
  const uint32_t COOLDOWN_MS = 300;   // 关键：防止按太快/抖动多次触发

  uint8_t now = digitalRead(BTN_PIN);

  // 检测到一次“按下沿”
  if (last == HIGH && now == LOW) {
    // 冷却期内忽略
    if (millis() - lastClickMs >= COOLDOWN_MS) {
      lastClickMs = millis();
      last = now;
      return true;
    }
  }

  last = now;
  return false;
}

// ===================== X27.168 driver =====================
// ===================== X27.168 (use coil pairs) =====================
// You confirmed: (A0,A1) is one coil, (A2,A3) is another coil.
static const int A_PLUS  = A0;
static const int A_MINUS = A1;
static const int B_PLUS  = A2;
static const int B_MINUS = A3;

// Full-step, 2-phase ON (more torque, less likely to slip)
// Each row: {A_PLUS, A_MINUS, B_PLUS, B_MINUS}
static const uint8_t PHASE[4][4] = {
  {1,0, 1,0},  // A+ B+
  {0,1, 1,0},  // A- B+
  {0,1, 0,1},  // A- B-
  {1,0, 0,1}   // A+ B-
};

static int phaseIdx = 0;
static long currentSteps = 0;

// Calibration (adjust if needed)
static const float MAX_DEG = 180.0f;
static const int   STEPS_PER_315_DEG = 600;
static const float STEPS_PER_DEG = (float)STEPS_PER_315_DEG / 315.0f;

// Slow down for stability
static const uint16_t STEP_DELAY_US = 9000;

// If direction is reversed, set this to -1.
static const int MOTOR_DIR_SIGN = +1;

static inline void applyPhase(int idx) {
  idx = (idx % 4 + 4) % 4;
  digitalWrite(A_PLUS,  PHASE[idx][0] ? HIGH : LOW);
  digitalWrite(A_MINUS, PHASE[idx][1] ? HIGH : LOW);
  digitalWrite(B_PLUS,  PHASE[idx][2] ? HIGH : LOW);
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
  // dir: +1 or -1
  phaseIdx += dir * MOTOR_DIR_SIGN;
  applyPhase(phaseIdx);
  delayMicroseconds(STEP_DELAY_US);
}

void x27MoveSteps(long steps) {
  int dir = (steps >= 0) ? +1 : -1;
  long n = labs(steps);
  for (long i = 0; i < n; i++) stepOnce(dir);
  currentSteps += steps;
}

void x27SetAngle(float deg) {
  deg = constrain(deg, 0.0f, MAX_DEG);

  // Pre-lock to avoid ambiguous start
  x27Hold();
  delay(20);

  long target = lroundf(deg * STEPS_PER_DEG);
  long delta  = target - currentSteps;

  x27MoveSteps(delta);
}

// ===================== Indicator logic =====================
static int currentDeg = 0;          // 0..180
static int dir = +1;                // +1 means going forward, -1 means going back


void stepIndicatorBy30() {
  // If we are at an end, flip direction for the *next* movement
  if (currentDeg >= 180) dir = -1;
  if (currentDeg <= 0)   dir = +1;

  int nextDeg = currentDeg + dir * 30;

  // Clamp and also flip direction if clamp hits ends
  if (nextDeg >= 180) { nextDeg = 180; dir = -1; }
  if (nextDeg <= 0)   { nextDeg = 0;   dir = +1; }

  currentDeg = nextDeg;

  x27SetAngle((float)currentDeg);
  x27Hold();

  showDegreeOnOled(currentDeg);
  // updateLedByAngle(currentDeg, 30);

  Serial.printf("Deg=%d dir=%d steps=%ld\n", currentDeg, dir, currentSteps);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // Motor pins
  for (int i = 0; i < 4; i++) {
    pinMode(COIL_PINS[i], OUTPUT);
    digitalWrite(COIL_PINS[i], LOW);
  }

  // Button
  pinMode(BTN_PIN, INPUT_PULLUP);

  // NeoPixel
  pixels.begin();
  ledSetColor(LED_OFF, 30);

  // OLED
  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 init failed. Try 0x3D.");
  }

  // Define current position as 0°
  currentDeg = 0;
  currentSteps = 0;
  dir = +1;

  x27SetAngle(0);
  x27Hold();
  showDegreeOnOled(currentDeg);

  Serial.println("Ready: press button to move 30 degrees.");
}

void loop() {
  if (buttonClick()) {
    stepIndicatorBy30();
  }
  delay(5);
}
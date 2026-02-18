#include <Arduino.h>

// 固定你测出来能转的 order2: {P0, P2, P1, P3}
static const int COIL_PINS[4] = {A0, A2, A1, A3};

// 全步：单相通电（稳定）
static const uint8_t SEQ[4][4] = {
  {1,0,0,0},
  {0,1,0,0},
  {0,0,1,0},
  {0,0,0,1}
};

int stepIndex = 0;

void applyStep(int idx) {
  idx = (idx % 4 + 4) % 4;
  for (int i = 0; i < 4; i++) {
    digitalWrite(COIL_PINS[i], SEQ[idx][i] ? HIGH : LOW);
  }
}

void stepMotor(int dir, uint16_t stepDelayUs = 4000) {
  stepIndex += dir;
  applyStep(stepIndex);
  delayMicroseconds(stepDelayUs);
}

void moveSteps(int steps, uint16_t stepDelayUs = 4000) {
  int dir = (steps >= 0) ? +1 : -1;
  int n = abs(steps);
  for (int i = 0; i < n; i++) stepMotor(dir, stepDelayUs);
}

void releaseMotor() {
  for (int i = 0; i < 4; i++) digitalWrite(COIL_PINS[i], LOW);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  for (int i = 0; i < 4; i++) {
    pinMode(COIL_PINS[i], OUTPUT);
    digitalWrite(COIL_PINS[i], LOW);
  }

  Serial.println("X27.168 running...");
}

void loop() {
  // 你说“反向才成功”，所以这里把“正向”定义为 dir = -1
  // 想反过来就把这两个 moveSteps 的正负号互换即可。

  moveSteps(-300, 4000);  // “正向”（对你来说是原来的反向）
  releaseMotor();
  delay(500);

  moveSteps(+300, 4000);  // “反向”
  releaseMotor();
  delay(500);
}

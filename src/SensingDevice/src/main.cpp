// #include <Arduino.h>

// static const int SEN0193_PIN = A0;
// static const uint32_t READ_INTERVAL_MS = 1000;

// uint32_t lastReadMs = 0;

// int readSoilRawAvg(int samples = 10, int sampleDelayMs = 5) {
//   long sum = 0;
//   for (int i = 0; i < samples; i++) {
//     sum += analogRead(SEN0193_PIN);
//     delay(sampleDelayMs);
//   }
//   return (int)(sum / samples);
// }

// void setup() {
//   Serial.begin(115200);
//   delay(200);

//   analogReadResolution(12); // 0~4095

//   pinMode(SEN0193_PIN, INPUT);

//   Serial.println("SEN0193 periodic read start...");
// }

// void loop() {
//   uint32_t now = millis();

//   if (now - lastReadMs >= READ_INTERVAL_MS) {
//     lastReadMs = now;

//     int raw = readSoilRawAvg(10, 5);  // 10 次平均
//     int dryRaw = 3400;
//     int wetRaw = 1800;
//     int percent = map(raw, dryRaw, wetRaw, 0, 100);
//     percent = constrain(percent, 0, 100);

//     Serial.print("Raw ADC: ");
//     Serial.print(raw);
//     Serial.print("  |  Soil: ");
//     Serial.print(percent);
//     Serial.println("%");
//   }

//   delay(100);
// }
#include <Adafruit_SHT31.h>
#include <Arduino.h>
#include <Wire.h>

static const uint8_t SHT31_ADDR = 0x44;  // AD=GND -> 0x44; AD=VDD -> 0x45
static const uint32_t READ_INTERVAL_MS = 1000;

static const int I2C_SDA = -1;  // -1 represent for default
static const int I2C_SCL = -1;

Adafruit_SHT31 sht31 = Adafruit_SHT31();

uint32_t lastReadMs = 0;

void setup() {
    Serial.begin(115200);
    delay(200);

    // I2C init
    if (I2C_SDA >= 0 && I2C_SCL >= 0) {
        Wire.begin(I2C_SDA, I2C_SCL);
    } else {
        Wire.begin();
    }

    // initial
    if (!sht31.begin(SHT31_ADDR)) {
        Serial.println("ERROR: SHT31 not found on I2C. Check wiring/address!");
        while (true) {
            delay(1000);
        }
    }

    Serial.println("SHT31-D started.");
}

void loop() {
    uint32_t now = millis();
    if (now - lastReadMs >= READ_INTERVAL_MS) {
        lastReadMs = now;

        float t = sht31.readTemperature();
        float h = sht31.readHumidity();

        if (isnan(t) || isnan(h)) {
            Serial.println("Read failed (NaN). Check sensor/power/I2C pull-ups.");
            return;
        }

        Serial.print("Temp: ");
        Serial.print(t, 2);
        Serial.print(" C  |  Hum: ");
        Serial.print(h, 2);
        Serial.println(" %");
    }

    delay(100);
}
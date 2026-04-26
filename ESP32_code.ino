#include <Wire.h>
#include "BluetoothSerial.h"

BluetoothSerial SerialBT;

#define MPU_ADDR 0x68
#define ACCEL_XOUT_H 0x3B
#define LED_PIN 2
#define INT_PIN 19          // optional interrupt pin
#define CRASH_THRESHOLD 15000  // higher for real crash
#define SERIAL_COOLDOWN 500     // ms between Serial prints
#define ROLLING_SAMPLES 5       // smoothing
#define DURATION_THRESHOLD 50   // ms duration for valid crash

volatile bool crashDetected = false;
bool waitingForUserResponse = false;
unsigned long lastSerialTime = 0;
unsigned long crashStartTime = 0;

int16_t ax0 = 0, ay0 = 0, az0 = 0; // baseline
bool baselineSet = false;
bool wasConnected = false;

// Rolling averages
int16_t axBuffer[ROLLING_SAMPLES] = {0};
int16_t ayBuffer[ROLLING_SAMPLES] = {0};
int16_t azBuffer[ROLLING_SAMPLES] = {0};
int bufferIndex = 0;

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  pinMode(LED_PIN, OUTPUT);
  pinMode(INT_PIN, INPUT);

  // Initialize MPU6050
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); // wake up
  Wire.write(0);
  Wire.endTransmission(true);

  // Motion detection registers (optional)
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1F); // MOT_THR
  Wire.write(CRASH_THRESHOLD >> 8);
  Wire.endTransmission(true);

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x20); // MOT_DUR
  Wire.write(1);
  Wire.endTransmission(true);

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x38); // INT_ENABLE
  Wire.write(0x40); // enable motion interrupt
  Wire.endTransmission(true);

  attachInterrupt(digitalPinToInterrupt(INT_PIN), crashISR, RISING);

  // Start Bluetooth
  if (!SerialBT.begin("CrashGuard")) {
    Serial.println("Bluetooth start failed!");
    while (1);
  }
  Serial.println("Bluetooth started. Waiting for phone to connect...");
}

void loop() {
  // Detect Bluetooth connection
  if (SerialBT.hasClient() && !wasConnected) {
    Serial.println("✅ Phone connected!");
    wasConnected = true;
  }
  if (!SerialBT.hasClient() && wasConnected) {
    Serial.println("⚠️ Phone disconnected!");
    wasConnected = false;
  }

  // Only run detection if app is connected
  if (!SerialBT.hasClient()) {
    digitalWrite(LED_PIN, LOW);
    return;
  }

  // LED blink
  digitalWrite(LED_PIN, millis() % 1000 < 500 ? HIGH : LOW);

  // If waiting for user response, pause sensor detection
  if (waitingForUserResponse) {
    checkUserResponse();
    return;
  }

  // Read sensor
  int16_t ax, ay, az;
  readAccel(ax, ay, az);

  // Baseline calibration
  if (!baselineSet) {
    ax0 = ax; ay0 = ay; az0 = az;
    baselineSet = true;
    Serial.println("⚙️ Baseline calibrated.");
  }

  // Update rolling buffers
  axBuffer[bufferIndex] = ax - ax0;
  ayBuffer[bufferIndex] = ay - ay0;
  azBuffer[bufferIndex] = az - az0;
  bufferIndex = (bufferIndex + 1) % ROLLING_SAMPLES;

  // Compute rolling averages
  float dax = 0, day = 0, daz = 0;
  for (int i = 0; i < ROLLING_SAMPLES; i++) {
    dax += axBuffer[i];
    day += ayBuffer[i];
    daz += azBuffer[i];
  }
  dax /= ROLLING_SAMPLES;
  day /= ROLLING_SAMPLES;
  daz /= ROLLING_SAMPLES;

  float totalD = sqrt(dax*dax + day*day + daz*daz);

  // Serial debug periodically
  if (millis() - lastSerialTime > SERIAL_COOLDOWN) {
    Serial.print("AX: "); Serial.print(dax);
    Serial.print(" AY: "); Serial.print(day);
    Serial.print(" AZ: "); Serial.print(daz);
    Serial.print(" | Total: "); Serial.println(totalD);
    lastSerialTime = millis();
  }

  // Crash detection
  if (!crashDetected && totalD > CRASH_THRESHOLD) {
    if (crashStartTime == 0) crashStartTime = millis();
    else if (millis() - crashStartTime >= DURATION_THRESHOLD) {
      triggerCrash();
      crashStartTime = 0;
    }
  } else {
    crashStartTime = 0;
  }

  // Manual crash trigger via Serial Monitor
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.equalsIgnoreCase("TESTCRASH") && !crashDetected) {
      triggerCrash();
    }
  }

  checkUserResponse();
}

void crashISR() {
  if (!crashDetected && SerialBT.hasClient()) {
    triggerCrash();
  }
}

void triggerCrash() {
  crashDetected = true;
  waitingForUserResponse = true;
  Serial.println("🚨 CRASH detected! Waiting for user response...");
  SerialBT.println("crash");
  Serial.println("✅ Sent 'crash' to app");
}

void checkUserResponse() {
  if (SerialBT.available()) {
    String msg = SerialBT.readString();
    msg.trim();
    if (msg.equalsIgnoreCase("YES")) {
      crashDetected = false;
      waitingForUserResponse = false;
      baselineSet = false;
      Serial.println("👍 User is fine. Resuming detection...");
    } else if (msg.equalsIgnoreCase("NO")) {
      waitingForUserResponse = true;
      Serial.println("⚠️ Serious crash detected! Waiting for RESET...");
    } else if (msg.equalsIgnoreCase("RESET")) {
      crashDetected = false;
      waitingForUserResponse = false;
      baselineSet = false;
      Serial.println("🔄 Reset received. Resuming detection...");
    }
  }
}

void readAccel(int16_t &ax, int16_t &ay, int16_t &az) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(ACCEL_XOUT_H);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 6, true);

  ax = Wire.read() << 8 | Wire.read();
  ay = Wire.read() << 8 | Wire.read();
  az = Wire.read() << 8 | Wire.read();

  // Orientation fix for upside-down mounting
  az = -az;
}

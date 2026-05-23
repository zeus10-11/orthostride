const int motorPin = 4;

const int pwmFreq = 5000;
const int pwmResolution = 8;     // 8-bit: 0 to 255
const int dutyMin = 0;
const int dutyMax = 255;

const int rampStep = 2;          // Smaller step = smoother change
const int rampDelayMs = 25;      // Delay between steps
const int holdAtMaxMs = 800;
const int holdAtMinMs = 500;

void setup() {
  // ESP32 LEDC on selected pin.
  ledcAttach(motorPin, pwmFreq, pwmResolution);
  ledcWrite(motorPin, 0);
}

void rampDuty(int startDuty, int endDuty, int stepSize, int stepDelayMs) {
  if (startDuty < endDuty) {
    for (int d = startDuty; d <= endDuty; d += stepSize) {
      ledcWrite(motorPin, d);
      delay(stepDelayMs);
    }
  } else {
    for (int d = startDuty; d >= endDuty; d -= stepSize) {
      ledcWrite(motorPin, d);
      delay(stepDelayMs);
    }
  }

  // Ensure exact final value.
  ledcWrite(motorPin, endDuty);
}

void loop() {
  // Gradually increase vibration from OFF to HIGH.
  rampDuty(dutyMin, dutyMax, rampStep, rampDelayMs);
  delay(holdAtMaxMs);

  // Gradually decrease back to OFF.
  rampDuty(dutyMax, dutyMin, rampStep, rampDelayMs);
  delay(holdAtMinMs);
}

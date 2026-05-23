#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <math.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

Adafruit_MPU6050 mpu;

// ---------------------------
// Captive portal / hotspot
// ---------------------------
const char* AP_SSID = "Footwear-Tracker";
const char* AP_PASS = "12345678"; // Min 8 chars for WPA2
const byte DNS_PORT = 53;
DNSServer dnsServer;
WebServer server(80);
IPAddress apIP(192, 168, 4, 1);

bool alertActive = false;
unsigned long lastFallAlertMs = 0;

// ---------------------------
// User-tunable parameters
// ---------------------------
float stepThresholdHigh = 1.20f;   // m/s^2 dynamic acceleration threshold to start a step
float stepThresholdLow = 0.45f;    // m/s^2 dynamic acceleration threshold to finish a step
unsigned long stepDebounceMs = 250; // minimum time between steps

float freeFallThreshold = 3.0f;     // m/s^2 (~0.3g)
float impactThreshold = 24.0f;      // m/s^2 (~2.45g)
unsigned long impactWindowMs = 1200;
unsigned long stillnessAfterFallMs = 2000;

float strideLengthMeters = 0.70f;   // average stride estimate, tune for user
float userWeightKg = 65.0f;         // for calorie estimate

// ---------------------------
// Internal state
// ---------------------------
unsigned long lastLoopMs = 0;
unsigned long lastStepMs = 0;
unsigned long lastPrintMs = 0;

unsigned long stepTimestamps[20] = {0};
uint8_t stepTsIndex = 0;

bool stepActive = false;
float stepPeakDynamic = 0.0f;

float gravityMagLP = 9.81f;         // low-pass estimate of gravity magnitude
const float gravityAlpha = 0.92f;

// Fall detection state machine
bool inPotentialFreeFall = false;
bool waitingForImpact = false;
bool fallDetected = false;
unsigned long freeFallStartMs = 0;
unsigned long impactDetectedMs = 0;

// Metrics
unsigned long stepCount = 0;
unsigned long fallCount = 0;
float cadenceSpm = 0.0f;
float distanceMeters = 0.0f;
float caloriesKcal = 0.0f;
float maxImpact = 0.0f;

bool csvMode = false;

// Cached latest sensor values
float latestAccMag = 0.0f;
float latestDynAcc = 0.0f;
float latestGyroMag = 0.0f;
float latestTempC = 0.0f;

const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Footwear Tracker</title>
  <style>
    :root {
      --bg1: #e7f5ff;
      --bg2: #fff8e7;
      --card: rgba(255,255,255,0.86);
      --ink: #0b2239;
      --ok: #157f3d;
      --warn: #cc6f00;
      --alert: #b00020;
      --accent: #006d77;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: "Trebuchet MS", "Segoe UI", sans-serif;
      color: var(--ink);
      background: linear-gradient(135deg, var(--bg1), var(--bg2));
      min-height: 100vh;
    }
    .wrap {
      max-width: 980px;
      margin: 0 auto;
      padding: 18px;
    }
    .hero {
      padding: 16px;
      border-radius: 16px;
      background: var(--card);
      backdrop-filter: blur(8px);
      border: 1px solid rgba(255,255,255,0.8);
      box-shadow: 0 10px 30px rgba(0,0,0,0.06);
    }
    h1 { margin: 0 0 6px; font-size: 1.45rem; }
    .sub { margin: 0; color: #26435f; }
    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(170px, 1fr));
      gap: 12px;
      margin-top: 14px;
    }
    .card {
      background: var(--card);
      border: 1px solid rgba(255,255,255,0.9);
      border-radius: 12px;
      padding: 12px;
      box-shadow: 0 6px 18px rgba(0,0,0,0.05);
    }
    .label { font-size: 0.84rem; opacity: 0.8; }
    .val { font-size: 1.45rem; font-weight: 700; margin-top: 3px; }
    .bar {
      height: 10px;
      background: #d8e7ef;
      border-radius: 20px;
      overflow: hidden;
      margin-top: 7px;
    }
    .fill {
      height: 100%;
      width: 0%;
      background: linear-gradient(90deg, #57c785, #2a9d8f);
      transition: width 0.25s ease;
    }
    .status {
      margin-top: 12px;
      padding: 10px 12px;
      border-radius: 10px;
      font-weight: 700;
      background: #eaf8f0;
      color: var(--ok);
      border: 1px solid #d3f1df;
    }
    .status.warn { background: #fff4e6; color: var(--warn); border-color: #ffdcb2; }
    .status.alert {
      background: #ffe9ee;
      color: var(--alert);
      border-color: #ffc7d2;
      animation: pulse 1s infinite;
    }
    .row {
      margin-top: 12px;
      display: flex;
      gap: 10px;
      flex-wrap: wrap;
    }
    button {
      border: 0;
      padding: 10px 13px;
      border-radius: 10px;
      font-weight: 700;
      background: var(--accent);
      color: #fff;
      cursor: pointer;
    }
    button.secondary { background: #24557a; }
    .small { font-size: 0.83rem; opacity: 0.8; margin-top: 10px; }
    @keyframes pulse {
      0% { box-shadow: 0 0 0 0 rgba(176,0,32,0.35); }
      70% { box-shadow: 0 0 0 10px rgba(176,0,32,0); }
      100% { box-shadow: 0 0 0 0 rgba(176,0,32,0); }
    }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="hero">
      <h1>Smart Footwear Dashboard</h1>
      <p class="sub">Live step, gait, and fall monitoring over ESP32 hotspot</p>

      <div id="status" class="status">Normal activity</div>

      <div class="grid">
        <div class="card"><div class="label">Steps</div><div id="steps" class="val">0</div></div>
        <div class="card"><div class="label">Falls</div><div id="falls" class="val">0</div></div>
        <div class="card"><div class="label">Cadence (spm)</div><div id="cadence" class="val">0</div></div>
        <div class="card"><div class="label">Distance (m)</div><div id="distance" class="val">0</div></div>
        <div class="card"><div class="label">Calories (kcal)</div><div id="calories" class="val">0</div></div>
        <div class="card"><div class="label">Temperature (C)</div><div id="temp" class="val">0</div></div>
      </div>

      <div class="grid">
        <div class="card">
          <div class="label">Dynamic Acceleration</div>
          <div id="dynAcc" class="val">0</div>
          <div class="bar"><div id="dynFill" class="fill"></div></div>
        </div>
        <div class="card">
          <div class="label">Impact Magnitude</div>
          <div id="impact" class="val">0</div>
          <div class="bar"><div id="impactFill" class="fill"></div></div>
        </div>
      </div>

      <div class="row">
        <button onclick="ackAlert()">Acknowledge Alert</button>
        <button class="secondary" onclick="resetStats()">Reset Stats</button>
      </div>
      <div class="small">Connect to hotspot, then open any website. Captive portal should redirect here.</div>
    </div>
  </div>

  <script>
    let lastAlertState = false;

    function clamp(v, min, max) { return Math.max(min, Math.min(max, v)); }

    async function refresh() {
      try {
        const r = await fetch('/data', { cache: 'no-store' });
        const d = await r.json();

        document.getElementById('steps').textContent = d.steps;
        document.getElementById('falls').textContent = d.falls;
        document.getElementById('cadence').textContent = Number(d.cadence).toFixed(1);
        document.getElementById('distance').textContent = Number(d.distance).toFixed(2);
        document.getElementById('calories').textContent = Number(d.calories).toFixed(2);
        document.getElementById('temp').textContent = Number(d.temp).toFixed(1);
        document.getElementById('dynAcc').textContent = Number(d.dynAcc).toFixed(2);
        document.getElementById('impact').textContent = Number(d.accMag).toFixed(2);

        document.getElementById('dynFill').style.width = clamp((d.dynAcc / 5.0) * 100, 0, 100) + '%';
        document.getElementById('impactFill').style.width = clamp((d.accMag / 30.0) * 100, 0, 100) + '%';

        const status = document.getElementById('status');
        status.className = 'status';
        if (d.alert) {
          status.className = 'status alert';
          status.textContent = 'FALL ALERT: possible fall detected';
          if (!lastAlertState) {
            alert('FALL ALERT: Check the user immediately.');
          }
        } else if (d.dynAcc > 2.5) {
          status.className = 'status warn';
          status.textContent = 'High activity detected';
        } else {
          status.textContent = 'Normal activity';
        }
        lastAlertState = d.alert;
      } catch (e) {
        const status = document.getElementById('status');
        status.className = 'status warn';
        status.textContent = 'Waiting for device data...';
      }
    }

    async function ackAlert() {
      await fetch('/ack-alert', { method: 'POST' });
      lastAlertState = false;
      refresh();
    }

    async function resetStats() {
      await fetch('/reset', { method: 'POST' });
      refresh();
    }

    setInterval(refresh, 800);
    refresh();
  </script>
</body>
</html>
)rawliteral";

// ---------------------------
// Helpers
// ---------------------------
float magnitude3(float x, float y, float z) {
  return sqrtf(x * x + y * y + z * z);
}

void addStepTimestamp(unsigned long t) {
  stepTimestamps[stepTsIndex] = t;
  stepTsIndex = (stepTsIndex + 1) % 20;
}

void updateCadence(unsigned long nowMs) {
  const unsigned long windowMs = 10000; // 10-second rolling cadence
  int validCount = 0;

  for (uint8_t i = 0; i < 20; i++) {
    if (stepTimestamps[i] > 0 && (nowMs - stepTimestamps[i]) <= windowMs) {
      validCount++;
    }
  }

  cadenceSpm = validCount * (60000.0f / windowMs);
}

void resetStats() {
  stepCount = 0;
  fallCount = 0;
  cadenceSpm = 0.0f;
  distanceMeters = 0.0f;
  caloriesKcal = 0.0f;
  maxImpact = 0.0f;
  for (uint8_t i = 0; i < 20; i++) {
    stepTimestamps[i] = 0;
  }
  stepTsIndex = 0;
}

void sendJsonData() {
  String json = "{";
  json += "\"steps\":" + String(stepCount);
  json += ",\"falls\":" + String(fallCount);
  json += ",\"cadence\":" + String(cadenceSpm, 2);
  json += ",\"distance\":" + String(distanceMeters, 3);
  json += ",\"calories\":" + String(caloriesKcal, 3);
  json += ",\"accMag\":" + String(latestAccMag, 3);
  json += ",\"dynAcc\":" + String(latestDynAcc, 3);
  json += ",\"gyroMag\":" + String(latestGyroMag, 3);
  json += ",\"temp\":" + String(latestTempC, 2);
  json += ",\"alert\":" + String(alertActive ? "true" : "false");
  json += ",\"uptime\":" + String(millis());
  json += "}";

  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.send(200, "application/json", json);
}

void setupCaptivePortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASS);

  dnsServer.start(DNS_PORT, "*", apIP);

  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", DASHBOARD_HTML);
  });

  server.on("/data", HTTP_GET, []() {
    sendJsonData();
  });

  server.on("/ack-alert", HTTP_POST, []() {
    alertActive = false;
    server.send(200, "text/plain", "ok");
  });

  server.on("/reset", HTTP_POST, []() {
    resetStats();
    alertActive = false;
    server.send(200, "text/plain", "ok");
  });

  // Redirect all unknown hosts/paths to captive portal page
  server.onNotFound([]() {
    server.sendHeader("Location", String("http://") + apIP.toString(), true);
    server.send(302, "text/plain", "");
  });

  server.begin();

  Serial.println("Hotspot started.");
  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  Serial.print("Password: ");
  Serial.println(AP_PASS);
  Serial.print("Portal URL: http://");
  Serial.println(apIP);
}

void printHelp() {
  Serial.println("\nCommands:");
  Serial.println("  h                -> help");
  Serial.println("  r                -> reset stats");
  Serial.println("  s                -> print status now");
  Serial.println("  c                -> toggle CSV mode");
  Serial.println("  k <stride_m>     -> set stride length in meters");
  Serial.println("  w <weight_kg>    -> set user weight in kg");
  Serial.println("  t <high> <low>   -> set step thresholds (m/s^2)");
  Serial.println("  a                -> auto-calibrate standing baseline");
}

void printStatus(bool force = false) {
  unsigned long nowMs = millis();
  if (!force && (nowMs - lastPrintMs) < 1000) {
    return;
  }
  lastPrintMs = nowMs;

  if (csvMode) {
    Serial.print(nowMs);
    Serial.print(',');
    Serial.print(stepCount);
    Serial.print(',');
    Serial.print(fallCount);
    Serial.print(',');
    Serial.print(cadenceSpm, 1);
    Serial.print(',');
    Serial.print(distanceMeters, 2);
    Serial.print(',');
    Serial.print(caloriesKcal, 2);
    Serial.print(',');
    Serial.print(latestAccMag, 2);
    Serial.print(',');
    Serial.print(latestDynAcc, 2);
    Serial.print(',');
    Serial.print(latestGyroMag, 2);
    Serial.print(',');
    Serial.println(latestTempC, 2);
    return;
  }

  Serial.print("Steps: ");
  Serial.print(stepCount);
  Serial.print(" | Falls: ");
  Serial.print(fallCount);
  Serial.print(" | Cadence: ");
  Serial.print(cadenceSpm, 1);
  Serial.print(" spm | Dist: ");
  Serial.print(distanceMeters, 2);
  Serial.print(" m | Cal: ");
  Serial.print(caloriesKcal, 2);
  Serial.print(" kcal | DynAcc: ");
  Serial.print(latestDynAcc, 2);
  Serial.print(" | ImpactMax: ");
  Serial.println(maxImpact, 2);
}

void autoCalibrateBaseline() {
  Serial.println("Auto-calibration: stand still for 3 seconds...");
  const int samples = 150;
  float sum = 0.0f;

  for (int i = 0; i < samples; i++) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    float m = magnitude3(a.acceleration.x, a.acceleration.y, a.acceleration.z);
    sum += m;
    delay(20);
  }

  gravityMagLP = sum / samples;
  Serial.print("New baseline gravity magnitude: ");
  Serial.println(gravityMagLP, 3);
}

void handleSerialCommands() {
  if (!Serial.available()) {
    return;
  }

  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) {
    return;
  }

  char cmd = line.charAt(0);

  if (cmd == 'h') {
    printHelp();
  } else if (cmd == 'r') {
    resetStats();
    Serial.println("Stats reset.");
  } else if (cmd == 's') {
    printStatus(true);
  } else if (cmd == 'c') {
    csvMode = !csvMode;
    Serial.print("CSV mode: ");
    Serial.println(csvMode ? "ON" : "OFF");
  } else if (cmd == 'a') {
    autoCalibrateBaseline();
  } else if (cmd == 'k') {
    float v;
    if (sscanf(line.c_str(), "k %f", &v) == 1 && v > 0.20f && v < 2.00f) {
      strideLengthMeters = v;
      Serial.print("Stride length set to: ");
      Serial.println(strideLengthMeters, 3);
    } else {
      Serial.println("Usage: k <stride_m> (0.20 to 2.00)");
    }
  } else if (cmd == 'w') {
    float v;
    if (sscanf(line.c_str(), "w %f", &v) == 1 && v > 20.0f && v < 250.0f) {
      userWeightKg = v;
      Serial.print("Weight set to: ");
      Serial.println(userWeightKg, 1);
    } else {
      Serial.println("Usage: w <weight_kg> (20 to 250)");
    }
  } else if (cmd == 't') {
    float high, low;
    if (sscanf(line.c_str(), "t %f %f", &high, &low) == 2 && high > low && low > 0.05f) {
      stepThresholdHigh = high;
      stepThresholdLow = low;
      Serial.print("Step thresholds set. High=");
      Serial.print(stepThresholdHigh, 2);
      Serial.print(" Low=");
      Serial.println(stepThresholdLow, 2);
    } else {
      Serial.println("Usage: t <high> <low>, where high > low > 0");
    }
  } else {
    Serial.println("Unknown command. Use 'h' for help.");
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Serial.println("\nFootwear Step + Fall Detection (MPU6050)");

  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip");
    while (1) {
      delay(10);
    }
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  Serial.println("MPU6050 initialized.");
  setupCaptivePortal();
  printHelp();
  Serial.println("\nTip: run auto-calibration command 'a' while standing still in footwear.\n");

  lastLoopMs = millis();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  handleSerialCommands();

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  unsigned long nowMs = millis();

  latestAccMag = magnitude3(a.acceleration.x, a.acceleration.y, a.acceleration.z);
  latestGyroMag = magnitude3(g.gyro.x, g.gyro.y, g.gyro.z);
  latestTempC = temp.temperature;

  gravityMagLP = gravityAlpha * gravityMagLP + (1.0f - gravityAlpha) * latestAccMag;
  latestDynAcc = fabsf(latestAccMag - gravityMagLP);

  // Track strongest impact seen (useful for tuning and diagnostics)
  if (latestAccMag > maxImpact) {
    maxImpact = latestAccMag;
  }

  // ---------------------------
  // Step detection (hysteresis)
  // ---------------------------
  if (!stepActive) {
    if (latestDynAcc > stepThresholdHigh && (nowMs - lastStepMs) > stepDebounceMs) {
      stepActive = true;
      stepPeakDynamic = latestDynAcc;
    }
  } else {
    if (latestDynAcc > stepPeakDynamic) {
      stepPeakDynamic = latestDynAcc;
    }

    if (latestDynAcc < stepThresholdLow) {
      stepActive = false;
      // Reject very violent events as normal steps
      if (stepPeakDynamic < 8.0f) {
        stepCount++;
        lastStepMs = nowMs;
        addStepTimestamp(nowMs);

        distanceMeters = stepCount * strideLengthMeters;
        caloriesKcal = (userWeightKg * (distanceMeters / 1000.0f)) * 0.75f;
      }
    }
  }

  updateCadence(nowMs);

  // ---------------------------
  // Fall detection
  // Pattern: free-fall -> impact -> stillness
  // ---------------------------
  if (!waitingForImpact && latestAccMag < freeFallThreshold) {
    if (!inPotentialFreeFall) {
      inPotentialFreeFall = true;
      freeFallStartMs = nowMs;
    }

    if ((nowMs - freeFallStartMs) > 80) {
      waitingForImpact = true;
      inPotentialFreeFall = false;
    }
  } else if (!waitingForImpact && latestAccMag >= freeFallThreshold) {
    inPotentialFreeFall = false;
  }

  if (waitingForImpact) {
    if (latestAccMag > impactThreshold) {
      impactDetectedMs = nowMs;
      waitingForImpact = false;
      fallDetected = true;
    } else if ((nowMs - freeFallStartMs) > impactWindowMs) {
      waitingForImpact = false;
      fallDetected = false;
    }
  }

  if (fallDetected) {
    bool still = (latestDynAcc < 0.35f && latestGyroMag < 0.8f);
    if (still && (nowMs - impactDetectedMs) >= stillnessAfterFallMs) {
      fallCount++;
      fallDetected = false;
      alertActive = true;
      lastFallAlertMs = nowMs;
      Serial.println("ALERT: FALL DETECTED");
    }

    // If the person quickly resumes movement, cancel this event.
    if (!still && (nowMs - impactDetectedMs) > stillnessAfterFallMs + 1200) {
      fallDetected = false;
    }
  }

  printStatus(false);

  // Keep update rate near 50 Hz
  unsigned long elapsed = millis() - lastLoopMs;
  if (elapsed < 20) {
    delay(20 - elapsed);
  }
  lastLoopMs = millis();

  // Clear stale alert if not acknowledged for long period.
  if (alertActive && (millis() - lastFallAlertMs) > 60000) {
    alertActive = false;
  }
}

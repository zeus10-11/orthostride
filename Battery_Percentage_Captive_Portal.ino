#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

// ---------------------------
// Wi-Fi captive portal config
// ---------------------------
const char* AP_SSID = "Battery-Monitor";
const char* AP_PASS = "12345678"; // Minimum 8 chars for WPA2
const byte DNS_PORT = 53;

IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
WebServer server(80);

// ---------------------------
// Battery sensing config
// ---------------------------
const int BATTERY_PIN = A0;       // Divider midpoint connected to A0
const int ADC_SAMPLES = 32;       // More samples = smoother value
const float DIVIDER_RATIO = 2.0f; // 220k:220k divider, Vbat = 2 * Vadc

// 1S Li-ion/LiPo voltage range (adjust for your pack behavior)
const float BATTERY_V_MIN = 3.00f;
const float BATTERY_V_MAX = 4.20f;

const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Battery Monitor</title>
  <style>
    :root {
      --sky: #dff3ff;
      --mint: #e9ffe8;
      --card: rgba(255,255,255,0.9);
      --ink: #143047;
      --good: #1f8f4e;
      --warn: #cc7a00;
      --bad: #b00020;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: "Trebuchet MS", "Segoe UI", sans-serif;
      background: linear-gradient(135deg, var(--sky), var(--mint));
      color: var(--ink);
      min-height: 100vh;
      display: grid;
      place-items: center;
      padding: 16px;
    }
    .panel {
      width: min(560px, 100%);
      border-radius: 16px;
      background: var(--card);
      border: 1px solid rgba(255,255,255,0.95);
      box-shadow: 0 12px 32px rgba(0,0,0,0.08);
      padding: 20px;
    }
    h1 { margin: 0 0 6px; font-size: 1.4rem; }
    .muted { margin: 0; opacity: 0.8; }
    .row {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
      gap: 12px;
      margin-top: 14px;
    }
    .card {
      background: #ffffff;
      border: 1px solid #e6f0f7;
      border-radius: 12px;
      padding: 12px;
    }
    .label { font-size: 0.84rem; opacity: 0.8; }
    .value { font-size: 1.6rem; font-weight: 700; margin-top: 3px; }
    .battery {
      margin-top: 16px;
      border: 2px solid #6f8ea8;
      border-radius: 10px;
      height: 40px;
      position: relative;
      overflow: hidden;
      background: #f2f7fb;
    }
    .battery::after {
      content: "";
      position: absolute;
      right: -8px;
      top: 11px;
      width: 6px;
      height: 14px;
      border-radius: 0 3px 3px 0;
      background: #6f8ea8;
    }
    .fill {
      height: 100%;
      width: 0%;
      transition: width 0.35s ease;
      background: linear-gradient(90deg, #ff5c5c, #ffbe55, #35b06e);
    }
    .status {
      margin-top: 12px;
      font-weight: 700;
      padding: 10px 12px;
      border-radius: 10px;
      background: #eaf8f0;
      color: var(--good);
    }
  </style>
</head>
<body>
  <div class="panel">
    <h1>Live Battery Monitor</h1>
    <p class="muted">ESP32 captive portal for 1S Li-ion/LiPo battery</p>

    <div class="row">
      <div class="card">
        <div class="label">Battery Voltage</div>
        <div id="voltage" class="value">0.000 V</div>
      </div>
      <div class="card">
        <div class="label">Charge</div>
        <div id="percent" class="value">0 %</div>
      </div>
    </div>

    <div class="battery"><div id="fill" class="fill"></div></div>
    <div id="status" class="status">Reading...</div>
  </div>

  <script>
    function clamp(v, min, max) {
      return Math.max(min, Math.min(max, v));
    }

    async function refresh() {
      try {
        const r = await fetch('/data', { cache: 'no-store' });
        const d = await r.json();

        document.getElementById('voltage').textContent = Number(d.voltage).toFixed(3) + ' V';
        document.getElementById('percent').textContent = Math.round(d.percent) + ' %';

        const fill = clamp(d.percent, 0, 100);
        document.getElementById('fill').style.width = fill + '%';

        const status = document.getElementById('status');
        if (d.percent >= 60) {
          status.textContent = 'Battery healthy';
          status.style.background = '#eaf8f0';
          status.style.color = '#1f8f4e';
        } else if (d.percent >= 25) {
          status.textContent = 'Battery medium';
          status.style.background = '#fff6e8';
          status.style.color = '#cc7a00';
        } else {
          status.textContent = 'Battery low - recharge now';
          status.style.background = '#ffecef';
          status.style.color = '#b00020';
        }
      } catch (e) {
        document.getElementById('status').textContent = 'Connection retry...';
      }
    }

    setInterval(refresh, 1000);
    refresh();
  </script>
</body>
</html>
)rawliteral";

float readBatteryVoltage() {
  uint32_t sumMv = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    sumMv += analogReadMilliVolts(BATTERY_PIN);
    delay(2);
  }

  float adcMv = (float)sumMv / (float)ADC_SAMPLES;
  float battV = (adcMv / 1000.0f) * DIVIDER_RATIO;
  return battV;
}

// Piecewise approximation for 1S Li-ion state-of-charge.
float voltageToPercent(float v) {
  if (v >= 4.20f) return 100.0f;
  if (v <= 3.00f) return 0.0f;

  if (v >= 4.10f) return 90.0f + (v - 4.10f) * 100.0f;    // 4.10-4.20 => 90-100
  if (v >= 4.00f) return 80.0f + (v - 4.00f) * 100.0f;    // 4.00-4.10 => 80-90
  if (v >= 3.90f) return 60.0f + (v - 3.90f) * 200.0f;    // 3.90-4.00 => 60-80
  if (v >= 3.80f) return 40.0f + (v - 3.80f) * 200.0f;    // 3.80-3.90 => 40-60
  if (v >= 3.70f) return 25.0f + (v - 3.70f) * 150.0f;    // 3.70-3.80 => 25-40
  if (v >= 3.60f) return 15.0f + (v - 3.60f) * 100.0f;    // 3.60-3.70 => 15-25
  if (v >= 3.50f) return 8.0f + (v - 3.50f) * 70.0f;      // 3.50-3.60 => 8-15
  if (v >= 3.40f) return 3.0f + (v - 3.40f) * 50.0f;      // 3.40-3.50 => 3-8
  return (v - 3.00f) * 7.5f;                              // 3.00-3.40 => 0-3
}

void handleRoot() {
  server.send(200, "text/html", DASHBOARD_HTML);
}

void handleData() {
  float voltage = readBatteryVoltage();
  float percent = voltageToPercent(voltage);

  String json = "{";
  json += "\"voltage\":" + String(voltage, 3) + ",";
  json += "\"percent\":" + String(percent, 1);
  json += "}";

  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send(200, "application/json", json);
}

void handleCaptivePortal() {
  server.sendHeader("Location", String("http://") + apIP.toString(), true);
  server.send(302, "text/plain", "");
}

void setup() {
  Serial.begin(115200);

  pinMode(BATTERY_PIN, INPUT);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASS);

  dnsServer.start(DNS_PORT, "*", apIP);

  server.on("/", handleRoot);
  server.on("/data", handleData);

  // Captive portal handlers for common OS probes.
  server.on("/generate_204", handleCaptivePortal);
  server.on("/hotspot-detect.html", handleCaptivePortal);
  server.on("/ncsi.txt", handleCaptivePortal);
  server.onNotFound(handleCaptivePortal);

  server.begin();

  Serial.println("\nBattery Monitor AP started");
  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  Serial.print("Password: ");
  Serial.println(AP_PASS);
  Serial.print("Portal URL: http://");
  Serial.println(apIP);
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
}

#include <EEPROM.h>
#include <Wire.h>
#include <math.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <esp_wifi.h>

// =====================================================
// OrthoStride - Smart Rehab Footwear (MYOSA motherboard)
// Integrated features:
// 1) FSR pressure + calibrated weight estimation
// 2) Closed-loop overload biofeedback with 3 vibrators
// 3) Step, cadence, distance, calorie, fall analytics (MYOSA MPU / MPU6050)
// 4) Hotspot-based geofencing (client proximity)
// 5) Hybrid energy demo (step pressure -> virtual charge)
// 6) Captive portal dashboard + control APIs
// =====================================================

// ---------------------------
// Hardware pins (EDIT FOR YOUR BOARD WIRING - MYOSA)
// ---------------------------
// Update these constants to match the MYOSA motherboard pinout.
// If MYOSA uses default Wire() I2C pins, set I2C_SDA_PIN/I2C_SCL_PIN to -1 to use defaults.
// Replace the placeholder values below with the correct MYOSA pin numbers.
const int FSR_PIN = A0;          // Analog input for FSR (set to MYOSA ADC pin)
const int BATTERY_PIN = A1;      // Analog input for battery measurement (set to MYOSA ADC pin)
const int VIB1_PIN = 2;          // Vibration motor 1 (set to MYOSA GPIO)
const int VIB2_PIN = 3;          // Vibration motor 2 (set to MYOSA GPIO)
const int VIB3_PIN = 4;          // Vibration motor 3 (set to MYOSA GPIO)
const int I2C_SDA_PIN = -1;      // MYOSA SDA pin (set to -1 to use default Wire SDA)
const int I2C_SCL_PIN = -1;      // MYOSA SCL pin (set to -1 to use default Wire SCL)

// ---------------------------
// EEPROM calibration storage
// ---------------------------
#define EEPROM_SIZE 512
#define CALIBRATION_ADDR 0
#define CAL_SIGNATURE 0xAA
#define NUM_WEIGHTS 6
#define NUM_SAMPLES 50

float calibrationWeights[NUM_WEIGHTS] = {2.0f, 3.0f, 5.0f, 7.0f, 8.0f, 10.0f};
int calibrationValues[NUM_WEIGHTS];
bool isCalibrated = false;

// ---------------------------
// FSR filtering
// ---------------------------
#define MOVING_AVG_SIZE 20
#define ZERO_THRESHOLD 20
#define OUTLIER_THRESHOLD 220
#define MIN_STABLE_READINGS 5

int fsrBuffer[MOVING_AVG_SIZE];
int fsrBufferIndex = 0;
bool fsrBufferFilled = false;
int fsrLastValidReading = 0;
int fsrStableCount = 0;

float latestWeightKg = 0.0f;
float lastDisplayedWeight = 0.0f;

// ---------------------------
// Doctor thresholds / alerting
// ---------------------------
float doctorWeightLimitKg = 8.0f;
unsigned long overloadHoldMs = 700;
unsigned long overloadStartMs = 0;
bool overloadActive = false;

// ---------------------------
// MPU / gait / fall tracking
// ---------------------------
Adafruit_MPU6050 mpu;

float stepThresholdHigh = 1.20f;
float stepThresholdLow = 0.45f;
unsigned long stepDebounceMs = 250;

float freeFallThreshold = 3.0f;
float impactThreshold = 24.0f;
unsigned long impactWindowMs = 1200;
unsigned long stillnessAfterFallMs = 2000;

float strideLengthMeters = 0.70f;
float userWeightKg = 65.0f;

unsigned long stepCount = 0;
unsigned long fallCount = 0;
float cadenceSpm = 0.0f;
float distanceMeters = 0.0f;
float caloriesKcal = 0.0f;
float maxImpact = 0.0f;
unsigned long lastStepMs = 0;

unsigned long stepTimestamps[20] = {0};
uint8_t stepTsIndex = 0;

bool stepActive = false;
float stepPeakDynamic = 0.0f;

float gravityMagLP = 9.81f;
const float gravityAlpha = 0.92f;

bool inPotentialFreeFall = false;
bool waitingForImpact = false;
bool fallDetected = false;
bool fallAlertActive = false;
unsigned long freeFallStartMs = 0;
unsigned long impactDetectedMs = 0;
unsigned long lastFallConfirmedMs = 0;
unsigned long fallCooldownMs = 3000;

float latestAccMag = 0.0f;
float latestDynAcc = 0.0f;
float latestGyroMag = 0.0f;
float latestTempC = 0.0f;

// ---------------------------
// Hotspot geofence (proximity)
// ---------------------------
const char* AP_SSID = "OrthoStride-Rehab";
const char* AP_PASS = "12345678";
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);

DNSServer dnsServer;
WebServer server(80);

unsigned long lastClientSeenMs = 0;
unsigned long geofenceTimeoutMs = 15000;
bool geofenceAlert = false;
bool hasSeenHotspotClient = false;
float geofenceDistanceLimitM = 8.0f;
float geofenceCurrentDistanceM = 0.0f;
int latestClientRssi = -127;
const float rssiAt1m = -45.0f;
const float pathLossExponent = 2.2f;
unsigned long geofenceBreachStartMs = 0;
unsigned long geofenceHoldMs = 3500;

// ---------------------------
// Hybrid energy demo
// ---------------------------
float harvestedEnergymWh = 0.0f;
float virtualBatteryPct = 35.0f;
bool chargingNow = false;
unsigned long chargingUntilMs = 0;

// ---------------------------
// Real battery monitor
// ---------------------------
const int ADC_SAMPLES = 32;
const float DIVIDER_RATIO = 2.0f;
const float BATTERY_V_MIN = 3.00f;
const float BATTERY_V_MAX = 4.20f;
float latestBatteryVoltage = 0.0f;
float latestBatteryPercent = 0.0f;
unsigned long lastBatteryReadMs = 0;

// ---------------------------
// Alert + massage control
// ---------------------------
bool alertActive = false;
unsigned long lastFallAlertMs = 0;

bool massageMode = false;
uint8_t massageIntensity = 150;

// Massage modes: 0=basic, 1=intense, 2=therapeutic, 3=recovery
uint8_t massageMode_selected = 0;
const char* massageMode_names[] = {"Basic Wave", "Intense Pulse", "Therapeutic Beat", "Recovery Cycle"};

// FSR charging simulation
float fsrChargeLevel = 0.0f;

// Feature flags for enable/disable
bool fallDetectionEnabled = true;
bool geofenceEnabled = true;
bool overloadAlertEnabled = true;

// PWM setup
const int pwmFreq = 5000;
const int pwmRes = 8;

// Loop timing
unsigned long lastLoopMs = 0;
unsigned long lastPrintMs = 0;
bool csvMode = false;

const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no" />
  <title>OrthoStride Rehab Dashboard</title>
  <style>
    :root {
      --primary: #0f7dff;
      --primary-dark: #0050cc;
      --primary-light: #e6f3ff;
      --success: #10b981;
      --warning: #f59e0b;
      --danger: #ef4444;
      --info: #06b6d4;
      --bg-dark: #0a1628;
      --bg-light: #f8fbff;
      --surface: #ffffff;
      --surface-secondary: #f0f6ff;
      --text-primary: #1a2332;
      --text-secondary: #5b7395;
      --border: #d4dce8;
      --shadow-sm: 0 2px 8px rgba(15, 125, 255, 0.08);
      --shadow-md: 0 8px 24px rgba(15, 125, 255, 0.12);
      --shadow-lg: 0 16px 40px rgba(15, 125, 255, 0.16);
      --radius: 12px;
    }

    * {
      box-sizing: border-box;
      -webkit-tap-highlight-color: transparent;
    }

    html, body {
      margin: 0;
      padding: 0;
      width: 100%;
      height: 100%;
    }

    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", "Roboto", sans-serif;
      background: linear-gradient(135deg, var(--bg-light) 0%, #f0f6ff 100%);
      color: var(--text-primary);
      min-height: 100vh;
      position: relative;
      overflow-x: hidden;
    }

    /* Glassmorphism background elements */
    body::before {
      content: "";
      position: fixed;
      top: -20%;
      right: -10%;
      width: 500px;
      height: 500px;
      background: radial-gradient(circle, rgba(15, 125, 255, 0.08) 0%, transparent 70%);
      border-radius: 50%;
      z-index: -1;
    }

    body::after {
      content: "";
      position: fixed;
      bottom: -10%;
      left: -5%;
      width: 400px;
      height: 400px;
      background: radial-gradient(circle, rgba(16, 185, 129, 0.06) 0%, transparent 70%);
      border-radius: 50%;
      z-index: -1;
    }

    .container {
      max-width: 1400px;
      margin: 0 auto;
      padding: 16px;
    }

    /* ===== HEADER SECTION ===== */
    .header {
      margin-bottom: 32px;
    }

    .header-title {
      margin: 0 0 8px 0;
      font-size: 2rem;
      font-weight: 800;
      color: var(--text-primary);
      letter-spacing: -0.01em;
    }

    .header-subtitle {
      margin: 0;
      font-size: 0.95rem;
      color: var(--text-secondary);
      font-weight: 500;
      letter-spacing: 0.3px;
    }

    .header-stats {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 12px;
      margin-top: 16px;
    }

    .stat-pill {
      display: flex;
      align-items: center;
      gap: 10px;
      background: var(--surface);
      border: 1px solid var(--border);
      border-radius: 10px;
      padding: 12px 16px;
      box-shadow: var(--shadow-sm);
      transition: all 0.25s ease;
    }

    .stat-pill:hover {
      border-color: var(--primary);
      box-shadow: var(--shadow-md);
      transform: translateY(-2px);
    }

    .stat-pill-icon {
      font-size: 1.3rem;
      opacity: 0.8;
    }

    .stat-pill-text {
      font-weight: 600;
      color: var(--text-secondary);
      font-size: 0.9rem;
    }

    .stat-pill-value {
      font-weight: 700;
      color: var(--primary);
      font-size: 1.1rem;
      margin-left: auto;
    }

    /* ===== STATUS BANNER ===== */
    .status-banner {
      margin-bottom: 24px;
      padding: 16px 20px;
      border-radius: 12px;
      font-weight: 600;
      display: flex;
      align-items: center;
      gap: 12px;
      box-shadow: var(--shadow-md);
      transition: all 0.3s ease;
    }

    .status-banner.normal {
      background: #ecfdf5;
      color: var(--success);
      border: 1px solid #97efc1;
    }

    .status-banner.warning {
      background: #fef3c7;
      color: #a16207;
      border: 1px solid #fcd34d;
    }

    .status-banner.alert {
      background: #fee2e2;
      color: var(--danger);
      border: 1px solid #fca5a5;
      animation: pulse-alert 2s infinite;
    }

    .status-banner.charging {
      background: #e0f2ff;
      color: var(--primary);
      border: 1px solid #7dd3fc;
    }

    @keyframes pulse-alert {
      0%, 100% { box-shadow: 0 0 0 0 rgba(239, 68, 68, 0.4), var(--shadow-md); }
      50% { box-shadow: 0 0 0 8px rgba(239, 68, 68, 0), var(--shadow-md); }
    }

    .status-icon {
      font-size: 1.3rem;
      flex-shrink: 0;
    }

    .status-text {
      flex: 1;
      letter-spacing: 0.3px;
    }

    /* ===== SECTION TITLE ===== */
    .section-title {
      font-size: 1.1rem;
      font-weight: 700;
      color: var(--text-primary);
      margin: 32px 0 16px 0;
      text-transform: uppercase;
      letter-spacing: 0.5px;
      display: flex;
      align-items: center;
      gap: 8px;
    }

    .section-title::before {
      content: "";
      width: 4px;
      height: 20px;
      background: var(--primary);
      border-radius: 2px;
    }

    /* ===== CARD GRID ===== */
    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 16px;
      margin-bottom: 24px;
    }

    .card {
      background: var(--surface);
      border: 1px solid var(--border);
      border-radius: var(--radius);
      padding: 20px;
      box-shadow: var(--shadow-sm);
      transition: all 0.3s cubic-bezier(0.34, 1.56, 0.64, 1);
      backdrop-filter: blur(10px);
    }

    .card:hover {
      transform: translateY(-4px);
      box-shadow: var(--shadow-lg);
      border-color: var(--primary-light);
    }

    .card-label {
      font-size: 0.75rem;
      font-weight: 700;
      text-transform: uppercase;
      color: var(--text-secondary);
      letter-spacing: 0.4px;
      margin-bottom: 8px;
    }

    .card-value {
      font-size: 2rem;
      font-weight: 800;
      color: var(--primary);
      margin-bottom: 12px;
      line-height: 1;
    }

    .card-bar {
      height: 6px;
      background: var(--surface-secondary);
      border-radius: 3px;
      overflow: hidden;
      margin-top: 12px;
    }

    .card-fill {
      height: 100%;
      width: 0%;
      background: linear-gradient(90deg, var(--primary), var(--info));
      border-radius: 3px;
      transition: width 0.4s cubic-bezier(0.34, 1.56, 0.64, 1);
    }

    /* ===== BATTERY SECTION ===== */
    .battery-widget {
      display: flex;
      align-items: center;
      gap: 12px;
      margin: 12px 0;
      position: relative;
    }

    .battery-icon {
      width: 48px;
      height: 28px;
      border: 2.5px solid var(--text-primary);
      border-radius: 4px;
      position: relative;
      overflow: hidden;
      background: var(--surface-secondary);
    }

    .battery-icon::after {
      content: "";
      position: absolute;
      right: -3px;
      top: 50%;
      transform: translateY(-50%);
      width: 4px;
      height: 12px;
      background: var(--text-primary);
      border-radius: 0 3px 3px 0;
    }

    .battery-fill {
      height: 100%;
      width: 100%;
      background: linear-gradient(90deg, var(--success), #10b981);
      border-radius: 2px;
      transition: width 0.3s ease;
      position: relative;
    }

    .battery-fill::after {
      content: "⚡";
      position: absolute;
      top: -2px;
      right: 2px;
      font-size: 0.8rem;
      opacity: 0;
      animation: charging-pulse 1s infinite;
    }

    .battery-fill.charging::after {
      opacity: 1;
    }

    @keyframes charging-pulse {
      0%, 100% { opacity: 0.3; transform: scale(1); }
      50% { opacity: 1; transform: scale(1.2); }
    }

    .battery-text {
      font-weight: 700;
      font-size: 1rem;
      color: var(--text-primary);
      min-width: 45px;
    }

    /* ===== BUTTON GRID ===== */
    .button-group {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
      gap: 12px;
      margin-bottom: 24px;
    }

    .button-row {
      display: flex;
      gap: 12px;
      flex-wrap: wrap;
      margin-bottom: 16px;
    }

    /* ===== BUTTONS ===== */
    button, .btn {
      border: none;
      padding: 12px 18px;
      border-radius: 8px;
      font-weight: 700;
      cursor: pointer;
      font-size: 0.9rem;
      transition: all 0.2s ease;
      display: inline-flex;
      align-items: center;
      gap: 6px;
      white-space: nowrap;
      touch-action: manipulation;
      -webkit-user-select: none;
      user-select: none;
    }

    button:active, .btn:active {
      transform: scale(0.96);
    }

    button:disabled, .btn:disabled {
      opacity: 0.5;
      cursor: not-allowed;
    }

    .btn-primary {
      background: linear-gradient(135deg, var(--primary), var(--primary-dark));
      color: white;
      box-shadow: var(--shadow-md);
    }

    .btn-primary:hover:not(:disabled) {
      box-shadow: var(--shadow-lg);
      transform: translateY(-2px);
    }

    .btn-success {
      background: linear-gradient(135deg, var(--success), #059669);
      color: white;
      box-shadow: var(--shadow-md);
    }

    .btn-success:hover:not(:disabled) {
      box-shadow: var(--shadow-lg);
      transform: translateY(-2px);
    }

    .btn-warning {
      background: linear-gradient(135deg, var(--warning), #d97706);
      color: white;
      box-shadow: var(--shadow-md);
    }

    .btn-warning:hover:not(:disabled) {
      box-shadow: var(--shadow-lg);
      transform: translateY(-2px);
    }

    .btn-danger {
      background: linear-gradient(135deg, var(--danger), #dc2626);
      color: white;
      box-shadow: var(--shadow-md);
    }

    .btn-danger:hover:not(:disabled) {
      box-shadow: var(--shadow-lg);
      transform: translateY(-2px);
    }

    .btn-info {
      background: linear-gradient(135deg, var(--info), #0891b2);
      color: white;
      box-shadow: var(--shadow-md);
    }

    .btn-info:hover:not(:disabled) {
      box-shadow: var(--shadow-lg);
      transform: translateY(-2px);
    }

    .btn-sm {
      padding: 8px 14px;
      font-size: 0.85rem;
    }

    /* ===== MODERN TOGGLE SWITCH ===== */
    .toggle-switch {
      display: inline-flex;
      align-items: center;
      gap: 10px;
      background: var(--surface);
      border: 1px solid var(--border);
      padding: 10px 16px;
      border-radius: 8px;
      font-weight: 600;
      color: var(--text-secondary);
      transition: all 0.2s ease;
      cursor: pointer;
      user-select: none;
    }

    .toggle-switch.active {
      background: var(--primary-light);
      border-color: var(--primary);
      color: var(--primary);
    }

    .switch {
      display: inline-block;
      position: relative;
      width: 44px;
      height: 24px;
    }

    .switch input {
      display: none;
    }

    .slider {
      position: absolute;
      top: 0;
      left: 0;
      right: 0;
      bottom: 0;
      background: #cbd5e1;
      border-radius: 12px;
      transition: 0.3s;
      cursor: pointer;
    }

    .slider::before {
      content: "";
      position: absolute;
      height: 20px;
      width: 20px;
      left: 2px;
      bottom: 2px;
      background: white;
      border-radius: 50%;
      transition: 0.3s;
    }

    input:checked + .slider {
      background: var(--primary);
    }

    input:checked + .slider::before {
      transform: translateX(20px);
    }

    /* ===== FORM INPUTS ===== */
    input[type="number"], input[type="text"], select {
      border: 1.5px solid var(--border);
      border-radius: 8px;
      padding: 10px 14px;
      font-weight: 600;
      color: var(--text-primary);
      background: var(--surface-secondary);
      transition: all 0.2s ease;
      font-size: 0.95rem;
    }

    input[type="number"]:hover, input[type="text"]:hover, select:hover {
      border-color: var(--primary);
      background: var(--surface);
    }

    input[type="number"]:focus, input[type="text"]:focus, select:focus {
      outline: none;
      border-color: var(--primary);
      box-shadow: 0 0 0 3px var(--primary-light);
    }

    input[type="range"] {
      width: 100%;
      height: 6px;
      border-radius: 3px;
      background: var(--surface-secondary);
      outline: none;
      -webkit-appearance: none;
      appearance: none;
      cursor: pointer;
    }

    input[type="range"]::-webkit-slider-thumb {
      -webkit-appearance: none;
      appearance: none;
      width: 20px;
      height: 20px;
      border-radius: 50%;
      background: var(--primary);
      cursor: pointer;
      box-shadow: var(--shadow-md);
      transition: all 0.2s ease;
    }

    input[type="range"]::-webkit-slider-thumb:hover {
      transform: scale(1.15);
      box-shadow: var(--shadow-lg);
    }

    input[type="range"]::-moz-range-thumb {
      width: 20px;
      height: 20px;
      border-radius: 50%;
      background: var(--primary);
      cursor: pointer;
      border: none;
      box-shadow: var(--shadow-md);
      transition: all 0.2s ease;
    }

    input[type="range"]::-moz-range-thumb:hover {
      transform: scale(1.15);
      box-shadow: var(--shadow-lg);
    }

    /* ===== FORM GROUP ===== */
    .form-group {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
      gap: 16px;
      align-items: end;
      margin-bottom: 16px;
    }

    .form-field {
      display: flex;
      flex-direction: column;
      gap: 6px;
    }

    .form-field label {
      font-weight: 600;
      color: var(--text-secondary);
      font-size: 0.9rem;
    }

    .form-field input {
      width: 100%;
    }

    .graphs-grid {
      display: grid;
      grid-template-columns: repeat(3, minmax(0, 1fr));
      gap: 12px;
      margin-bottom: 24px;
    }

    .graph-card {
      background: var(--surface);
      border: 1px solid var(--border);
      border-radius: var(--radius);
      padding: 14px;
      box-shadow: var(--shadow-sm);
    }

    .graph-title {
      font-size: 0.78rem;
      font-weight: 700;
      text-transform: uppercase;
      color: var(--text-secondary);
      margin-bottom: 8px;
      letter-spacing: 0.35px;
    }

    .spark {
      width: 100%;
      height: 96px;
      display: block;
      border-radius: 8px;
      background: linear-gradient(180deg, #f8fbff 0%, #eef4ff 100%);
      border: 1px solid #e1e8f5;
    }

    .spark polyline {
      fill: none;
      stroke-width: 2.2;
      stroke-linecap: round;
      stroke-linejoin: round;
    }

    /* ===== RESPONSIVE GRID ===== */
    @media (max-width: 768px) {
      .container {
        padding: 12px;
      }

      .header-title {
        font-size: 1.5rem;
      }

      .header-stats {
        grid-template-columns: repeat(auto-fit, minmax(160px, 1fr));
        gap: 10px;
      }

      .grid {
        grid-template-columns: repeat(auto-fit, minmax(160px, 1fr));
        gap: 12px;
      }

      .graphs-grid {
        grid-template-columns: 1fr;
      }

      .card {
        padding: 16px;
      }

      .card-value {
        font-size: 1.5rem;
      }

      .button-group {
        grid-template-columns: repeat(2, 1fr);
      }

      .form-group {
        grid-template-columns: 1fr;
      }

      .section-title {
        font-size: 1rem;
        margin: 24px 0 12px 0;
      }

      button, .btn {
        padding: 10px 14px;
        font-size: 0.85rem;
      }

      .stat-pill {
        flex-direction: column;
        text-align: center;
        gap: 6px;
      }

      .stat-pill-value {
        margin-left: auto;
        margin-right: auto;
      }
    }

    @media (max-width: 480px) {
      .header-title {
        font-size: 1.3rem;
      }

      .grid {
        grid-template-columns: repeat(2, minmax(0, 1fr));
        gap: 10px;
      }

      .card-value {
        font-size: 1.15rem;
      }

      .button-group {
        grid-template-columns: 1fr;
      }

      .graphs-grid {
        grid-template-columns: 1fr;
      }

      button, .btn {
        width: 100%;
        justify-content: center;
      }

      .form-group {
        gap: 12px;
      }

      input[type="range"] {
        width: 100%;
      }

      .status-banner {
        padding: 14px 16px;
        font-size: 0.9rem;
      }

      .stat-pill {
        padding: 10px 12px;
      }
    }
  </style>
</head>
<body>
  <div class="container">
    <!-- HEADER SECTION -->
    <div class="header">
      <h1 class="header-title">🏥 OrthoStride</h1>
      <p class="header-subtitle">Smart Rehabilitation Footwear with AI Biofeedback & Gait Analytics</p>

      <div class="header-stats">
        <div class="stat-pill">
          <span class="stat-pill-icon">📡</span>
          <span class="stat-pill-text">Signal</span>
          <span class="stat-pill-value" id="rssi">--</span>
          <span style="font-size: 0.75rem; color: var(--text-secondary);">dBm</span>
        </div>
        <div class="stat-pill">
          <span class="stat-pill-icon">📏</span>
          <span class="stat-pill-text">Distance</span>
          <span class="stat-pill-value" id="geoDist">0.0</span>
          <span style="font-size: 0.75rem; color: var(--text-secondary);">m</span>
        </div>
        <div class="stat-pill">
          <span class="stat-pill-icon">🎯</span>
          <span class="stat-pill-text">Geofence</span>
          <span class="stat-pill-value" id="geoLimit">0.0</span>
          <span style="font-size: 0.75rem; color: var(--text-secondary);">m</span>
        </div>
        <div class="stat-pill">
          <span class="stat-pill-icon">👥</span>
          <span class="stat-pill-text">Connected</span>
          <span class="stat-pill-value" id="clients">0</span>
          <span style="font-size: 0.75rem; color: var(--text-secondary);">clients</span>
        </div>
      </div>
    </div>

    <!-- STATUS BANNER -->
    <div id="status" class="status-banner normal">
      <span class="status-icon">✓</span>
      <span class="status-text">Normal rehabilitation activity</span>
    </div>

    <!-- BATTERY & ENERGY SECTION -->
    <h2 class="section-title">⚡ Power & Energy</h2>
    <div class="grid">
      <div class="card">
        <div class="card-label">Real Battery</div>
        <div class="battery-widget">
          <div class="battery-icon"><div id="batteryFillIcon" class="battery-fill"></div></div>
          <span class="battery-text" id="battPct">0%</span>
        </div>
        <div class="card-bar"><div id="battPctFill" class="card-fill"></div></div>
      </div>

      <div class="card">
        <div class="card-label">Voltage</div>
        <div class="card-value" id="battVolt">0.0V</div>
        <div class="card-bar"><div id="battVoltFill" class="card-fill"></div></div>
      </div>

      <div class="card">
        <div class="card-label">FSR Charge</div>
        <div class="card-value" id="fsrCharge">0%</div>
        <div class="card-bar"><div id="fsrChargeFill" class="card-fill"></div></div>
      </div>

      <div class="card">
        <div class="card-label">Virtual Battery</div>
        <div class="card-value" id="battery">0%</div>
        <div class="card-bar"><div id="batteryFill" class="card-fill"></div></div>
      </div>
    </div>

    <!-- CORE METRICS SECTION -->
    <h2 class="section-title">📊 Core Metrics</h2>
    <div class="grid">
      <div class="card">
        <div class="card-label">Weight (kg)</div>
        <div class="card-value" id="weight">0</div>
      </div>
      <div class="card">
        <div class="card-label">Doctor Limit (kg)</div>
        <div class="card-value" id="limit">0</div>
      </div>
      <div class="card">
        <div class="card-label">Steps</div>
        <div class="card-value" id="steps">0</div>
        <div class="card-bar"><div id="stepsFill" class="card-fill"></div></div>
      </div>
      <div class="card">
        <div class="card-label">Falls</div>
        <div class="card-value" id="falls">0</div>
      </div>
      <div class="card">
        <div class="card-label">Cadence (spm)</div>
        <div class="card-value" id="cadence">0</div>
      </div>
      <div class="card">
        <div class="card-label">Distance (m)</div>
        <div class="card-value" id="distance">0</div>
      </div>
      <div class="card">
        <div class="card-label">Calories (kcal)</div>
        <div class="card-value" id="calories">0</div>
      </div>
      <div class="card">
        <div class="card-label">Temperature (°C)</div>
        <div class="card-value" id="temp">0</div>
      </div>
    </div>

    <!-- ADVANCED METRICS SECTION -->
    <h2 class="section-title">🎯 Advanced Metrics</h2>
    <div class="grid">
      <div class="card">
        <div class="card-label">Dynamic Acceleration</div>
        <div class="card-value" id="dynAcc">0</div>
        <div class="card-bar"><div id="dynFill" class="card-fill"></div></div>
      </div>
      <div class="card">
        <div class="card-label">Impact Magnitude</div>
        <div class="card-value" id="impact">0</div>
        <div class="card-bar"><div id="impactFill" class="card-fill"></div></div>
      </div>
      <div class="card">
        <div class="card-label">Energy Harvested (mWh)</div>
        <div class="card-value" id="energy">0</div>
        <div class="card-bar"><div id="energyFill" class="card-fill"></div></div>
      </div>
      <div class="card">
        <div class="card-label">Gyro Magnitude</div>
        <div class="card-value" id="gyro">0</div>
        <div class="card-bar"><div id="gyroFill" class="card-fill"></div></div>
      </div>
    </div>

    <!-- LIVE GRAPHS -->
    <h2 class="section-title">📈 Live Trends</h2>
    <div class="graphs-grid">
      <div class="graph-card">
        <div class="graph-title">Weight Trend (kg)</div>
        <svg class="spark" viewBox="0 0 300 100" preserveAspectRatio="none">
          <polyline id="gWeight" points=""></polyline>
        </svg>
      </div>
      <div class="graph-card">
        <div class="graph-title">Dynamic Accel Trend</div>
        <svg class="spark" viewBox="0 0 300 100" preserveAspectRatio="none">
          <polyline id="gDyn" points=""></polyline>
        </svg>
      </div>
      <div class="graph-card">
        <div class="graph-title">Battery % Trend</div>
        <svg class="spark" viewBox="0 0 300 100" preserveAspectRatio="none">
          <polyline id="gBatt" points=""></polyline>
        </svg>
      </div>
    </div>

    <!-- CONTROLS SECTION -->
    <h2 class="section-title">🎮 Quick Actions</h2>
    <div class="button-row">
      <button class="btn btn-primary" onclick="ackAlert()">🔔 Acknowledge Alert</button>
      <button class="btn btn-success" onclick="toggleMassage()">💆 Toggle Massage</button>
      <button class="btn btn-info" onclick="resetStats()">🔄 Reset Stats</button>
      <button class="btn btn-warning" onclick="startCal()">⚙️ Calibrate FSR</button>
      <button class="btn btn-primary" onclick="downloadExcelData()">⬇️ Download Excel (CSV)</button>
    </div>

    <!-- FEATURE TOGGLES SECTION -->
    <h2 class="section-title">🎛️ Safety Features</h2>
    <div class="button-group">
      <label class="toggle-switch active" id="toggleFallLabel">
        <span>Fall Detection</span>
        <div class="switch">
          <input type="checkbox" id="toggleFallSwitch" checked onchange="toggleFeature('fall')">
          <span class="slider"></span>
        </div>
      </label>
      <label class="toggle-switch active" id="toggleGeofenceLabel">
        <span>Geofence Alert</span>
        <div class="switch">
          <input type="checkbox" id="toggleGeofenceSwitch" checked onchange="toggleFeature('geofence')">
          <span class="slider"></span>
        </div>
      </label>
      <label class="toggle-switch active" id="toggleOverloadLabel">
        <span>Overload Alert</span>
        <div class="switch">
          <input type="checkbox" id="toggleOverloadSwitch" checked onchange="toggleFeature('overload')">
          <span class="slider"></span>
        </div>
      </label>
    </div>

    <!-- MASSAGE THERAPY SECTION -->
    <h2 class="section-title">🎵 Massage Therapy</h2>
    <div class="form-group">
      <div class="form-field">
        <label for="massageMode">Therapy Mode</label>
        <select id="massageMode" onchange="updateMassageMode()">
          <option value="0">Basic Wave (slow rotating)</option>
          <option value="1">Intense Pulse (fast pattern)</option>
          <option value="2">Therapeutic Beat (medical rhythm)</option>
          <option value="3">Recovery Cycle (deep tissue)</option>
        </select>
      </div>

      <div class="form-field">
        <label for="massageRange">Intensity: <strong id="massageValue">150</strong>/255</label>
        <input id="massageRange" type="range" min="40" max="255" step="1" value="150" oninput="showMassageValue()" />
      </div>

      <button class="btn btn-success" onclick="setMassageIntensity()">Apply Intensity</button>
    </div>

    <!-- SETTINGS SECTION -->
    <h2 class="section-title">⚙️ Settings & Personalization</h2>
    <div class="form-group">
      <div class="form-field">
        <label for="limitInput">Doctor Weight Limit (kg)</label>
        <input id="limitInput" type="number" min="1" max="40" step="0.1" value="8.0" />
      </div>

      <div class="form-field">
        <label for="geoInput">Geofence Distance (m)</label>
        <input id="geoInput" type="number" min="1" max="50" step="0.5" value="8.0" />
      </div>

      <button class="btn btn-primary" onclick="setLimit(); setGeofenceDistance();">Apply Settings</button>
    </div>

    <p style="text-align: center; color: var(--text-secondary); font-size: 0.85rem; margin-top: 32px; margin-bottom: 24px;">
      🏥 Professional tele-rehabilitation panel | AI Fall Detection | Distance-based Geofence | Adaptive Therapy
    </p>
  </div>

  <script>
    let lastAlertState = false;
    let lastAlertType = 'none';
    let alertSoundTimer = null;
    let audioCtx = null;
    let featureStates = {
      fall: true,
      geofence: true,
      overload: true
    };
    const historyMax = 120;
    const history = {
      ts: [],
      weight: [],
      dyn: [],
      batt: []
    };

    const clamp = (v, min, max) => Math.max(min, Math.min(max, v));

    function pushHistory(d) {
      const now = new Date();
      history.ts.push(now.toISOString());
      history.weight.push(Number(d.weight));
      history.dyn.push(Number(d.dynAcc));
      history.batt.push(Number(d.battPct));

      if (history.ts.length > historyMax) {
        history.ts.shift();
        history.weight.shift();
        history.dyn.shift();
        history.batt.shift();
      }
    }

    function toPolylinePoints(arr, minY, maxY) {
      const w = 300;
      const h = 100;
      if (!arr.length) return '';
      const safeMin = Number.isFinite(minY) ? minY : Math.min(...arr);
      const safeMax = Number.isFinite(maxY) ? maxY : Math.max(...arr);
      const span = Math.max(0.0001, safeMax - safeMin);
      return arr.map((v, i) => {
        const x = (i / Math.max(1, arr.length - 1)) * w;
        const y = h - ((v - safeMin) / span) * h;
        return x.toFixed(1) + ',' + y.toFixed(1);
      }).join(' ');
    }

    function updateGraphs() {
      const gw = document.getElementById('gWeight');
      const gd = document.getElementById('gDyn');
      const gb = document.getElementById('gBatt');
      gw.setAttribute('points', toPolylinePoints(history.weight, 0, Math.max(10, ...history.weight, 1)));
      gd.setAttribute('points', toPolylinePoints(history.dyn, 0, Math.max(5, ...history.dyn, 1)));
      gb.setAttribute('points', toPolylinePoints(history.batt, 0, 100));
      gw.style.stroke = '#0f7dff';
      gd.style.stroke = '#f59e0b';
      gb.style.stroke = '#10b981';
    }

    function downloadExcelData() {
      if (history.ts.length === 0) {
        alert('No data available yet.');
        return;
      }
      const rows = ['Timestamp,WeightKg,DynamicAcceleration,BatteryPercent'];
      for (let i = 0; i < history.ts.length; i++) {
        rows.push(history.ts[i] + ',' + history.weight[i].toFixed(2) + ',' + history.dyn[i].toFixed(3) + ',' + history.batt[i].toFixed(1));
      }
      const csv = '\ufeff' + rows.join('\n');
      const blob = new Blob([csv], { type: 'text/csv;charset=utf-8;' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = 'orthostride_data_' + Date.now() + '.csv';
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      URL.revokeObjectURL(url);
    }

    // Audio alert system
    function beep(freq = 880, ms = 180) {
      try {
        if (!audioCtx) {
          audioCtx = new (window.AudioContext || window.webkitAudioContext)();
        }
        const osc = audioCtx.createOscillator();
        const gain = audioCtx.createGain();
        osc.type = 'square';
        osc.frequency.value = freq;
        osc.connect(gain);
        gain.connect(audioCtx.destination);
        gain.gain.value = 0.0001;
        osc.start();
        const now = audioCtx.currentTime;
        gain.gain.exponentialRampToValueAtTime(0.22, now + 0.01);
        gain.gain.exponentialRampToValueAtTime(0.0001, now + (ms / 1000));
        osc.stop(now + (ms / 1000) + 0.01);
      } catch (e) {}
    }

    function getAlertType(d) {
      if (d.overload) return 'overload';
      if (d.fall) return 'fall';
      if (d.geofence) return 'geofence';
      return 'generic';
    }

    function runAlertBeep(type) {
      if (type === 'fall') {
        beep(720, 260);
      } else if (type === 'overload') {
        beep(1020, 180);
      } else if (type === 'geofence') {
        beep(900, 220);
      } else {
        beep(820, 200);
      }
    }

    function startAlertSound(type) {
      if (alertSoundTimer && lastAlertType === type) {
        return;
      }
      stopAlertSound();
      lastAlertType = type;
      runAlertBeep(type);
      alertSoundTimer = setInterval(() => runAlertBeep(type), 480);
    }

    function stopAlertSound() {
      if (alertSoundTimer) {
        clearInterval(alertSoundTimer);
        alertSoundTimer = null;
      }
      lastAlertType = 'none';
    }

    // UI Utilities
    function showMassageValue() {
      document.getElementById('massageValue').textContent = document.getElementById('massageRange').value;
    }

    // Toggle Features with proper icon & text
    async function toggleFeature(feature) {
      featureStates[feature] = !featureStates[feature];
      const switchElement = document.getElementById('toggle' + feature.charAt(0).toUpperCase() + feature.slice(1) + 'Switch');
      switchElement.checked = featureStates[feature];
      
      await fetch('/toggle-feature?f=' + feature + '&state=' + (featureStates[feature] ? '1' : '0'), { method: 'POST' });
    }

    async function updateMassageMode() {
      const mode = document.getElementById('massageMode').value;
      await fetch('/massage/mode?m=' + encodeURIComponent(mode), { method: 'POST' });
    }

    // Main refresh function - updates all metrics
    async function refresh() {
      try {
        const r = await fetch('/data', { cache: 'no-store' });
        const d = await r.json();

        // Core metrics
        document.getElementById('weight').textContent = Number(d.weight).toFixed(2);
        document.getElementById('limit').textContent = Number(d.doctorLimit).toFixed(2);
        document.getElementById('steps').textContent = d.steps;
        document.getElementById('falls').textContent = d.falls;
        document.getElementById('cadence').textContent = Number(d.cadence).toFixed(1);
        document.getElementById('distance').textContent = Number(d.distance).toFixed(2);
        document.getElementById('calories').textContent = Number(d.calories).toFixed(2);

        // Advanced metrics
        document.getElementById('dynAcc').textContent = Number(d.dynAcc).toFixed(2);
        document.getElementById('impact').textContent = Number(d.accMag).toFixed(2);
        document.getElementById('energy').textContent = Number(d.energy).toFixed(2);
        document.getElementById('temp').textContent = Number(d.temp).toFixed(1);
        document.getElementById('gyro').textContent = Number(d.gyroMag).toFixed(2);

        // Battery & Power
        const battPct = Number(d.battPct).toFixed(1);
        document.getElementById('battPct').textContent = battPct + '%';
        document.getElementById('battVolt').textContent = Number(d.battV).toFixed(3) + 'V';
        document.getElementById('battery').textContent = Number(d.vbat).toFixed(1) + '%';
        document.getElementById('rssi').textContent = d.rssi;
        document.getElementById('geoDist').textContent = Number(d.geoDistance).toFixed(1);
        document.getElementById('geoLimit').textContent = Number(d.geoLimit).toFixed(1);
        document.getElementById('clients').textContent = d.clients;

        pushHistory(d);
        updateGraphs();

        // FSR charge (mapped from weight: 0-10kg -> 0-100%)
        const weight = Number(d.weight);
        const fsrChargePercent = clamp((weight / 10.0) * 100, 0, 100);
        document.getElementById('fsrCharge').textContent = fsrChargePercent.toFixed(1) + '%';

        // Progress bars with smooth animation
        document.getElementById('dynFill').style.width = clamp((d.dynAcc / 5.0) * 100, 0, 100) + '%';
        document.getElementById('impactFill').style.width = clamp((d.accMag / 30.0) * 100, 0, 100) + '%';
        document.getElementById('energyFill').style.width = clamp((d.energy / 50.0) * 100, 0, 100) + '%';
        document.getElementById('battVoltFill').style.width = clamp(((d.battV - 3.0) / 1.2) * 100, 0, 100) + '%';
        document.getElementById('battPctFill').style.width = clamp(d.battPct, 0, 100) + '%';
        document.getElementById('batteryFill').style.width = clamp(d.vbat, 0, 100) + '%';
        document.getElementById('fsrChargeFill').style.width = fsrChargePercent + '%';
        document.getElementById('gyroFill').style.width = clamp((d.gyroMag / 5.0) * 100, 0, 100) + '%';
        document.getElementById('stepsFill').style.width = clamp((d.steps / 1000) * 100, 0, 100) + '%';

        // Battery icon fill with charging animation
        const batteryFillIcon = document.getElementById('batteryFillIcon');
        batteryFillIcon.style.width = clamp(d.battPct, 0, 100) + '%';
        if (d.charging) {
          batteryFillIcon.classList.add('charging');
        } else {
          batteryFillIcon.classList.remove('charging');
        }

        // Status banner update
        const status = document.getElementById('status');
        status.className = 'status-banner normal';

        if (d.alert) {
          const alertType = getAlertType(d);
          startAlertSound(alertType);
          status.className = 'status-banner alert';
          
          if (d.overload) {
            status.innerHTML = '<span class="status-icon">⚠️</span><span class="status-text">OVERLOAD ALERT: Reduce weight on foot now</span>';
          } else if (d.fall) {
            status.innerHTML = '<span class="status-icon">🚨</span><span class="status-text">FALL ALERT: Possible fall detected</span>';
          } else if (d.geofence) {
            status.innerHTML = '<span class="status-icon">📍</span><span class="status-text">GEOFENCE ALERT: Device moved outside configured distance</span>';
          }
          
          if (!lastAlertState) {
            alert(status.innerText);
          }
        } else if (d.massage) {
          stopAlertSound();
          status.className = 'status-banner warning';
          status.innerHTML = '<span class="status-icon">💆</span><span class="status-text">Massage therapy mode running</span>';
        } else {
          stopAlertSound();
          status.className = 'status-banner normal';
          if (d.charging) {
            status.innerHTML = '<span class="status-icon">⚡</span><span class="status-text">Walking energy harvesting detected (charging demo)</span>';
            status.className = 'status-banner charging';
          } else {
            status.innerHTML = '<span class="status-icon">✓</span><span class="status-text">Normal rehabilitation activity</span>';
          }
        }

        lastAlertState = d.alert;
      } catch (e) {
        const status = document.getElementById('status');
        status.className = 'status-banner warning';
        status.innerHTML = '<span class="status-icon">⏳</span><span class="status-text">Waiting for device data...</span>';
      }
    }

    // Action handlers
    async function ackAlert() {
      await fetch('/ack-alert', { method: 'POST' });
      lastAlertState = false;
      refresh();
    }

    async function toggleMassage() {
      await fetch('/massage/toggle', { method: 'POST' });
      refresh();
    }

    async function setMassageIntensity() {
      const v = parseInt(document.getElementById('massageRange').value || '150', 10);
      await fetch('/massage/intensity?duty=' + encodeURIComponent(v), { method: 'POST' });
      refresh();
    }

    async function setGeofenceDistance() {
      const v = parseFloat(document.getElementById('geoInput').value || '0');
      await fetch('/set-geofence?m=' + encodeURIComponent(v), { method: 'POST' });
      refresh();
    }

    async function resetStats() {
      await fetch('/reset', { method: 'POST' });
      refresh();
    }

    async function setLimit() {
      const v = parseFloat(document.getElementById('limitInput').value || '0');
      await fetch('/set-limit?kg=' + encodeURIComponent(v), { method: 'POST' });
      refresh();
    }

    async function startCal() {
      await fetch('/cal/start', { method: 'POST' });
      alert('Calibration mode requested. Complete steps in Serial Monitor.');
    }

    // Initialize and start refresh loop
    setInterval(refresh, 800);
    showMassageValue();
    
    // Resume audio context on first user interaction
    document.addEventListener('click', () => {
      if (audioCtx && audioCtx.state === 'suspended') {
        audioCtx.resume();
      }
    });
    
    refresh();
  </script>
</body>
</html>
)rawliteral";

float magnitude3(float x, float y, float z) {
  return sqrtf(x * x + y * y + z * z);
}

float rssiToDistanceMeters(int rssiDbm) {
  if (rssiDbm <= -126) {
    return geofenceDistanceLimitM + 99.0f;
  }
  return powf(10.0f, (rssiAt1m - (float)rssiDbm) / (10.0f * pathLossExponent));
}

int readStrongestClientRssi() {
  wifi_sta_list_t staList;
  if (esp_wifi_ap_get_sta_list(&staList) != ESP_OK || staList.num <= 0) {
    return -127;
  }

  int best = -127;
  for (int i = 0; i < staList.num; i++) {
    if (staList.sta[i].rssi > best) {
      best = staList.sta[i].rssi;
    }
  }
  return best;
}

void addStepTimestamp(unsigned long t) {
  stepTimestamps[stepTsIndex] = t;
  stepTsIndex = (stepTsIndex + 1) % 20;
}

void updateCadence(unsigned long nowMs) {
  const unsigned long windowMs = 10000;
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
  harvestedEnergymWh = 0.0f;
  for (uint8_t i = 0; i < 20; i++) {
    stepTimestamps[i] = 0;
  }
  stepTsIndex = 0;
}

void saveCalibration() {
  int addr = CALIBRATION_ADDR;
  EEPROM.write(addr++, CAL_SIGNATURE);
  for (int i = 0; i < NUM_WEIGHTS; i++) {
    EEPROM.put(addr, calibrationValues[i]);
    addr += sizeof(int);
  }
  EEPROM.commit();
}

void loadCalibration() {
  int addr = CALIBRATION_ADDR;
  byte sig = EEPROM.read(addr++);
  if (sig == CAL_SIGNATURE) {
    for (int i = 0; i < NUM_WEIGHTS; i++) {
      EEPROM.get(addr, calibrationValues[i]);
      addr += sizeof(int);
    }
    isCalibrated = true;
    Serial.println("Calibration loaded from EEPROM.");
  } else {
    isCalibrated = false;
    Serial.println("No calibration found. Use command '1' to calibrate.");
  }
}

void clearCalibration() {
  EEPROM.write(CALIBRATION_ADDR, 0x00);
  EEPROM.commit();
  isCalibrated = false;
}

void performCalibration() {
  Serial.println("\n=== FSR CALIBRATION MODE ===");
  Serial.println("For each step place exact weight and press any key.");

  for (int i = 0; i < NUM_WEIGHTS; i++) {
    Serial.print("Place ");
    Serial.print(calibrationWeights[i], 1);
    Serial.println(" kg, then press any key...");

    while (Serial.available() == 0) {
      delay(60);
    }
    while (Serial.available() > 0) {
      Serial.read();
    }

    delay(900);
    long sum = 0;
    for (int j = 0; j < NUM_SAMPLES; j++) {
      sum += analogRead(FSR_PIN);
      delay(35);
    }
    calibrationValues[i] = sum / NUM_SAMPLES;

    Serial.print("Captured ADC for ");
    Serial.print(calibrationWeights[i], 1);
    Serial.print(" kg -> ");
    Serial.println(calibrationValues[i]);
  }

  saveCalibration();
  isCalibrated = true;
  Serial.println("Calibration complete and saved.");
}

int getSmoothedReading(int newReading) {
  long sum = 0;
  int count = 0;

  for (int i = 0; i < MOVING_AVG_SIZE; i++) {
    sum += fsrBuffer[i];
    if (fsrBuffer[i] != 0 || fsrBufferFilled) {
      count++;
    }
  }

  int currentAvg = (count > 0) ? (sum / count) : fsrLastValidReading;

  if (newReading < ZERO_THRESHOLD && currentAvg > 50) {
    fsrStableCount = 0;
    return fsrLastValidReading;
  }

  if (count > 0 && currentAvg > 0 && abs(newReading - currentAvg) > OUTLIER_THRESHOLD) {
    fsrStableCount = 0;
    return currentAvg;
  }

  fsrBuffer[fsrBufferIndex] = newReading;
  fsrBufferIndex = (fsrBufferIndex + 1) % MOVING_AVG_SIZE;
  if (fsrBufferIndex == 0) {
    fsrBufferFilled = true;
  }

  sum = 0;
  count = fsrBufferFilled ? MOVING_AVG_SIZE : fsrBufferIndex;
  for (int i = 0; i < count; i++) {
    sum += fsrBuffer[i];
  }

  int smoothed = (count > 0) ? (sum / count) : newReading;

  if (smoothed > ZERO_THRESHOLD) {
    fsrStableCount++;
    fsrLastValidReading = smoothed;
  } else {
    fsrStableCount = 0;
  }

  return smoothed;
}

float calculateWeight(int fsrValue) {
  if (!isCalibrated) {
    return 0.0f;
  }

  if (fsrValue <= 0) {
    return 0.0f;
  }

  if (fsrValue < calibrationValues[0]) {
    float den = (float)(calibrationValues[1] - calibrationValues[0]);
    if (fabsf(den) < 1e-5f) {
      return 0.0f;
    }
    float slope = (calibrationWeights[1] - calibrationWeights[0]) / den;
    float w = calibrationWeights[0] + slope * (fsrValue - calibrationValues[0]);
    return max(0.0f, w);
  }

  if (fsrValue >= calibrationValues[NUM_WEIGHTS - 1]) {
    int i = NUM_WEIGHTS - 1;
    float den = (float)(calibrationValues[i] - calibrationValues[i - 1]);
    if (fabsf(den) < 1e-5f) {
      return calibrationWeights[i];
    }
    float slope = (calibrationWeights[i] - calibrationWeights[i - 1]) / den;
    return calibrationWeights[i] + slope * (fsrValue - calibrationValues[i]);
  }

  for (int i = 0; i < NUM_WEIGHTS - 1; i++) {
    if (fsrValue >= calibrationValues[i] && fsrValue < calibrationValues[i + 1]) {
      float den = (float)(calibrationValues[i + 1] - calibrationValues[i]);
      if (fabsf(den) < 1e-5f) {
        return calibrationWeights[i];
      }
      float ratio = (float)(fsrValue - calibrationValues[i]) / den;
      return calibrationWeights[i] + ratio * (calibrationWeights[i + 1] - calibrationWeights[i]);
    }
  }

  return 0.0f;
}

void setupVibration() {
  ledcAttach(VIB1_PIN, pwmFreq, pwmRes);
  ledcAttach(VIB2_PIN, pwmFreq, pwmRes);
  ledcAttach(VIB3_PIN, pwmFreq, pwmRes);

  ledcWrite(VIB1_PIN, 0);
  ledcWrite(VIB2_PIN, 0);
  ledcWrite(VIB3_PIN, 0);
}

void writeVibration(uint8_t d1, uint8_t d2, uint8_t d3) {
  ledcWrite(VIB1_PIN, d1);
  ledcWrite(VIB2_PIN, d2);
  ledcWrite(VIB3_PIN, d3);
}

void updateVibration(unsigned long nowMs) {
  // Allow therapy pattern to run unless immediate overload safety alert is active.
  if (massageMode && !overloadActive) {
    // Different massage patterns based on selected mode
    if (massageMode_selected == 0) {
      // Mode 0: Basic Wave (slow rotating pattern)
      static uint8_t phase = 0;
      static unsigned long t = 0;
      if (nowMs - t >= 240) {
        phase = (phase + 1) % 3;
        t = nowMs;
      }
      if (phase == 0) writeVibration(massageIntensity, 40, 40);
      if (phase == 1) writeVibration(40, massageIntensity, 40);
      if (phase == 2) writeVibration(40, 40, massageIntensity);
    } 
    else if (massageMode_selected == 1) {
      // Mode 1: Intense Pulse (fast alternating pattern)
      static bool pulseOn = false;
      static unsigned long t = 0;
      if (nowMs - t >= 120) {
        pulseOn = !pulseOn;
        t = nowMs;
      }
      uint8_t duty = pulseOn ? massageIntensity : 40;
      writeVibration(duty, duty, duty);
    } 
    else if (massageMode_selected == 2) {
      // Mode 2: Therapeutic Beat (medical rhythm pattern)
      static uint8_t beatPhase = 0;
      static unsigned long t = 0;
      if (nowMs - t >= 150) {
        beatPhase = (beatPhase + 1) % 4;
        t = nowMs;
      }
      if (beatPhase == 0) writeVibration(massageIntensity, massageIntensity, 40);
      if (beatPhase == 1) writeVibration(40, 40, 40);
      if (beatPhase == 2) writeVibration(massageIntensity, massageIntensity, massageIntensity);
      if (beatPhase == 3) writeVibration(40, 40, 40);
    } 
    else if (massageMode_selected == 3) {
      // Mode 3: Recovery Cycle (deep tissue, all motors together)
      static bool strongPulse = false;
      static unsigned long t = 0;
      if (nowMs - t >= 300) {
        strongPulse = !strongPulse;
        t = nowMs;
      }
      uint8_t duty = strongPulse ? massageIntensity : (massageIntensity / 2);
      writeVibration(duty, duty, duty);
    }
    return;
  }

  if (alertActive) {
    static bool pulseOn = false;
    static unsigned long t = 0;
    if (nowMs - t >= 150) {
      pulseOn = !pulseOn;
      t = nowMs;
    }
    uint8_t duty = pulseOn ? 255 : 0;
    writeVibration(duty, duty, duty);
    return;
  }

  writeVibration(0, 0, 0);
}

void updateHybridEnergy(unsigned long nowMs, bool isStepOrPressureEvent) {
  if (isStepOrPressureEvent) {
    float gain = max(0.02f, latestWeightKg * 0.005f);
    harvestedEnergymWh += gain;
    virtualBatteryPct += gain * 0.04f;
    if (virtualBatteryPct > 100.0f) {
      virtualBatteryPct = 100.0f;
    }

    chargingNow = true;
    chargingUntilMs = nowMs + 1200;
  }

  if (chargingNow && nowMs > chargingUntilMs) {
    chargingNow = false;
  }

  // Small idle drain for realism
  virtualBatteryPct -= 0.0008f;
  if (virtualBatteryPct < 0.0f) {
    virtualBatteryPct = 0.0f;
  }
}

float readBatteryVoltage() {
  uint32_t sumMv = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    sumMv += analogReadMilliVolts(BATTERY_PIN);
    delay(2);
  }

  float adcMv = (float)sumMv / (float)ADC_SAMPLES;
  return (adcMv / 1000.0f) * DIVIDER_RATIO;
}

float voltageToPercent(float v) {
  if (v >= BATTERY_V_MAX) return 100.0f;
  if (v <= BATTERY_V_MIN) return 0.0f;

  if (v >= 4.10f) return 90.0f + (v - 4.10f) * 100.0f;
  if (v >= 4.00f) return 80.0f + (v - 4.00f) * 100.0f;
  if (v >= 3.90f) return 60.0f + (v - 3.90f) * 200.0f;
  if (v >= 3.80f) return 40.0f + (v - 3.80f) * 200.0f;
  if (v >= 3.70f) return 25.0f + (v - 3.70f) * 150.0f;
  if (v >= 3.60f) return 15.0f + (v - 3.60f) * 100.0f;
  if (v >= 3.50f) return 8.0f + (v - 3.50f) * 70.0f;
  if (v >= 3.40f) return 3.0f + (v - 3.40f) * 50.0f;
  return (v - 3.00f) * 7.5f;
}

void updateBatteryMonitor(unsigned long nowMs) {
  if ((nowMs - lastBatteryReadMs) < 1000) {
    return;
  }
  lastBatteryReadMs = nowMs;

  latestBatteryVoltage = readBatteryVoltage();
  latestBatteryPercent = voltageToPercent(latestBatteryVoltage);
}

void sendJsonData() {
  int clients = WiFi.softAPgetStationNum();

  String json = "{";
  json += "\"weight\":" + String(latestWeightKg, 2);
  json += ",\"doctorLimit\":" + String(doctorWeightLimitKg, 2);
  json += ",\"steps\":" + String(stepCount);
  json += ",\"falls\":" + String(fallCount);
  json += ",\"cadence\":" + String(cadenceSpm, 2);
  json += ",\"distance\":" + String(distanceMeters, 3);
  json += ",\"calories\":" + String(caloriesKcal, 3);
  json += ",\"accMag\":" + String(latestAccMag, 3);
  json += ",\"dynAcc\":" + String(latestDynAcc, 3);
  json += ",\"gyroMag\":" + String(latestGyroMag, 3);
  json += ",\"temp\":" + String(latestTempC, 2);
  json += ",\"energy\":" + String(harvestedEnergymWh, 2);
  json += ",\"battV\":" + String(latestBatteryVoltage, 3);
  json += ",\"battPct\":" + String(latestBatteryPercent, 1);
  json += ",\"vbat\":" + String(virtualBatteryPct, 1);
  json += ",\"clients\":" + String(clients);
  json += ",\"rssi\":" + String(latestClientRssi);
  json += ",\"geoDistance\":" + String(geofenceCurrentDistanceM, 2);
  json += ",\"geoLimit\":" + String(geofenceDistanceLimitM, 2);
  json += ",\"charging\":" + String(chargingNow ? "true" : "false");
  json += ",\"massage\":" + String(massageMode ? "true" : "false");
  json += ",\"massageMode\":" + String((int)massageMode_selected);
  json += ",\"massageIntensity\":" + String((int)massageIntensity);
  json += ",\"alert\":" + String(alertActive ? "true" : "false");
  json += ",\"overload\":" + String(overloadActive ? "true" : "false");
  json += ",\"fall\":" + String(fallAlertActive ? "true" : "false");
  json += ",\"geofence\":" + String(geofenceAlert ? "true" : "false");
  json += ",\"fsrCharge\":" + String((latestWeightKg / 10.0f) * 100.0f, 1);
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
    overloadActive = false;
    geofenceAlert = false;
    fallAlertActive = false;
    server.send(200, "text/plain", "ok");
  });

  server.on("/reset", HTTP_POST, []() {
    resetStats();
    alertActive = false;
    overloadActive = false;
    geofenceAlert = false;
    fallAlertActive = false;
    server.send(200, "text/plain", "ok");
  });

  server.on("/massage/toggle", HTTP_POST, []() {
    massageMode = !massageMode;
    if (massageMode) {
      // Avoid stale non-critical alerts masking therapy feedback.
      geofenceAlert = false;
      if (!overloadActive) {
        alertActive = false;
      }
    }
    server.send(200, "text/plain", massageMode ? "massage_on" : "massage_off");
  });

  server.on("/massage/intensity", HTTP_POST, []() {
    if (!server.hasArg("duty")) {
      server.send(400, "text/plain", "missing duty");
      return;
    }
    int duty = server.arg("duty").toInt();
    if (duty < 40 || duty > 255) {
      server.send(400, "text/plain", "invalid duty (40..255)");
      return;
    }
    massageIntensity = (uint8_t)duty;
    server.send(200, "text/plain", "ok");
  });

  server.on("/massage/mode", HTTP_POST, []() {
    if (!server.hasArg("m")) {
      server.send(400, "text/plain", "missing m");
      return;
    }
    int mode = server.arg("m").toInt();
    if (mode < 0 || mode > 3) {
      server.send(400, "text/plain", "invalid mode (0..3)");
      return;
    }
    massageMode_selected = (uint8_t)mode;
    Serial.print("Massage mode set to: ");
    Serial.println(massageMode_names[massageMode_selected]);
    server.send(200, "text/plain", "ok");
  });

  server.on("/toggle-feature", HTTP_POST, []() {
    if (!server.hasArg("f") || !server.hasArg("state")) {
      server.send(400, "text/plain", "missing f or state");
      return;
    }
    String feature = server.arg("f");
    bool state = server.arg("state") == "1";
    
    if (feature == "fall") {
      fallDetectionEnabled = state;
      Serial.print("Fall detection: ");
      Serial.println(fallDetectionEnabled ? "ENABLED" : "DISABLED");
    } else if (feature == "geofence") {
      geofenceEnabled = state;
      if (!state) geofenceAlert = false;
      Serial.print("Geofence: ");
      Serial.println(geofenceEnabled ? "ENABLED" : "DISABLED");
    } else if (feature == "overload") {
      overloadAlertEnabled = state;
      if (!state) overloadActive = false;
      Serial.print("Overload alert: ");
      Serial.println(overloadAlertEnabled ? "ENABLED" : "DISABLED");
    } else {
      server.send(400, "text/plain", "unknown feature");
      return;
    }
    server.send(200, "text/plain", "ok");
  });

  server.on("/set-geofence", HTTP_POST, []() {
    if (!server.hasArg("m")) {
      server.send(400, "text/plain", "missing m");
      return;
    }
    float m = server.arg("m").toFloat();
    if (m < 1.0f || m > 50.0f) {
      server.send(400, "text/plain", "invalid meters (1..50)");
      return;
    }
    geofenceDistanceLimitM = m;
    geofenceAlert = false;
    if (!overloadActive && !fallAlertActive) {
      alertActive = false;
    }
    server.send(200, "text/plain", "ok");
  });

  server.on("/set-limit", HTTP_POST, []() {
    if (!server.hasArg("kg")) {
      server.send(400, "text/plain", "missing kg");
      return;
    }
    float kg = server.arg("kg").toFloat();
    if (kg < 1.0f || kg > 40.0f) {
      server.send(400, "text/plain", "invalid kg (1..40)");
      return;
    }
    doctorWeightLimitKg = kg;
    server.send(200, "text/plain", "ok");
  });

  // Start calibration from UI, complete through Serial prompts.
  server.on("/cal/start", HTTP_POST, []() {
    server.send(200, "text/plain", "calibration_requested");
    performCalibration();
  });

  // Captive portal probes
  server.on("/generate_204", HTTP_GET, []() {
    server.sendHeader("Location", String("http://") + apIP.toString(), true);
    server.send(302, "text/plain", "");
  });
  server.on("/hotspot-detect.html", HTTP_GET, []() {
    server.sendHeader("Location", String("http://") + apIP.toString(), true);
    server.send(302, "text/plain", "");
  });
  server.on("/ncsi.txt", HTTP_GET, []() {
    server.sendHeader("Location", String("http://") + apIP.toString(), true);
    server.send(302, "text/plain", "");
  });

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
  Serial.println("  h                 -> help");
  Serial.println("  1                 -> run FSR calibration");
  Serial.println("  2                 -> show calibration values");
  Serial.println("  3                 -> clear calibration");
  Serial.println("  r                 -> reset stats");
  Serial.println("  s                 -> print status now");
  Serial.println("  c                 -> toggle CSV mode");
  Serial.println("  m                 -> toggle massage mode");
  Serial.println("  i <40..255>       -> set massage intensity");
  Serial.println("  M <0..3>          -> set massage mode (0=wave, 1=pulse, 2=beat, 3=recovery)");
  Serial.println("  g <1..50 m>       -> set geofence distance");
  Serial.println("  k <stride_m>      -> set stride length");
  Serial.println("  w <weight_kg>     -> set user weight");
  Serial.println("  l <limit_kg>      -> set doctor weight limit");
  Serial.println("  t <high> <low>    -> set step thresholds");
  Serial.println("  a                 -> auto-calibrate gravity baseline");
  Serial.println("  F <fall_0/1>      -> toggle fall detection (0=off, 1=on)");
  Serial.println("  G <geo_0/1>       -> toggle geofence (0=off, 1=on)");
  Serial.println("  O <over_0/1>      -> toggle overload alert (0=off, 1=on)");
}

void printCalibration() {
  Serial.println("\nFSR calibration table:");
  if (!isCalibrated) {
    Serial.println("Not calibrated");
    return;
  }
  for (int i = 0; i < NUM_WEIGHTS; i++) {
    Serial.print(calibrationWeights[i], 1);
    Serial.print(" kg -> ");
    Serial.println(calibrationValues[i]);
  }
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
    Serial.print(latestWeightKg, 2);
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
    Serial.print(harvestedEnergymWh, 2);
    Serial.print(',');
    Serial.print(virtualBatteryPct, 1);
    Serial.print(',');
    Serial.println(alertActive ? 1 : 0);
    return;
  }

  Serial.print("Weight: ");
  Serial.print(latestWeightKg, 2);
  Serial.print(" kg | Limit: ");
  Serial.print(doctorWeightLimitKg, 2);
  Serial.print(" | Steps: ");
  Serial.print(stepCount);
  Serial.print(" | Falls: ");
  Serial.print(fallCount);
  Serial.print(" | Cadence: ");
  Serial.print(cadenceSpm, 1);
  Serial.print(" spm | Dist: ");
  Serial.print(distanceMeters, 2);
  Serial.print(" m | Energy: ");
  Serial.print(harvestedEnergymWh, 2);
  Serial.print(" mWh | Alert: ");
  Serial.println(alertActive ? "ON" : "OFF");
}

void autoCalibrateBaseline() {
  Serial.println("Auto-calibration: stand still for 3 seconds...");
  const int samples = 150;
  float sum = 0.0f;

  for (int i = 0; i < samples; i++) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    sum += magnitude3(a.acceleration.x, a.acceleration.y, a.acceleration.z);
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
  } else if (cmd == '1') {
    performCalibration();
  } else if (cmd == '2') {
    printCalibration();
  } else if (cmd == '3') {
    clearCalibration();
    Serial.println("Calibration cleared.");
  } else if (cmd == 'r') {
    resetStats();
    Serial.println("Stats reset.");
  } else if (cmd == 's') {
    printStatus(true);
  } else if (cmd == 'c') {
    csvMode = !csvMode;
    Serial.print("CSV mode: ");
    Serial.println(csvMode ? "ON" : "OFF");
  } else if (cmd == 'm') {
    massageMode = !massageMode;
    if (massageMode) {
      geofenceAlert = false;
      if (!overloadActive) {
        alertActive = false;
      }
    }
    Serial.print("Massage mode: ");
    Serial.println(massageMode ? "ON" : "OFF");
  } else if (cmd == 'i') {
    int v;
    if (sscanf(line.c_str(), "i %d", &v) == 1 && v >= 40 && v <= 255) {
      massageIntensity = (uint8_t)v;
      Serial.print("Massage intensity set to: ");
      Serial.println((int)massageIntensity);
    } else {
      Serial.println("Usage: i <duty> (40 to 255)");
    }
  } else if (cmd == 'g') {
    float v;
    if (sscanf(line.c_str(), "g %f", &v) == 1 && v >= 1.0f && v <= 50.0f) {
      geofenceDistanceLimitM = v;
      Serial.print("Geofence distance set to: ");
      Serial.print(geofenceDistanceLimitM, 2);
      Serial.println(" m");
    } else {
      Serial.println("Usage: g <distance_m> (1 to 50)");
    }
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
      Serial.print("User weight set to: ");
      Serial.println(userWeightKg, 1);
    } else {
      Serial.println("Usage: w <weight_kg> (20 to 250)");
    }
  } else if (cmd == 'l') {
    float v;
    if (sscanf(line.c_str(), "l %f", &v) == 1 && v > 1.0f && v < 40.0f) {
      doctorWeightLimitKg = v;
      Serial.print("Doctor limit set to: ");
      Serial.println(doctorWeightLimitKg, 2);
    } else {
      Serial.println("Usage: l <limit_kg> (1 to 40)");
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
  } else if (cmd == 'M') {
    int v;
    if (sscanf(line.c_str(), "M %d", &v) == 1 && v >= 0 && v <= 3) {
      massageMode_selected = (uint8_t)v;
      Serial.print("Massage mode set to: ");
      Serial.println(massageMode_names[massageMode_selected]);
    } else {
      Serial.println("Usage: M <mode> (0=wave, 1=pulse, 2=beat, 3=recovery)");
    }
  } else if (cmd == 'F') {
    int v;
    if (sscanf(line.c_str(), "F %d", &v) == 1 && (v == 0 || v == 1)) {
      fallDetectionEnabled = (v == 1);
      Serial.print("Fall detection: ");
      Serial.println(fallDetectionEnabled ? "ENABLED" : "DISABLED");
    } else {
      Serial.println("Usage: F <0/1> (0=off, 1=on)");
    }
  } else if (cmd == 'G') {
    int v;
    if (sscanf(line.c_str(), "G %d", &v) == 1 && (v == 0 || v == 1)) {
      geofenceEnabled = (v == 1);
      Serial.print("Geofence: ");
      Serial.println(geofenceEnabled ? "ENABLED" : "DISABLED");
    } else {
      Serial.println("Usage: G <0/1> (0=off, 1=on)");
    }
  } else if (cmd == 'O') {
    int v;
    if (sscanf(line.c_str(), "O %d", &v) == 1 && (v == 0 || v == 1)) {
      overloadAlertEnabled = (v == 1);
      Serial.print("Overload alert: ");
      Serial.println(overloadAlertEnabled ? "ENABLED" : "DISABLED");
    } else {
      Serial.println("Usage: O <0/1> (0=off, 1=on)");
    }
  } else {
    Serial.println("Unknown command. Use 'h' for help.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(800);

  pinMode(BATTERY_PIN, INPUT);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  EEPROM.begin(EEPROM_SIZE);
  loadCalibration();

  for (int i = 0; i < MOVING_AVG_SIZE; i++) {
    fsrBuffer[i] = 0;
  }

  setupVibration();

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip. Check wiring.");
    while (1) {
      delay(10);
    }
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  setupCaptivePortal();

  Serial.println("\n=== OrthoStride Integrated System Ready ===");
  Serial.println("Closed-loop rehab biofeedback + tele-monitoring demo");
  Serial.println("MYOSA board: set FSR/BAT/VIB/I2C constants in sketch to match MYOSA pinout");
  printHelp();

  lastLoopMs = millis();
  lastClientSeenMs = millis();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  handleSerialCommands();

  unsigned long nowMs = millis();

  // ---------- FSR pressure ----------
  int rawFsr = analogRead(FSR_PIN);
  int smoothFsr = getSmoothedReading(rawFsr);

  if (isCalibrated && fsrStableCount >= MIN_STABLE_READINGS) {
    latestWeightKg = calculateWeight(smoothFsr);
  } else {
    latestWeightKg = 0.0f;
  }

  // Overload biofeedback
  if (latestWeightKg > doctorWeightLimitKg && overloadAlertEnabled) {
    if (overloadStartMs == 0) {
      overloadStartMs = nowMs;
    }
    if ((nowMs - overloadStartMs) >= overloadHoldMs) {
      overloadActive = true;
      alertActive = true;
    }
  } else {
    overloadStartMs = 0;
    overloadActive = false;
  }

  // ---------- MPU motion ----------
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  latestAccMag = magnitude3(a.acceleration.x, a.acceleration.y, a.acceleration.z);
  latestGyroMag = magnitude3(g.gyro.x, g.gyro.y, g.gyro.z);
  latestTempC = temp.temperature;

  gravityMagLP = gravityAlpha * gravityMagLP + (1.0f - gravityAlpha) * latestAccMag;
  latestDynAcc = fabsf(latestAccMag - gravityMagLP);

  if (latestAccMag > maxImpact) {
    maxImpact = latestAccMag;
  }

  bool stepEvent = false;

  // Step detection (hysteresis)
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
      if (stepPeakDynamic < 8.0f) {
        stepCount++;
        lastStepMs = nowMs;
        addStepTimestamp(nowMs);
        distanceMeters = stepCount * strideLengthMeters;
        caloriesKcal = (userWeightKg * (distanceMeters / 1000.0f)) * 0.75f;
        stepEvent = true;
      }
    }
  }

  updateCadence(nowMs);

  // Fall detection: free-fall -> impact -> stillness
  if (fallDetectionEnabled) {
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

    if (!fallDetected && (nowMs - lastFallConfirmedMs) > fallCooldownMs) {
      if (latestAccMag > (impactThreshold + 6.0f) && latestDynAcc > 2.7f && latestGyroMag > 1.8f) {
        fallDetected = true;
        impactDetectedMs = nowMs;
      }
    }

    if (fallDetected) {
      bool still = (latestDynAcc < 0.35f && latestGyroMag < 0.8f);
      if (still && (nowMs - impactDetectedMs) >= stillnessAfterFallMs) {
        fallCount++;
        fallDetected = false;
        fallAlertActive = true;
        alertActive = true;
        lastFallAlertMs = nowMs;
        lastFallConfirmedMs = nowMs;
        Serial.println("ALERT: FALL DETECTED");
      }
      if (!still && (nowMs - impactDetectedMs) > stillnessAfterFallMs + 1200) {
        fallDetected = false;
      }
    }
  } else {
    // Fall detection disabled - reset fall state
    fallDetected = false;
    waitingForImpact = false;
    inPotentialFreeFall = false;
    fallAlertActive = false;
  }

  // Hotspot geofence: if no client connected for long enough, raise alert.
  if (geofenceEnabled) {
    int clients = WiFi.softAPgetStationNum();
    if (clients > 0) {
      lastClientSeenMs = nowMs;
      hasSeenHotspotClient = true;
      latestClientRssi = readStrongestClientRssi();
      geofenceCurrentDistanceM = rssiToDistanceMeters(latestClientRssi);

      if (geofenceCurrentDistanceM > geofenceDistanceLimitM) {
        if (geofenceBreachStartMs == 0) {
          geofenceBreachStartMs = nowMs;
        }
        if ((nowMs - geofenceBreachStartMs) > geofenceHoldMs) {
          geofenceAlert = true;
          alertActive = true;
        }
      } else {
        geofenceBreachStartMs = 0;
        geofenceAlert = false;
        if (!overloadActive && !fallAlertActive) {
          alertActive = false;
        }
      }
    } else if (hasSeenHotspotClient && (nowMs - lastClientSeenMs) > geofenceTimeoutMs) {
      geofenceCurrentDistanceM = geofenceDistanceLimitM + 99.0f;
      geofenceAlert = true;
      alertActive = true;
    }
  } else {
    // Geofence disabled - reset geofence state
    geofenceAlert = false;
  }

  // Hybrid energy demo: pressure or steps increase virtual charge.
  bool pressureEvent = latestWeightKg > 0.6f;
  updateHybridEnergy(nowMs, stepEvent || pressureEvent);
  updateBatteryMonitor(nowMs);

  // Clear stale alerts after 60 sec if not acknowledged.
  if (alertActive && (nowMs - lastFallAlertMs) > 60000 && !overloadActive && !geofenceAlert) {
    fallAlertActive = false;
    alertActive = false;
  }

  updateVibration(nowMs);

  // Throttle console prints.
  if (fabsf(latestWeightKg - lastDisplayedWeight) > 0.15f) {
    lastDisplayedWeight = latestWeightKg;
    printStatus(true);
  } else {
    printStatus(false);
  }

  // Keep ~50 Hz loop.
  unsigned long elapsed = millis() - lastLoopMs;
  if (elapsed < 20) {
    delay(20 - elapsed);
  }
  lastLoopMs = millis();
}

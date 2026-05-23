---
publishDate: 2026-05-23T00:00:00Z
title: OrthoStride — Smart Rehabilitation Footwear for Real-Time Gait Monitoring
excerpt: OrthoStride is a MYOSA-powered smart footwear system that monitors human gait in real time using IMU and FSR pressure sensors, delivering instant haptic feedback and clinical insights for rehabilitation patients.
image: orthostride-cover.jpg
tags:
  - Healthcare
  - Wearable
  - Rehabilitation
  - IoT
  - GaitAnalysis
  - EmbeddedSystems
---

> Empowering recovery — one step at a time, with real-time gait intelligence embedded in your footwear.

---

## Acknowledgements

We, **Team 404**, thank our faculty mentor **Dr. Ranjitha Rajan** (Associate Professor, Amal Jyothi College of Engineering) for her constant guidance and support throughout the development of OrthoStride. We also thank the MYOSA Event 2026 organizers for providing a platform that bridges embedded innovation with real-world healthcare impact.

---

## Overview

OrthoStride is a smart rehabilitation footwear system that monitors and analyzes human gait in real time. Human walking patterns are critical indicators of recovery progress in patients undergoing physiotherapy following strokes, fractures, or neurological disorders. 

**The Problem:** Current rehabilitation relies on periodic clinical observation with no continuous or portable monitoring, making it difficult for patients to receive timely feedback on gait corrections.

**Our Solution:** OrthoStride addresses this gap by embedding motion sensing and pressure detection directly into footwear, enabling real-time detection of gait abnormalities and providing immediate, multi-channel feedback to patients and caregivers.

**Who It's For:** Physiotherapy patients, stroke survivors, post-fracture rehabilitation, neurological disorder patients, and caregivers who need portable, continuous gait monitoring without clinical infrastructure.

**Key Features:**

* Real-time tilt angle, step detection, and gait symmetry analysis using MPU6050 IMU (100 Hz sampling)
* Plantar pressure mapping across heel, arch, and toe zones using three FSR sensors
* Instant multi-channel haptic feedback via three vibration motors embedded in footwear
* Comprehensive analytics dashboard accessible via WiFi hotspot (192.168.4.1)
* Fall detection with impact analysis and automatic caregiver alerts
* Cadence, stride length, distance, and calorie estimation with live display
* Calibration-based weight estimation with ±0.2 kg accuracy
* Battery monitoring and energy-efficient operation on MYOSA Motherboard
* Serial command interface for threshold configuration and data export

---

## Demo / Examples

### **Images**

<p align="center">
  <img src="/orthostride-cover.jpg" width="800"><br/>
  <i>OrthoStride smart footwear with MYOSA motherboard and multi-sensor array embedded in the insole</i>
</p>

<p align="center">
  <img src="/orthostride-app-dashboard.jpg" width="800"><br/>
  <i>OrthoStride Dashboard — Live display of tilt angle, step count, cadence, fall detection status, and real-time pressure heatmap across heel, arch, and toe zones</i>
</p>

<p align="center">
  <img src="/orthostride-block-diagram.jpg" width="800"><br/>
  <i>System Architecture: FSR Sensors + MPU6050 IMU → MYOSA Processing → Real-time Feature Extraction → Anomaly Detection → Multi-Modal Feedback & WiFi Dashboard</i>
</p>

### **Videos**

<video controls width="100%">
  <source src="/orthostride-demo.mp4" type="video/mp4">
</video>

---

## Features (Detailed)

### **1. Real-Time Gait Analysis (MYOSA Motherboard + MPU6050 IMU)**

The MYOSA motherboard communicates with the MPU6050 IMU sensor via I2C protocol, continuously acquiring 3-axis accelerometer and gyroscope data at 100 Hz. On-board algorithms compute:

- **Tilt Angle Calculation** — Using formula: `θ = arctan(Ax / √(Ay² + Az²))` to detect postural deviations
- **Step Detection** — Peak detection on Z-axis acceleration with sliding-window threshold and 250 ms debounce
- **Cadence Estimation** — Steps per minute calculated from inter-step intervals, updated continuously
- **Fall Detection** — Combined free-fall detection (threshold: 3.0 m/s²) + impact magnitude analysis (threshold: 24.0 m/s²) with 1200 ms detection window
- **Stability Index** — Variance-based metric computed over rolling window of gyroscope data

A **low-pass Butterworth filter** (cutoff: 5 Hz) is applied before feature extraction to suppress 50/60 Hz electrical noise and high-frequency sensor artifacts.

### **2. Plantar Pressure Mapping & Weight Estimation (FSR Sensors)**

Three FSR (Force Sensitive Resistor) sensors placed at **heel**, **midfoot (arch)**, and **toe** regions measure real-time foot pressure distribution (12-bit ADC, 0–4095 range).

- **Calibration Mode** — Factory calibration with known weights (2, 3, 5, 7, 8, 10 kg) stored in EEPROM for accurate load estimation
- **Dynamic Filtering** — 20-sample moving average with outlier rejection (threshold: 220 ADC units) and zero-baseline compensation
- **Weight Estimation** — Piecewise linear interpolation from calibration curve to output weight in kg with ±0.2 kg accuracy
- **Overload Detection** — Alerts when pressure exceeds doctor-set threshold (default: 8.0 kg) sustained for >700 ms

This complementary dataset enables detection of abnormal loading patterns (over-pronation, toe-walking, limping, antalgic gait).

### **3. Multi-Channel Feedback System**

When a gait anomaly is detected, the system provides immediate multi-modal feedback to the patient and nearby caregivers:

- **Vibration Motors (×3)** — Haptic feedback patterns vary by anomaly type:
  - Overload: rapid pulses on affected zone
  - Imbalance: alternating side-to-side patterns
  - Fall risk: strong double-pulse alert
  - Positioned at heel, arch, and toe for spatial localization
- **LED Indicators** — Visual alerts for caregivers visible in room-lighting conditions
- **Buzzer (5V)** — Audible alert (80 dB) triggered on unsafe gait events (fall-risk posture, free-fall detection)
- **WiFi Dashboard** — Real-time metrics and control interface accessible via captive portal at 192.168.4.1 (no app installation needed)

### **4. Analytics & Reporting Engine**

Comprehensive gait metrics are computed in real time and logged for clinical review:

- **Step Count & Cadence** — Cumulative steps and SPM (steps per minute), updated every 10 Hz
- **Distance Estimation** — Based on calibrated stride length (default: 0.70 m), accounting for pace variations
- **Calorie Estimation** — Using Mifflin-St Jeor formula combined with activity duration and user weight
- **Fall Events** — Timestamp, impact magnitude, and recovery time logged and exported
- **Session Summary** — Auto-generated reports with statistics exportable via EXPORT command over serial

Session data persists in EEPROM and can be exported for clinical analysis.

### **5. Processing Pipeline**

```
┌─────────────────────────┐
│ FSR Sensors + MPU6050   │
│     (100 Hz input)      │
└────────────┬────────────┘
             ↓
     ┌───────────────┐
     │  MYOSA Board  │
     │  Main Loop    │
     │   (10 Hz)     │
     └───────┬───────┘
             ↓
    ┌────────────────────┐
    │  Low-Pass Filter   │
    │  (Butterworth 5Hz) │
    └────────┬───────────┘
             ↓
  ┌──────────────────────────┐
  │ Feature Extraction       │
  │ (step, fall, tilt,       │
  │  pressure, cadence)      │
  └────────┬─────────────────┘
           ↓
  ┌──────────────────────────┐
  │ Anomaly Detection &      │
  │ Thresholding             │
  └────────┬─────────────────┘
           ↓
┌──────────────────────────────────┐
│ Multi-Modal Feedback             │
│ • Haptic (3 vibrators)           │
│ • Visual (LED)                   │
│ • Audible (Buzzer)               │
│ • WiFi Dashboard                 │
└──────────────────────────────────┘
           ↓
┌──────────────────────────────────┐
│ Session Logging & Analytics      │
│ (EEPROM storage, serial export)  │
└──────────────────────────────────┘
```

---

## Usage Instructions

### **Hardware Setup**

1. **Wiring (MYOSA Motherboard)**
   - FSR Sensor (Heel) → A0 (ADC input)
   - FSR Sensor (Arch) → A1 (ADC input)
   - FSR Sensor (Toe) → A2 (ADC input)
   - Battery Monitor → A3 (ADC input)
   - MPU6050 SDA → MYOSA I2C SDA pin
   - MPU6050 SCL → MYOSA I2C SCL pin
   - Vibration Motor 1 → GPIO Pin 2
   - Vibration Motor 2 → GPIO Pin 3
   - Vibration Motor 3 → GPIO Pin 4
   - LED Indicators → GPIO Pins 5, 6
   - Buzzer → GPIO Pin 7
   - Ground and 3.3V power distribution to all components

2. **Sensor Placement**
   - Place FSR sensors inside the insole at heel, arch (midfoot), and toe positions
   - Mount MPU6050 on the top/side of footwear for optimal motion sensing without interference
   - Secure vibration motors at corresponding foot zones (one near heel, one near arch, one near toe)
   - Ensure all connections are shielded and protected from moisture

3. **Power**
   - Connect 1000 mAh Li-Po battery to MYOSA motherboard
   - Enable battery charging circuit; typical charge time: 1.5 hours
   - Battery provides approximately 4–6 hours of continuous monitoring

### **Firmware Installation**

1. **Prerequisites**
   ```bash
   pip install esptool pyserial
   ```

2. **Compile Firmware**
   - Open `firmware/OrthoStride_Integrated_MYOSA.ino` in Arduino IDE or PlatformIO
   - Add required libraries via Library Manager:
     - Adafruit_MPU6050 v2.2.0
     - Adafruit_Sensor v1.1.4
   - Select board: MYOSA Motherboard / ESP32-S3 variant
   - Compile and verify (no errors)

3. **Flash Firmware**
   ```bash
   python -m esptool --chip esp32s3 --port COM3 write_flash 0x0000 OrthoStride_Integrated_MYOSA.bin
   ```
   Replace `COM3` with your board's COM port (Windows) or `/dev/ttyUSB0` (Linux).

4. **Calibration (First Run)**
   - Open Serial Monitor at 115200 baud
   - Device will prompt: "Enter calibration mode? (Y/N)"
   - Press 'Y' and follow on-screen instructions
   - Sequentially place known weights on the foot sensor: 2 kg, 3 kg, 5 kg, 7 kg, 8 kg, 10 kg
   - Wait for stabilization (~5 seconds) between each weight
   - Device stores calibration curve in EEPROM automatically

### **Starting a Monitoring Session**

1. **Serial Monitor Interface**
   ```bash
   python -m serial.tools.miniterm COM3 115200
   ```
   View real-time sensor data, step counts, fall alerts, and debug information.

2. **WiFi Hotspot & Dashboard**
   - Device automatically broadcasts: `OrthoStride-XXXX` (where XXXX is device ID)
   - On your phone/laptop, connect to this WiFi network (password: orthostride)
   - Open browser to `http://192.168.4.1`
   - View live metrics: tilt angle, cadence, step count, pressure distribution
   - No mobile app installation required — works on any browser

3. **Bluetooth Streaming** (Optional, for future app integration)
   - Enable Bluetooth pairing from device setup menu
   - Pair with smartphone via standard Bluetooth
   - Stream live gait data to future companion app

### **Configuration Commands (Serial Interface)**

Type these commands in the serial monitor and press Enter:

```plaintext
# Set maximum safe weight (kg) — triggers haptic alert if exceeded for >700ms
SETWEIGHT:8.0

# Configure step detection thresholds (high threshold, low threshold)
STEPTHR:1.2,0.45

# Enter calibration mode interactively
CALIBRATE

# Reset session counters (steps, distance, calories, falls) to zero
RESET

# Print session summary and export to SD card (if available)
EXPORT

# Display help menu with all available commands
HELP
```

**Example Session Flow:**
```plaintext
> SETWEIGHT:7.5
✓ Weight limit set to 7.5 kg

> STEPTHR:1.15,0.50
✓ Step thresholds updated

> RESET
✓ Session cleared — step counter: 0, fall count: 0

[10 minutes of walking...]

> EXPORT
Session Summary:
  Steps: 847
  Cadence: 105 SPM
  Distance: 592 m
  Calories: 28 kcal
  Falls: 0
  Overload Events: 3
  Session Duration: 10:23 min
✓ Data exported to serial + EEPROM
```

---

## Tech Stack

* **MYOSA Motherboard** — Core processing unit (microcontroller + WiFi/Bluetooth chip)
* **MPU6050 IMU** — 3-axis accelerometer (±8G range) + 3-axis gyroscope (±500°/s) via I2C
* **FSR (Force Sensitive Resistor) Sensors ×3** — Analog pressure sensing at heel, arch, toe (12-bit ADC)
* **Embedded C / Arduino Framework** — Firmware for real-time signal processing and gait analysis
* **EEPROM Storage** — On-board non-volatile memory for calibration curves and session data
* **WiFi 802.11b/g/n** — Real-time dashboard and wireless data streaming
* **Bluetooth 5.0** — Future mobile app connectivity and remote monitoring
* **Haptic Feedback System** — Vibration motors for patient alerts
* **Li-Po Battery Management** — Rechargeable 1000 mAh power source with integrated charging circuit

---

## Requirements / Installation

### **Firmware Dependencies**

```bash
pip install esptool pyserial
```

### **Arduino Libraries** (Install via Arduino IDE → Sketch → Include Library → Manage Libraries)

```
- Adafruit_MPU6050 v2.2.0      (for IMU communication)
- Adafruit_Sensor v1.1.4        (sensor abstraction layer)
- Wire (built-in)               (I2C communication)
- WiFi (built-in)               (WiFi hotspot)
- WebServer (built-in)          (HTTP dashboard)
- DNSServer (built-in)          (captive portal)
```

### **Hardware Bill of Materials**

| Component | Quantity | Part Number / Source | Purpose |
|-----------|----------|----------------------|---------|
| MYOSA Motherboard | 1 | MYOSA Mini IoT Kit | Central processing & WiFi/BT |
| MPU6050 IMU Module | 1 | Included in MYOSA Kit | Accelerometer + Gyroscope |
| FSR Sensors (Model 402) | 3 | Generic / Sparkfun | Plantar pressure at 3 zones |
| Vibration Motors (3V) | 3 | Generic 1027 motor | Haptic feedback actuators |
| Li-Po Battery (1000 mAh) | 1 | 3.7V single cell | Portable power |
| LED Indicators (3mm) | 2 | Any color | Visual caregiver alerts |
| Buzzer (5V piezo) | 1 | Generic 85 dB | Audible alerts |
| Resistors (10 kΩ, 1/4W) | 3 | Generic 1/4W carbon film | FSR biasing & ADC circuit |
| Micro USB Cable | 1 | Standard | Charging & serial connection |
| Insole / Shoe Insert | 1 | EVA foam | Sensor mounting substrate |

### **Recommended Development Environment**

- **Arduino IDE 1.8.13+** or **PlatformIO** (extension for VS Code)
- **Python 3.8+** with pip
- **Serial Terminal** (built-in miniterm or Putty)
- **Web Browser** (any modern browser for dashboard)

---

## File Structure

```
/orthostride
├── orthostride.md                          # MYOSA submission document
├── README.md                               # GitHub project overview
├── orthostride-cover.jpg                   # Project cover image
├── orthostride-app-dashboard.jpg           # Dashboard interface screenshot
├── orthostride-block-diagram.jpg           # System architecture diagram
├── orthostride-demo.mp4                    # Demo video
│
├── firmware/
│   ├── OrthoStride_Integrated_MYOSA.ino    # Main integrated firmware (primary)
│   ├── Footwear_Step_Fall_Detection.ino    # Step & fall detection module
│   ├── Battery_Percentage_Captive_Portal.ino
│   ├── fsr_basic_test.ino                  # FSR sensor basic test
│   ├── fsr_diagnostic_test.ino             # FSR advanced diagnostics
│   ├── FSR_NoCalibration_Direct.ino        # Raw ADC output test
│   ├── FSR_Weight_Calibration.ino          # Calibration utility
│   └── Gradual_Vibration_Control.ino       # Vibration pattern testing
│
└── assets/
    └── docs/
        ├── ASSEMBLY_GUIDE.md               # Hardware assembly instructions
        ├── CALIBRATION_GUIDE.md            # Step-by-step calibration
        └── TROUBLESHOOTING.md              # Common issues & solutions
```

---

## License

This project is submitted under the **MIT License**. All hardware designs, firmware code, and documentation are open-source and may be freely used, modified, and distributed with attribution to Team 404, Amal Jyothi College of Engineering.

```
MIT License

Copyright (c) 2026 Team 404 - Amal Jyothi College of Engineering

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## Contribution Notes

We welcome contributions from the open-source and rehabilitation engineering community. To contribute:

1. **Fork** this repository on GitHub
2. **Create a feature branch** (`git checkout -b feature/amazing-feature`)
3. **Commit your changes** with clear, descriptive messages
4. **Push to your fork** and open a **Pull Request** with detailed description of changes
5. **Request review** from team leads

### **Areas for Contribution**

- **AI/ML-based Gait Classification** — Integrate machine learning models for improved anomaly detection and personalization
- **Sensor Fusion** — Add complementary sensors (barometer for stairs/elevation, temperature compensation, humidity)
- **Mobile App** — Develop cross-platform companion app (Flutter/React Native) for patient/caregiver monitoring
- **Cloud Integration** — Enable cloud storage for longitudinal patient data analytics and telehealth
- **UI/UX Improvements** — Enhance captive portal dashboard with responsive design and accessibility features
- **Documentation** — Expand troubleshooting guides, video tutorials, and hardware assembly instructions
- **Performance Optimization** — Reduce power consumption for extended battery life
- **Additional Gait Modes** — Support stair climbing, ramp walking, sit-to-stand transitions

### **Reporting Issues & Suggestions**

- Open a GitHub issue with clear title and detailed description
- Include hardware configuration, firmware version, and reproduction steps
- Attach log files, error screenshots, or serial output for debugging
- For security vulnerabilities, please email team404@ajce.in

---

**Team 404 — Amal Jyothi College of Engineering, Kanjirappally, Kerala, India**

**IEEE MYOSA Event 2026 — Smart Rehabilitation Wearable System**

Contact: geomathew1212@gmail.com | Faculty Advisor: Dr. Ranjitha Rajan

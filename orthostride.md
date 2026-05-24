---
publishDate: 2026-05-17T00:00:00Z

title: OrthoStride — Smart Rehabilitation Footwear for Real-Time Gait Monitoring

excerpt: OrthoStride is a MYOSA-powered smart footwear system that monitors human gait in real time using IMU and FSR pressure sensors, delivering instant haptic feedback and clinical insights for rehabilitation patients.

image: orthostride-cover.jpeg

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

We, **Team 404**, thank our faculty mentor **Dr. Ranjitha Rajan** (Associate Professor, Amal Jyothi College of Engineering) for her constant guidance and support throughout the development of OrthoStride. We also thank the **IEEE MYOSA Event 2026** organizers for providing a platform that bridges embedded innovation with real-world healthcare impact.

---

## Overview

OrthoStride is a smart rehabilitation footwear system built on the **MYOSA platform** that monitors and analyzes human gait in real time. Human walking patterns are critical indicators of recovery progress in patients undergoing physiotherapy following strokes, fractures, or neurological disorders. Current rehabilitation relies on periodic clinical observation with no continuous or portable monitoring.

OrthoStride addresses this gap by embedding motion sensing and pressure detection directly into footwear, enabling real-time detection of gait abnormalities and providing immediate, multi-channel feedback to patients and caregivers.

**Key features:**

- Real-time tilt angle, step detection, and gait symmetry analysis using the MPU6050 IMU
- Plantar pressure mapping across heel, arch, and toe zones using FSR sensors
- Instant multi-channel haptic feedback via vibration motors embedded in the footwear
- Comprehensive analytics dashboard for doctors, physiotherapists, and caregivers
- Wireless Bluetooth/WiFi connectivity for real-time data streaming
- Fall detection with impact analysis and automatic alert system
- Cadence, stride length, distance, and calorie estimation
- Battery monitoring and energy-efficient operation

---

## Demo / Examples

### Images

<p align="center">
  <img src="/orthostride-cover.jpeg" width="800"><br/>

  <i>OrthoStride smart footwear with MYOSA board and multi-sensor array</i>
</p>

<p align="center">
  <img src="/orthostride-app-dashboard.jpeg" width="800"><br/>
<img src="/orthostride-dashboard-2.jpeg" width="800"><br/>
  <img src="/orthostride-dashboard-3.jpeg" width="800"><br/>
  <i>OrthoStride Dashboard — Live display of tilt angle, step count, cadence, fall detection, and pressure heatmap</i>
</p>

<p align="center">
  <img src="/orthostride-block-diagram.jpg" width="800"><br/>
  <i>System Architecture: Sensors → MYOSA Board → Real-time Feedback and Clinical Insights</i>
</p>

### Videos

**Presentation**

<video controls width="100%">
  <source src="/orthostride-presentation.mp4" type="video/mp4">
</video>

**Demonstration**

<video controls width="100%">
  <source src="/orthostride-demo.mp4" type="video/mp4">
</video>

---

## Features (Detailed)

### 1. Real-Time Gait Analysis (MYOSA Board + MPU6050 IMU)

The MYOSA Motherboard communicates with the MPU6050 IMU sensor via I2C protocol, continuously acquiring 3-axis accelerometer and gyroscope data at 100 Hz. On-board algorithms compute:

- **Tilt Angle** — using the formula: `θ = arctan(Ax / √(Ay² + Az²))`
- **Step Detection** — peak detection on the vertical acceleration signal (Z-axis) using a sliding-window threshold with debounce
- **Cadence Estimation** — steps per minute calculated from inter-step intervals
- **Fall Detection** — combined free-fall detection and impact analysis with configurable thresholds
- **Stability Index** — variance-based metric computed over a rolling window of gyroscope data

A **low-pass Butterworth filter** (cutoff: 5 Hz) is applied before feature extraction to suppress noise and high-frequency sensor artifacts.

### 2. Plantar Pressure Mapping and Weight Estimation (FSR Sensors)

Three FSR (Force Sensitive Resistor) sensors placed at **heel**, **midfoot (arch)**, and **toe** regions measure real-time foot pressure distribution:

- **Calibration Mode** — piecewise linear calibration with known weights (2–10 kg) stored in EEPROM for accurate load estimation
- **Dynamic Filtering** — moving average (20 samples) with outlier rejection and zero-baseline compensation
- **Weight Estimation** — interpolated from calibration curve, outputting weight in kg
- **Overload Detection** — alert triggered when pressure exceeds doctor-set limit (default: 8.0 kg) for more than 700 ms

This dataset enables detection of abnormal loading patterns such as over-pronation, toe-walking, and limping.

### 3. Multi-Channel Feedback System

When a gait anomaly is detected, the system responds immediately:

- **Vibration Motors (×3)** — haptic feedback patterns vary by anomaly type; positioned at heel, arch, and toe for spatial localization directly on the patient's foot
- **LED Indicators** — visual alerts visible to nearby caregivers for imbalance or asymmetry events
- **Buzzer** — audible alert triggered on unsafe gait events such as fall-risk posture or free-fall
- **WiFi Captive Portal Dashboard** — live metrics and threshold configuration accessible from any browser at 192.168.4.1

### 4. Analytics and Reporting Engine

Comprehensive gait metrics are computed in real time and logged per session:

- **Step Count and Cadence** — cumulative steps and steps per minute (SPM)
- **Distance Estimation** — based on calibrated stride length (default: 0.70 m)
- **Calorie Estimation** — using activity duration, weight, and cadence data
- **Fall Event Log** — timestamp, impact magnitude, and recovery time for every fall
- **Session Summary** — auto-generated report with full statistics exportable via serial interface

### 5. Processing Pipeline

```plaintext
FSR Sensors + MPU6050 IMU
        ↓
MYOSA Board Main Loop (10 Hz)
        ↓
Low-Pass Filter (5 Hz cutoff)
        ↓
Feature Extraction (step, fall, tilt, pressure)
        ↓
Anomaly Detection and Thresholding
        ↓
Haptic / LED / Buzzer / WiFi Dashboard Alerts
        ↓
Session Logging and Analytics Export
```

---

## Usage Instructions

### Hardware Setup

Wiring (MYOSA Board):

```plaintext
GPIO3  (ADC) ← FSR Sensor (heel)
GPIO4  (ADC) ← Battery Monitor
GPIO6  (SDA) ← MPU6050 I2C Data
GPIO7  (SCL) ← MPU6050 I2C Clock
GPIO8        → Vibration Motor 3 (toe)
GPIO9        → Vibration Motor 2 (arch)
GPIO10       → Vibration Motor 1 (heel)
3.3V, GND   → Common power rail
```

Sensor Placement:

- Place FSR sensors inside the insole at heel, arch, and toe positions
- Mount MPU6050 on top of the footwear for optimal motion capture
- Secure vibration motors at corresponding foot zones for haptic feedback

### Firmware Installation

Install dependencies:

```bash
pip install esptool pyserial
```

Flash firmware:

```bash
python -m esptool --chip esp32c3 --port COM3 write_flash 0x0000 OrthoStride_Integrated_ESP32C3.bin
```

Replace COM3 with your port (/dev/ttyUSB0 on Linux/Mac).

### Calibration (First Run)

```bash
python -m serial.tools.miniterm COM3 115200
```

Follow on-screen prompts and place known weights (2, 3, 5, 7, 8, 10 kg) on the foot sensor. Calibration saves automatically to EEPROM.

### Starting a Monitoring Session

Via Serial Monitor:

```bash
python -m serial.tools.miniterm COM3 115200
```

Via WiFi Dashboard:

```plaintext
1. Power on the device
2. Connect to WiFi: OrthoStride-XXXX
3. Open browser → http://192.168.4.1
4. View live gait metrics and configure thresholds
```

### Serial Configuration Commands

```plaintext
SETWEIGHT:8.0     → Set overload weight limit in kg
STEPTHR:1.2,0.45  → Set step detection high/low thresholds
CALIBRATE         → Enter FSR calibration mode
RESET             → Reset current session data
EXPORT            → Print session summary to serial output
```

---

## Tech Stack

- **MYOSA Motherboard** — Central processing unit running all gait algorithms
- **MPU6050 IMU** — 3-axis accelerometer and gyroscope via I2C at 100 Hz
- **FSR Sensors (×3)** — Analog force-sensitive resistors for plantar pressure
- **Embedded C / Arduino** — Firmware for real-time signal processing and gait analysis
- **EEPROM** — On-board calibration and session data persistence
- **WiFi / Bluetooth** — Wireless data streaming and captive portal dashboard
- **Vibration Motors (×3)** — Haptic feedback actuators for spatial anomaly alerts
- **Li-Po Battery (1000 mAh)** — Rechargeable portable power with monitoring circuit

---

## Requirements / Installation

### Firmware Dependencies

```bash
pip install esptool pyserial
```

### Arduino Libraries

```plaintext
Adafruit_MPU6050 v2.2.0
Adafruit_Sensor v1.1.4
Wire          (built-in)
WiFi          (built-in)
WebServer     (built-in)
DNSServer     (built-in)
```

Install via Arduino IDE: Sketch → Include Library → Manage Libraries

### Hardware Components

| Component | Quantity | Purpose |
|---|---|---|
| MYOSA Motherboard | 1 | Main processing unit |
| MPU6050 IMU Module | 1 | Accelerometer and gyroscope |
| FSR Sensors | 3 | Heel, arch, toe pressure |
| Vibration Motors | 3 | Haptic feedback actuators |
| Li-Po Battery 1000 mAh | 1 | Portable power supply |
| Resistors 10 kΩ | 3 | ADC voltage divider circuit |
| LED Indicators | 2 | Visual caregiver alerts |
| Buzzer 5V | 1 | Audio alarm output |

---

## File Structure

```plaintext
/orthostride
  ├── orthostride.md
  ├── README.md
  ├── orthostride-cover.jpg
  ├── orthostride-app-dashboard.jpg
  ├── orthostride-block-diagram.jpg
  ├── orthostride-demo.mp4
  ├── orthostride-presentation.mp4
  └── firmware/
      ├── OrthoStride_Integrated_ESP32C3.ino
      ├── Footwear_Step_Fall_Detection.ino
      ├── Battery_Percentage_Captive_Portal.ino
      ├── fsr_basic_test.ino
      ├── fsr_diagnostic_test.ino
      ├── FSR_NoCalibration_Direct.ino
      ├── FSR_Weight_Calibration.ino
      └── Gradual_Vibration_Control.ino
```

---

## License

This project is released under the **MIT License**. All hardware designs, firmware, and documentation are open-source and may be freely used, modified, and distributed with attribution to Team 404, Amal Jyothi College of Engineering, Kanjirappally, Kerala, India.

---

## Contribution Notes

To contribute to OrthoStride:

1. Fork this repository
2. Create a feature branch: `git checkout -b feature/your-feature`
3. Commit your changes with clear messages
4. Open a Pull Request with a description of what you changed and why

For bug reports, open an issue and include your hardware setup, firmware version, and any serial log output.

---

**Team 404 — Amal Jyothi College of Engineering, Kanjirappally, Kerala, India**
IEEE MYOSA Event 2026

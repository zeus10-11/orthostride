# OrthoStride — Smart Rehabilitation Footwear

**Real-Time Gait Monitoring & Rehabilitation Analytics for Wearable Footwear**

![OrthoStride](orthostride-demo.mp4)

## 🏥 Overview

OrthoStride is an advanced wearable system that monitors human gait in real time for rehabilitation and physiotherapy applications. Using embedded IMU and pressure sensors on the MYOSA motherboard (MYOSA MPU / MPU6050-compatible IMU), it detects gait abnormalities, provides haptic feedback, and generates clinical-grade analytics for patients recovering from strokes, fractures, or neurological disorders.

**Problem Solved:** Current rehabilitation relies on periodic clinical observation with no continuous monitoring. OrthoStride enables 24/7 portable gait monitoring with instant feedback.

## ✨ Key Features

- 🎯 **Real-Time Gait Analysis** — Tilt angle, step detection, cadence, and symmetry tracking using MPU6050 IMU
- 👣 **Plantar Pressure Mapping** — 3-zone FSR sensor array (heel, arch, toe) for load distribution analysis
- 📳 **Multi-Modal Feedback** — Haptic vibration, LED indicators, and buzzer alerts for anomalies
- 📊 **Clinical Dashboard** — WiFi hotspot interface with live metrics and analytics
- ⚠️ **Fall Detection** — Real-time impact analysis with automatic caregiver alerts
- 🔋 **Battery & Energy Monitoring** — Efficient power management with remaining capacity display
- 📱 **Wireless Streaming** — Bluetooth/WiFi for mobile app integration
- 💾 **EEPROM Calibration** — On-device calibration storage for multi-user scenarios

## 🛠️ Hardware

| Component | Details |
|-----------|---------|
| **Microcontroller** | MYOSA motherboard |
| **Motion Sensor** | MPU6050 (3-axis accelerometer + gyroscope) |
| **Pressure Sensors** | FSR (Force Sensitive Resistor) ×3 |
| **Feedback Actuators** | Vibration motors ×3, LED indicators, buzzer |
| **Power** | Li-Po 1000 mAh rechargeable battery |
| **Communication** | Bluetooth, WiFi, Serial (UART) |

### Wiring Diagram (MYOSA motherboard)

```
GPIO3  (ADC) ←→ FSR Sensor
GPIO4  (ADC) ←→ Battery Monitor
GPIO6  (SDA) ←→ MPU6050 I2C
GPIO7  (SCL) ←→ MPU6050 I2C
GPIO8  ←→ Vibration Motor 3
GPIO9  ←→ Vibration Motor 2
GPIO10 ←→ Vibration Motor 1
3.3V, GND → Common power distribution
```

## 🚀 Quick Start

### 1. Clone Repository

```bash
git clone https://github.com/Team404-AJCE/orthostride.git
cd orthostride
```

### 2. Install Dependencies

```bash
pip install esptool pyserial
```

### 3. Prepare Firmware

- Open `firmware/OrthoStride_Integrated_ESP32C3.ino` in Arduino IDE or PlatformIO
- Add required libraries:
  - Adafruit_MPU6050 v2.2.0
  - Adafruit_Sensor v1.1.4
- Connect MYOSA motherboard via USB

### 4. Flash Firmware

```bash
python -m esptool --chip esp32c3 --port COM3 write_flash 0x0000 OrthoStride_Integrated_ESP32C3.bin
```

Replace `COM3` with your device's serial port (Windows: COM*, Linux/Mac: /dev/ttyUSB*)

### 5. Calibration

1. Connect serial monitor at **115200 baud**
2. Follow on-screen calibration prompts
3. Place known weights (2, 3, 5, 7, 8, 10 kg) on the foot sensor
4. Calibration data stored automatically in EEPROM

### 6. Start Monitoring

**Via Serial Monitor:**
```bash
python -m serial.tools.miniterm COM3 115200
```

**Via WiFi Dashboard:**
- Connect to WiFi network: `OrthoStride-XXXX`
- Open browser: `http://192.168.4.1`
- View real-time metrics

## 📋 Configuration

### Calibration & Thresholds (Serial Commands)

```plaintext
# Set overload weight limit (kg)
SETWEIGHT:8.0

# Configure step detection thresholds
STEPTHR:1.2,0.45

# Trigger calibration mode
CALIBRATE

# Reset session data
RESET

# Export session summary
EXPORT
```

## 📁 Project Structure

```
orthostride/
├── orthostride.md                        # MYOSA submission
├── README.md                             # This file
├── orthostride-demo.mp4                  # Demo video
├── firmware/
│   ├── OrthoStride_Integrated_ESP32C3.ino
│   ├── Footwear_Step_Fall_Detection.ino
│   ├── Battery_Percentage_Captive_Portal.ino
│   ├── fsr_basic_test.ino
│   ├── fsr_diagnostic_test.ino
│   ├── FSR_NoCalibration_Direct.ino
│   ├── FSR_Weight_Calibration.ino
│   └── Gradual_Vibration_Control.ino
└── assets/
    └── images/
        └── orthostride_dashboard.png

## 📤 Submission Guidelines (MYOSA / GitHub)

Please include the required images in `assets/images/` using these filenames (or equivalent):

- `Screenshot_2026-05-23_102722.png`
- `Screenshot_2026-05-23_102759.png`
- `Screenshot_2026-05-23_102810.png`
- `Screenshot_2026-05-23_102830.png`

Steps to prepare and upload images:

1. Rename images to remove spaces (recommended) and verify they are under 5 MB each.
2. Place the images in `assets/images/`.
3. Reference images in this README using Markdown:

   ![Demo 1](assets/images/Screenshot_2026-05-23_102722.png)
   ![Demo 2](assets/images/Screenshot_2026-05-23_102759.png)
   ![Demo 3](assets/images/Screenshot_2026-05-23_102810.png)
   ![Demo 4](assets/images/Screenshot_2026-05-23_102830.png)

4. Commit and push to the `main` branch (or open a PR if required by the project maintainers).

Minimum image requirements for submission:

- Format: PNG or JPG
- Resolution: at least 1024×768 recommended
- File size: < 5 MB preferred
- Filenames: short, lowercase, use hyphens or underscores (no spaces)

If you need me to upload the files from your machine, run the commands in `UPLOAD_INSTRUCTIONS.md` (Windows PowerShell) shown in the project root.
```

## 🔬 Technical Details

### Gait Analysis Algorithms

**Step Detection:** Peak detection on Z-axis acceleration with debounce window

```c
if (accelZ > stepThresholdHigh && millis() - lastStepMs > stepDebounceMs) {
  stepCount++;
  lastStepMs = millis();
}
```

**Fall Detection:** Free-fall + impact analysis

```c
if (magnitudeAcc < freeFallThreshold) {
  // Free-fall detected
} else if (magnitudeAcc > impactThreshold && inFreeFall) {
  fallCount++;
  triggerAlert();
}
```

**Tilt Angle:** Roll calculation from accelerometer

```c
float tiltAngle = atan2(accelX, sqrt(accelY*accelY + accelZ*accelZ));
```

### Pressure Sensing & Calibration

- FSR output: **0–4095** (12-bit ADC)
- Calibration: Piecewise linear interpolation from 6-point curve
- Filtering: Moving average (20 samples) + outlier rejection
- Output: Weight in kg with ±0.2 kg accuracy

## 📱 Mobile App Integration (Future)

- Planned Flutter app for iOS/Android
- Real-time streaming via Bluetooth
- Doctor's panel for clinical review
- Caregiver alerts and notifications

## 🤝 Contributing

Contributions are welcome! Please follow these steps:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit changes (`git commit -m 'Add amazing feature'`)
4. Push to branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

### Areas for Contribution

- 🤖 Machine learning for advanced gait classification
- 📊 Cloud data analytics and longitudinal tracking
- 📱 Mobile app development (Flutter/React Native)
- 🔧 Additional sensor integration (barometer, thermometer)
- 📖 Expanded documentation and tutorials
- 🧪 Test cases and validation studies

## 📜 License

This project is licensed under the **MIT License** — see [LICENSE](LICENSE) for details.

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
```

## 📞 Contact & Support

- **Team:** Team 404, Amal Jyothi College of Engineering
- **Email:** geomathew1212@gmail.com
- **GitHub Issues:** [Report bugs or suggest features](https://github.com/zeus10-11/orthostride)
- **Mentor:** Dr. Ranjitha Rajan, Associate Professor

## 🎯 Project Goals

- ✅ Real-time gait monitoring for rehabilitation
- ✅ Clinical-grade analytics and reporting
- ✅ Portable, non-intrusive haptic feedback
- ✅ Multi-user calibration and personalization
- 🔄 Mobile app with doctor/caregiver dashboards
- 🔄 Cloud-based patient progression tracking
- 🔄 AI/ML for advanced gait classification

## 🏆 Acknowledgements

- **Dr. Ranjitha Rajan** — Faculty mentor and advisor
- **MYOSA Event 2026 Organizers** — Platform and support
- **Amal Jyothi College of Engineering** — Resources and facilities

---

**IEEE MYOSA Event 2026 | Smart Rehabilitation Wearable System**

#include <EEPROM.h>

#define FSR_PIN 34          // Analog pin for FSR
#define EEPROM_SIZE 512     // EEPROM size for ESP32
#define CALIBRATION_ADDR 0  // Starting address in EEPROM
#define NUM_WEIGHTS 6       // Number of calibration points
#define NUM_SAMPLES 50      // Number of samples to average during calibration
#define MOVING_AVG_SIZE 20  // Larger moving average for stability (increased from 10)
#define WEIGHT_THRESHOLD 0.1  // Minimum weight change to display (kg)
#define MIN_STABLE_READINGS 5  // Require 5 consecutive stable readings before displaying
#define ZERO_THRESHOLD 20   // FSR reading below this is considered noise/no contact
#define OUTLIER_THRESHOLD 200  // Reject readings that jump more than this from average

// Calibration weights in kg
float calibrationWeights[NUM_WEIGHTS] = {2.0, 3.0, 5.0, 7.0, 8.0, 10.0};
int calibrationValues[NUM_WEIGHTS];    // Stored FSR readings for each weight
bool isCalibrated = false;

// Moving average filter with outlier rejection
int readingBuffer[MOVING_AVG_SIZE];
int bufferIndex = 0;
bool bufferFilled = false;
float lastDisplayedWeight = 0.0;
int stableReadingCount = 0;
int lastValidReading = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // Initialize moving average buffer
  for (int i = 0; i < MOVING_AVG_SIZE; i++) {
    readingBuffer[i] = 0;
  }
  
  // Load calibration data from EEPROM
  loadCalibration();
  
  Serial.println("\n=== FSR Weight Measurement System ===");
  Serial.println("Commands:");
  Serial.println("  1 - Start Calibration");
  Serial.println("  2 - View Current Calibration");
  Serial.println("  3 - Clear Calibration");
  Serial.println("\nReady for weight measurement...\n");
}

void loop() {
  // Check for serial commands
  if (Serial.available() > 0) {
    char command = Serial.read();
    
    if (command == '1') {
      performCalibration();
    } else if (command == '2') {
      displayCalibration();
    } else if (command == '3') {
      clearCalibration();
    }
  }
  
  // Read FSR with outlier rejection
  int rawFsrValue = analogRead(FSR_PIN);
  int smoothedFsrValue = getSmoothedReading(rawFsrValue);
  
  float weight = 0.0;
  
  if (isCalibrated) {
    weight = calculateWeight(smoothedFsrValue);
    
    // Only display if weight changed AND readings are stable
    if (abs(weight - lastDisplayedWeight) > WEIGHT_THRESHOLD && stableReadingCount >= MIN_STABLE_READINGS) {
      Serial.print("FSR: ");
      Serial.print(smoothedFsrValue);
      Serial.print(" (Raw: ");
      Serial.print(rawFsrValue);
      Serial.print(") | Weight: ");
      Serial.print(weight, 2);
      Serial.println(" kg");
      
      lastDisplayedWeight = weight;
    }
  } else {
    Serial.print("FSR Reading: ");
    Serial.print(smoothedFsrValue);
    Serial.println(" | [NOT CALIBRATED - Press 1 to calibrate]");
  }
  
  delay(100);
}

void performCalibration() {
  Serial.println("\n\n========================================");
  Serial.println("    CALIBRATION MODE STARTED");
  Serial.println("========================================");
  Serial.println("IMPORTANT: For each weight, system will");
  Serial.println("take 50 readings and average them.");
  Serial.println("Press any key when weight is stable...\n");
  
  for (int i = 0; i < NUM_WEIGHTS; i++) {
    Serial.println("----------------------------------------");
    Serial.print("Step ");
    Serial.print(i + 1);
    Serial.print(" of ");
    Serial.println(NUM_WEIGHTS);
    Serial.print("Place ");
    Serial.print(calibrationWeights[i], 1);
    Serial.println(" kg weight on sensor");
    Serial.println("Press any key when ready...");
    
    // Wait for user input
    while (Serial.available() == 0) {
      delay(100);
    }
    while (Serial.available() > 0) {
      Serial.read(); // Clear buffer
    }
    
    Serial.println("Measuring...");
    delay(1000); // Longer stabilization delay
    
    // Take multiple readings and average
    long sum = 0;
    for (int j = 0; j < NUM_SAMPLES; j++) {
      int reading = analogRead(FSR_PIN);
      sum += reading;
      Serial.print(".");
      delay(100);  // Increased delay between samples for stability
    }
    
    calibrationValues[i] = sum / NUM_SAMPLES;
    
    Serial.println();
    Serial.print("Average reading: ");
    Serial.println(calibrationValues[i]);
    Serial.print(calibrationWeights[i], 1);
    Serial.print(" kg -> FSR Value: ");
    Serial.println(calibrationValues[i]);
    Serial.println("✓ Saved!");
    delay(1000);
  }
  
  // Save to EEPROM
  saveCalibration();
  isCalibrated = true;
  
  Serial.println("\n========================================");
  Serial.println("  CALIBRATION COMPLETED & SAVED!");
  Serial.println("========================================");
  Serial.println("Remove all weights from sensor.\n");
  
  displayCalibration();
}

void saveCalibration() {
  int addr = CALIBRATION_ADDR;
  
  // Write a signature byte to indicate valid calibration
  EEPROM.write(addr++, 0xAA);
  
  // Write calibration values (4 bytes for each int)
  for (int i = 0; i < NUM_WEIGHTS; i++) {
    EEPROM.put(addr, calibrationValues[i]);
    addr += sizeof(int);
  }
  
  EEPROM.commit();
  Serial.println("Calibration data saved to EEPROM");
}

void loadCalibration() {
  int addr = CALIBRATION_ADDR;
  
  // Check signature byte
  byte signature = EEPROM.read(addr++);
  
  if (signature == 0xAA) {
    // Read calibration values
    for (int i = 0; i < NUM_WEIGHTS; i++) {
      EEPROM.get(addr, calibrationValues[i]);
      addr += sizeof(int);
    }
    
    isCalibrated = true;
    Serial.println("Calibration data loaded from EEPROM");
    displayCalibration();
  } else {
    Serial.println("No calibration data found. Please calibrate (press 1)");
    isCalibrated = false;
  }
}

void displayCalibration() {
  Serial.println("\n--- Current Calibration Data ---");
  if (isCalibrated) {
    for (int i = 0; i < NUM_WEIGHTS; i++) {
      Serial.print(calibrationWeights[i], 1);
      Serial.print(" kg -> ");
      Serial.println(calibrationValues[i]);
    }
  } else {
    Serial.println("Not calibrated yet!");
  }
  Serial.println("--------------------------------\n");
}

void clearCalibration() {
  EEPROM.write(CALIBRATION_ADDR, 0x00);
  EEPROM.commit();
  isCalibrated = false;
  Serial.println("\nCalibration cleared! Press 1 to recalibrate.\n");
}

// Moving average filter function with outlier rejection
int getSmoothedReading(int newReading) {
  // Calculate current average
  long sum = 0;
  int count = 0;
  
  for (int i = 0; i < MOVING_AVG_SIZE; i++) {
    sum += readingBuffer[i];
    if (readingBuffer[i] != 0 || bufferFilled) {
      count++;
    }
  }
  
  int currentAvg = (count > 0) ? (sum / count) : lastValidReading;
  
  // Reject erratic zero readings (sensor losing contact)
  if (newReading < ZERO_THRESHOLD && currentAvg > 50) {
    // This is likely a false zero, ignore it and use last valid reading
    stableReadingCount = 0;
    return lastValidReading;
  }
  
  // Reject outliers that jump too much from average
  if (count > 0 && currentAvg > 0 && abs(newReading - currentAvg) > OUTLIER_THRESHOLD) {
    // This is an outlier, ignore it
    stableReadingCount = 0;
    return currentAvg;
  }
  
  // Add valid reading to buffer
  readingBuffer[bufferIndex] = newReading;
  bufferIndex = (bufferIndex + 1) % MOVING_AVG_SIZE;
  
  if (bufferIndex == 0) {
    bufferFilled = true;
  }
  
  // Calculate new average
  sum = 0;
  count = bufferFilled ? MOVING_AVG_SIZE : bufferIndex;
  
  for (int i = 0; i < count; i++) {
    sum += readingBuffer[i];
  }
  
  int smoothedValue = (count > 0) ? (sum / count) : newReading;
  
  // Track stability for better filtering
  if (smoothedValue > ZERO_THRESHOLD) {
    stableReadingCount++;
    lastValidReading = smoothedValue;
  } else {
    stableReadingCount = 0;
  }
  
  return smoothedValue;
}

float calculateWeight(int fsrValue) {
  // Handle edge cases
  if (fsrValue <= 0) {
    return 0.0;
  }
  
  // If below minimum calibrated value, return 0 or extrapolate
  if (fsrValue < calibrationValues[0]) {
    // Simple extrapolation for values below minimum
    float slope = (calibrationWeights[1] - calibrationWeights[0]) / 
                  (float)(calibrationValues[1] - calibrationValues[0]);
    float weight = calibrationWeights[0] + slope * (fsrValue - calibrationValues[0]);
    return max(0.0f, weight);
  }
  
  // If above maximum calibrated value, extrapolate
  if (fsrValue >= calibrationValues[NUM_WEIGHTS - 1]) {
    int lastIdx = NUM_WEIGHTS - 1;
    float slope = (calibrationWeights[lastIdx] - calibrationWeights[lastIdx - 1]) / 
                  (float)(calibrationValues[lastIdx] - calibrationValues[lastIdx - 1]);
    float weight = calibrationWeights[lastIdx] + slope * (fsrValue - calibrationValues[lastIdx]);
    return weight;
  }
  
  // Linear interpolation between calibration points
  for (int i = 0; i < NUM_WEIGHTS - 1; i++) {
    if (fsrValue >= calibrationValues[i] && fsrValue < calibrationValues[i + 1]) {
      // Interpolate between point i and i+1
      float ratio = (float)(fsrValue - calibrationValues[i]) / 
                    (float)(calibrationValues[i + 1] - calibrationValues[i]);
      float weight = calibrationWeights[i] + ratio * (calibrationWeights[i + 1] - calibrationWeights[i]);
      return weight;
    }
  }
  
  return 0.0;
}

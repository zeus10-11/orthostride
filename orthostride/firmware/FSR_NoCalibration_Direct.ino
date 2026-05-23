#define FSR_PIN 34          // Analog pin for FSR
#define MOVING_AVG_SIZE 20  // Moving average window
#define ZERO_THRESHOLD 20   // FSR reading below this is considered no contact
#define OUTLIER_THRESHOLD 200  // Reject readings that jump too much

// Maximum FSR reading (typically 4095 for 12-bit ADC)
#define MAX_FSR_VALUE 4095

// Direct weight mapping (adjustable based on your sensor)
#define MAX_MEASURABLE_WEIGHT 10.0  // kg

// Moving average filter
int readingBuffer[MOVING_AVG_SIZE];
int bufferIndex = 0;
bool bufferFilled = false;
int lastValidReading = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize moving average buffer
  for (int i = 0; i < MOVING_AVG_SIZE; i++) {
    readingBuffer[i] = 0;
  }
  
  Serial.println("\n=== FSR Weight Measurement (No Calibration) ===");
  Serial.println("Direct FSR reading without calibration");
  Serial.println("========================================\n");
}

void loop() {
  // Read FSR with smoothing
  int rawFsrValue = analogRead(FSR_PIN);
  int smoothedFsrValue = getSmoothedReading(rawFsrValue);
  
  // Calculate weight (simple linear mapping)
  float weight = (float)smoothedFsrValue / MAX_FSR_VALUE * MAX_MEASURABLE_WEIGHT;
  
  // Only display if above noise threshold
  if (smoothedFsrValue > ZERO_THRESHOLD) {
    Serial.print("FSR: ");
    Serial.print(smoothedFsrValue);
    Serial.print(" (Raw: ");
    Serial.print(rawFsrValue);
    Serial.print(") | Est. Weight: ");
    Serial.print(weight, 2);
    Serial.println(" kg");
  } else {
    Serial.println("No contact detected");
  }
  
  delay(100);
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
    return lastValidReading;
  }
  
  // Reject outliers that jump too much from average
  if (count > 0 && currentAvg > 0 && abs(newReading - currentAvg) > OUTLIER_THRESHOLD) {
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
  
  // Update last valid reading
  if (smoothedValue > ZERO_THRESHOLD) {
    lastValidReading = smoothedValue;
  }
  
  return smoothedValue;
}

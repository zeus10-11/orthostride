#define FSR_PIN 34  // Analog pin for FSR

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== FSR BASIC TEST ===");
  Serial.println("This program reads raw FSR values");
  Serial.println("Press weights and watch the values change\n");
}

void loop() {
  // Read the raw analog value
  int fsrValue = analogRead(FSR_PIN);
  
  // Print raw value and simple bar graph
  Serial.print("FSR Value: ");
  Serial.print(fsrValue);
  Serial.print(" | ");
  
  // Simple visual bar
  for (int i = 0; i < fsrValue / 100; i++) {
    Serial.print("█");
  }
  
  // Print status
  if (fsrValue < 50) {
    Serial.print(" [NO CONTACT]");
  } else if (fsrValue < 100) {
    Serial.print(" [VERY LIGHT]");
  } else if (fsrValue < 500) {
    Serial.print(" [LIGHT CONTACT]");
  } else if (fsrValue < 1000) {
    Serial.print(" [MEDIUM PRESSURE]");
  } else if (fsrValue < 2000) {
    Serial.print(" [HEAVY PRESSURE]");
  } else {
    Serial.print(" [VERY HEAVY]");
  }
  
  Serial.println();
  delay(300);
}

#define FSR_PIN 34  // Analog pin for FSR

// Statistics tracking
int minValue = 4095;
int maxValue = 0;
int lastValue = 0;
int noContactCount = 0;
int contactCount = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║      FSR SENSOR DIAGNOSTIC TEST       ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  Serial.println("INSTRUCTIONS:");
  Serial.println("1. Start without any weight - note the readings");
  Serial.println("2. Then press weights on the sensor");
  Serial.println("3. Watch for smooth transitions (good)");
  Serial.println("4. Avoid jumping to 0 (bad connection)\n");
  
  Serial.println("expected values:");
  Serial.println("  No weight:   0-50");
  Serial.println("  2kg weight:  500-2000");
  Serial.println("  10kg weight: 2000-4000\n");
  
  Serial.println("═════════════════════════════════════════\n");
}

void loop() {
  // Read FSR value
  int fsrValue = analogRead(FSR_PIN);
  
  // Track statistics
  if (fsrValue < minValue) minValue = fsrValue;
  if (fsrValue > maxValue) maxValue = fsrValue;
  
  // Detect contact changes
  if (fsrValue < 50 && lastValue >= 50) {
    noContactCount++;
    Serial.println("\n⚠️  LOST CONTACT!");
  }
  if (fsrValue >= 50 && lastValue < 50) {
    contactCount++;
    Serial.println("\n✓ Contact detected");
  }
  
  lastValue = fsrValue;
  
  // Display current reading
  Serial.print("Value: ");
  printPaddedNumber(fsrValue, 4);
  Serial.print(" | ");
  
  // Visual bar graph
  int barLength = fsrValue / 100;
  for (int i = 0; i < 10; i++) {
    if (i < barLength) {
      Serial.print("█");
    } else {
      Serial.print("░");
    }
  }
  Serial.print(" | ");
  
  // Status interpretation
  if (fsrValue < 30) {
    Serial.print("🔴 NO CONTACT");
  } else if (fsrValue < 100) {
    Serial.print("🟠 VERY LIGHT");
  } else if (fsrValue < 500) {
    Serial.print("🟡 LIGHT TOUCH");
  } else if (fsrValue < 1500) {
    Serial.print("🟢 NORMAL (2-5kg)");
  } else if (fsrValue < 3000) {
    Serial.print("🟢 HEAVY (5-10kg)");
  } else {
    Serial.print("🔵 VERY HEAVY (>10kg)");
  }
  
  Serial.println();
  
  delay(300);
}

void printPaddedNumber(int num, int width) {
  String str = String(num);
  while (str.length() < width) {
    str = " " + str;
  }
  Serial.print(str);
}

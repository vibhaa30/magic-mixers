#include <SPI.h>

const int SS_PIN = 10;

void setup() {
  Serial.begin(115200);
  SPI.begin();
  pinMode(SS_PIN, OUTPUT);
  digitalWrite(SS_PIN, HIGH);
}

void loop() {
  if (Serial.available()) {
    // int gesture = Serial.parseInt();
    // int intensity = Serial.parseInt();
    String input = Serial.readStringUntil('\n');

    int gesture, intensity;
    sscanf(input.c_str(), "%d %d", &gesture, &intensity);

      Serial.print(gesture);
      Serial.print(" ");
      Serial.println(intensity);

    if (gesture >= 1 && gesture <= 2 && intensity >= 0 && intensity <= 10) {
      uint8_t data[2];
      data[0] = (uint8_t)gesture;
      data[1] = (uint8_t)intensity;

      SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));

      digitalWrite(SS_PIN, LOW);

      SPI.transfer(data[0]);
      SPI.transfer(data[1]);

      digitalWrite(SS_PIN, HIGH);

      SPI.endTransaction();

    } else {
      Serial.println("Invalid input");
    }
  }
}
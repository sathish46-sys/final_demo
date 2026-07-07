/* ESP32 SEIZURE ALERT – DEBUG VERSION
   BLE Trigger → 5 Beeps → Call → Play WAV (30 sec) → Hangup → SMS → 4 Beeps
   SD + WAV Debug Included
*/

#include <Arduino.h>
#include <HardwareSerial.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <FS.h>
#include <SD.h>

HardwareSerial sim800(1);

// Pins
#define BUZZER_PIN 21
#define SD_CS      5
#define DAC_PIN    25
#define SIM_BAUD   57600

// WAV settings
#define WAV_PATH     "/wav/alert_8k_mono_8bit.wav"
#define SAMPLE_RATE  8000U

File audioFile;
unsigned long sampleDelayUs;

volatile bool bleTriggerRequested = false;

// BLE UUIDs
#define SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// ---------------- BUZZERS ----------------
void beepBefore() {
  for (int i = 0; i < 5; i++) {
    digitalWrite(BUZZER_PIN, HIGH); delay(250);
    digitalWrite(BUZZER_PIN, LOW);  delay(250);
  }
}

void beepAfter() {
  for (int i = 0; i < 4; i++) {
    digitalWrite(BUZZER_PIN, HIGH); delay(250);
    digitalWrite(BUZZER_PIN, LOW);  delay(250);
  }
}

// ---------------- SMS ----------------
void sendSMS() {
  sim800.println("AT+CMGF=1");
  delay(300);

  sim800.println("AT+CMGS=\"+918277343756\"");
  delay(300);

  sim800.println("Emergency Alert!");
  sim800.println("Possible seizure detected.");
  sim800.println("Immediate help required.");
  sim800.println("Location: https://maps.google.com/?q=12.9418,77.5662");

  delay(200);
  sim800.write(26);
  delay(3000);
}

// ---------------- PLAY WAV FOR X ms ----------------
void playWavForMs(uint32_t durationMs) {

  Serial.println("Starting WAV playback…");

  if (!audioFile) {
    Serial.println("ERROR: audioFile is not open!");
    return;
  }

  // WAV header skip
  audioFile.seek(44);

  unsigned long start = millis();
  unsigned long next = micros();

  while (millis() - start < durationMs) {

    if (sim800.available()) Serial.write(sim800.read());

    if (!audioFile.available()) {
      Serial.println("Reached WAV end — looping.");
      audioFile.seek(44);
      continue;
    }

    if (micros() >= next) {

      int byteVal = audioFile.read();
      if (byteVal < 0) {
        Serial.println("Invalid byte, restarting WAV.");
        audioFile.seek(44);
        continue;
      }

      dacWrite(DAC_PIN, (uint8_t)byteVal);
      next += sampleDelayUs;

    } else {
      delayMicroseconds(5);
    }
  }

  dacWrite(DAC_PIN, 128); // mid-value silence
  Serial.println("WAV Playback Finished.");
}

// ---------------- BLE CALLBACK ----------------
class BLETriggerCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *ch) {
    String val = ch->getValue();
    val.trim();

    Serial.print("BLE Received: ");
    Serial.println(val);

    if (val == "1") bleTriggerRequested = true;
  }
};

// =================================================================
// SETUP
// =================================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("\n===== ESP32 Seizure Alert Booting =====");

  // SIM800 setup
  sim800.begin(SIM_BAUD, SERIAL_8N1, 16, 17);

  // Buzzer
  pinMode(BUZZER_PIN, OUTPUT);

  // ---- SD INITIALIZATION DEBUG ----
  Serial.println("Initializing SD card...");
  SPI.begin(18, 19, 23, SD_CS);

  if (!SD.begin(SD_CS)) {
    Serial.println("ERROR: SD card FAILED!");
    while (1) delay(1000);
  }
  Serial.println("SD OK.");

  // ---- Check WAV File ----
  if (!SD.exists(WAV_PATH)) {
    Serial.println("ERROR: WAV file NOT FOUND on SD!");
  } else {
    Serial.println("WAV file FOUND.");
    File f = SD.open(WAV_PATH);
    Serial.print("WAV Size: ");
    Serial.println(f.size());
    f.close();
  }

  audioFile = SD.open(WAV_PATH);
  if (!audioFile) {
    Serial.println("ERROR: Unable to open WAV file!");
  }

  sampleDelayUs = 1000000UL / SAMPLE_RATE;

  // ---- BLE Setup (Your Working Version) ----
  BLEDevice::init("ESP32-SEIZURE-DEVICE");
  BLEServer *server = BLEDevice::createServer();
  BLEService *svc = server->createService(SERVICE_UUID);

  BLECharacteristic *pChar =
      svc->createCharacteristic(CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_WRITE);

  pChar->setCallbacks(new BLETriggerCallback());
  svc->start();

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);
  adv->setMinPreferred(0x12);

  BLEDevice::startAdvertising();

  Serial.println("BLE READY — Advertising...");
}

// =================================================================
// LOOP
// =================================================================
void loop() {

  // BLE keep alive
  static unsigned long lastAdv = 0;
  if (millis() - lastAdv > 5000) {
    BLEDevice::startAdvertising();
    lastAdv = millis();
  }

  if (sim800.available())
    Serial.write(sim800.read());

  if (bleTriggerRequested) {

    bleTriggerRequested = false;

    Serial.println("=== EMERGENCY START ===");
    beepBefore();
    delay(2000);

    // PLACE CALL
    sim800.print("ATD8277343756;");
    sim800.print("\r\n");

    delay(6000);

    // PLAY WAV DURING CALL
    playWavForMs(30000);

    sim800.println("ATH");
    delay(1500);

    sendSMS();
    beepAfter();

    Serial.println("=== EMERGENCY COMPLETE ===");
  }

  delay(5);
}

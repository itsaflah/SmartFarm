// -----------------------------------------------------------------------------
// FARM AUTOMATION (ESP32 + Blynk)
// Modules: Solar Tracker + RFID Gate + Green Net (Temp Controlled)
// Non-blocking, function-based, ready to extend.
// -----------------------------------------------------------------------------

#define BLYNK_TEMPLATE_ID "TMPL3_WcyI0Zc"
#define BLYNK_TEMPLATE_NAME "Farm Automation"
#define BLYNK_AUTH_TOKEN "8-4CxpvBl68xxX1IzjCNgJqyNNzVylOe"

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// --------------------- WiFi ---------------------
char ssid[] = "test";
char pass[] = "12345678";

// --------------------- Blynk Timer --------------
BlynkTimer timer;

// ============================================================================
//                               SOLAR TRACKER
// ============================================================================
/*
  App:
  V20: Switch (0=Auto, 1=Manual)
  V21: Button (Manual LEFT)
  V22: Button (Manual RIGHT)
  V23: Display (Tilt value)
*/

int LDR_LEFT_PIN  = 34;
int LDR_RIGHT_PIN = 35;
int SOLAR_SERVO_PIN = 27;

Servo solarServo;
int solarPos = 90;           // 0..180
int solarTolerance = 60;     // LDR diff threshold
int solarStep = 1;           // step per update
bool solarAuto = true;

unsigned long lastSolarCheck = 0;
const unsigned long SOLAR_INTERVAL_MS = 60;

void solar_sendTilt() {
  int tilt = 90 - solarPos;          // CENTER=0, LEFT negative, RIGHT positive
  Blynk.virtualWrite(V23, tilt);
}

void solarTracker() {
  unsigned long now = millis();
  if (!solarAuto) return;
  if (now - lastSolarCheck < SOLAR_INTERVAL_MS) return;
  lastSolarCheck = now;

  int leftValue  = analogRead(LDR_LEFT_PIN);
  int rightValue = analogRead(LDR_RIGHT_PIN);
  int diff = leftValue - rightValue;

  if (abs(diff) > solarTolerance) {
    if (diff > 0 && solarPos < 180) solarPos += solarStep;   // move left
    else if (diff < 0 && solarPos > 0) solarPos -= solarStep; // move right
    solarServo.write(solarPos);
  }
}

// Blynk: Solar controls
BLYNK_WRITE(V20) { // 0=Auto, 1=Manual
  solarAuto = (param.asInt() == 0);
}
BLYNK_WRITE(V21) { // Manual LEFT
  if (!solarAuto && param.asInt() == 1 && solarPos < 180) {
    solarPos += solarStep;
    solarServo.write(solarPos);
  }
}
BLYNK_WRITE(V22) { // Manual RIGHT
  if (!solarAuto && param.asInt() == 1 && solarPos > 0) {
    solarPos -= solarStep;
    solarServo.write(solarPos);
  }
}

// ============================================================================
//                                 RFID GATE
// ============================================================================
/*
  Wiring (RC522 -> ESP32):
  SDA=21, SCK=18, MOSI=23, MISO=19, RST=22, 3.3V, GND
  Servos: Left=13, Right=12

  App:
  V24: Button (Toggle Gate)
  V25: Label (Gate Status)
*/

#define RC522_SS_PIN   21
#define RC522_RST_PIN  22
#define SERVO_LEFT_PIN 13
#define SERVO_RIGHT_PIN 12

MFRC522 rfid(RC522_SS_PIN, RC522_RST_PIN);
Servo gateLeft;
Servo gateRight;

byte auth1[4] = {0xE9, 0x23, 0x4B, 0x06};
byte auth2[4] = {0xA2, 0xB2, 0xF6, 0x05};

bool gateOpen = false;
bool gateMoving = false;
int gatePos = 0;            // 0..90 (we mirror to 180-pos on right)
int gateTarget = 0;
unsigned long lastGateStep = 0;
const unsigned long GATE_STEP_MS = 12;

unsigned long lastRFIDScan = 0;
const unsigned long RFID_COOLDOWN_MS = 3000;

void gate_updateStatus() {
  Blynk.virtualWrite(V25, gateOpen ? "🚪 Gate: OPEN" : "🚪 Gate: CLOSED");
}

bool uidEqual(byte* a, byte* b) {
  for (byte i = 0; i < 4; i++) if (a[i] != b[i]) return false;
  return true;
}
bool uidAuthorized(byte* uid) {
  return uidEqual(uid, auth1) || uidEqual(uid, auth2);
}

void gate_startOpening() {
  if (gateOpen || gateMoving) return;
  gateMoving = true;
  gateTarget = 90;
  Serial.println("Opening gate...");
}
void gate_startClosing() {
  if (!gateOpen || gateMoving) return;
  gateMoving = true;
  gateTarget = 0;
  Serial.println("Closing gate...");
}

void gate_motionTask() {
  if (!gateMoving) return;
  unsigned long now = millis();
  if (now - lastGateStep < GATE_STEP_MS) return;
  lastGateStep = now;

  if (gateTarget > gatePos) gatePos += 3;
  else if (gateTarget < gatePos) gatePos -= 3;

  gateLeft.write(gatePos);
  gateRight.write(180 - gatePos);

  if (gatePos == gateTarget) {
    gateMoving = false;
    gateOpen = (gatePos == 90);
    gate_updateStatus();
    if (gateOpen) timer.setTimeout(5000, gate_startClosing); // auto-close
  }
}

void rfidTask() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  unsigned long now = millis();
  if (now - lastRFIDScan < RFID_COOLDOWN_MS) {
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }
  lastRFIDScan = now;

  if (uidAuthorized(rfid.uid.uidByte)) {
    Serial.println("✅ RFID: Access Granted");
    Blynk.logEvent("gate_access", "Gate Opened");
    gate_startOpening();
  } else {
    Serial.println("❌ RFID: Access Denied");
    Blynk.logEvent("gate_invalid", "Invalid Access Attempt");
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// Blynk: Gate button
BLYNK_WRITE(V24) {
  if (param.asInt() == 1) {
    if (gateOpen) gate_startClosing();
    else gate_startOpening();
    Blynk.virtualWrite(V24, 0); // reset button
  }
}

// ============================================================================
//                            GREEN NET (TEMP SERVO)
// ============================================================================
/*
  DS18B20 on GPIO 4
  Net Servo on GPIO 33

  App:
  V26: Switch (0=Auto, 1=Manual)
  V27: Switch (Manual Open/Close) 1=open, 0=close
  V28: Display (Temperature °C)
  V29: Label (Net status)
*/

#define ONEWIRE_PIN 4
OneWire oneWire(ONEWIRE_PIN);
DallasTemperature ds(&oneWire);

#define NET_SERVO_PIN 33
Servo netServo;

bool netManual = false;
bool netClosed = false;
float tempC = 0.0;

const float TEMP_HIGH = 32.0;
const float TEMP_LOW  = 28.0;

void net_updateStatus() {
  String msg = (netManual ? "Manual " : "Auto ");
  msg += (netClosed ? "Closed" : "Open");
  Blynk.virtualWrite(V29, msg);
  Serial.println("[Net] " + msg);
}

void smoothMove(Servo &servo, int fromAngle, int toAngle, int stepDelay = 15) {
  if (fromAngle < toAngle) {
    for (int pos = fromAngle; pos <= toAngle; pos++) {
      servo.write(pos);
      delay(stepDelay);  // delay between each degree (lower = faster)
    }
  } else {
    for (int pos = fromAngle; pos >= toAngle; pos--) {
      servo.write(pos);
      delay(stepDelay);
    }
  }
}

void net_open() {
  smoothMove(netServo, 0, 220, 10);  // smooth open (10ms per degree)
  netClosed = false;
  net_updateStatus();
  Serial.println("🟢 Net Open");
}

void net_close() {
  smoothMove(netServo, 220, 0, 10);  // smooth close
  netClosed = true;
  net_updateStatus();
  Serial.println("🔴 Net Closed");
}

void net_tempTask() {
  ds.requestTemperatures();
  float t = ds.getTempCByIndex(0);
  if (t == DEVICE_DISCONNECTED_C) {
    Serial.println("⚠️ DS18B20 not detected!");
    return;
  }
  tempC = t;
  Blynk.virtualWrite(V28, tempC);

  if (!netManual) {
    if (tempC > TEMP_HIGH && !netClosed) net_close();
    else if (tempC < TEMP_LOW && netClosed) net_open();
  }
}

// Blynk: Net controls
BLYNK_WRITE(V26) { // 0=Auto, 1=Manual
  netManual = (param.asInt() == 1);
  net_updateStatus();
}
BLYNK_WRITE(V27) { // Manual open/close (1=open, 0=close)
  if (!netManual) return;
  int s = param.asInt();
  if (s == 1 && netClosed) net_open();
  else if (s == 0 && !netClosed) net_close();
}

// ============================================================================
//                               BLYNK HOOKS
// ============================================================================
BLYNK_CONNECTED() {
  // Sync toggles and show initial statuses
  Blynk.syncVirtual(V20, V21, V22, V24, V26, V27);
  solar_sendTilt();
  gate_updateStatus();
  net_updateStatus();
}

// ============================================================================
//                                SETUP/LOOP
// ============================================================================
void setup() {
  Serial.begin(115200);

  // Blynk + WiFi
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  // --- SOLAR ---
  solarServo.attach(SOLAR_SERVO_PIN);
  solarServo.write(solarPos);
  timer.setInterval(500, solar_sendTilt);

  // --- RFID GATE ---
  SPI.begin();                 // SCK=18, MISO=19, MOSI=23, SS=21
  rfid.PCD_Init();
  gateLeft.attach(SERVO_LEFT_PIN);
  gateRight.attach(SERVO_RIGHT_PIN);
  gatePos = 0;
  gateLeft.write(gatePos);
  gateRight.write(180 - gatePos);
  gateOpen = false;
  gate_updateStatus();

  // --- GREEN NET ---
  ds.begin();
  netServo.attach(NET_SERVO_PIN);
  net_open(); // default open on boot
  timer.setInterval(3000, net_tempTask); // temp check every 3s

  Serial.println("✅ Farm Automation combined system ready.");
}

void loop() {
  Blynk.run();
  timer.run();

  // Non-blocking, modular tasks
  solarTracker();     // Solar panel tracking
  rfidTask();         // RFID read & auth
  gate_motionTask();  // Smooth gate movement
}

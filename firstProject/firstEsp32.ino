#define BLYNK_TEMPLATE_ID "TMPL3_WcyI0Zc"
#define BLYNK_TEMPLATE_NAME "Farm Automation"
#define BLYNK_AUTH_TOKEN "8-4CxpvBl68xxX1IzjCNgJqyNNzVylOe"


#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <ESP32Servo.h>
#include "time.h"

// ---------------- WiFi ----------------
char ssid[] = "test";
char pass[] = "12345678";

// ------------------- SERVO BOX -------------------
Servo servo;
const int servoPin = 27;  // You can keep this, it works fine if ESP32 boots okay
const long OPEN_DURATION_MS = 5000;
unsigned long servoCloseTime = 0;

int openHour = -1;
int openMinute = -1;
bool activeDays[8] = {false};
bool openedToday = false;

// ------------------- SERVO BOX LEVEL -------------------
#define TRIG_SERVO 5
#define ECHO_SERVO 18
#define SERVO_FULL_DISTANCE 2
#define SERVO_EMPTY_DISTANCE 15

long durationServo;
float distanceServo;
int servoLevelPercent = 0;

// ------------------- BEE BUZZER -------------------
#define BUZZER_PIN 14
bool buzzerState = false;
int buzzerMode = 0; // 0 = AUTO, 1 = MANUAL
bool autoBuzzing = false;

int startHourBuzz=-1, startMinuteBuzz=-1, endHourBuzz=-1, endMinuteBuzz=-1;
bool activeDaysBuzz[8]={false};

// ------------------- WATER TANK -------------------
#define TRIG_PIN 19
#define ECHO_PIN 21
#define RELAY_PIN 23   // Active LOW

#define FULL_DISTANCE 5
#define EMPTY_DISTANCE 25

#define VMODE       V0
#define VMANUAL_BTN V6
#define VLEVEL_TANK V2
#define VLEVEL_SERVO V4
#define VMOTOR_LED  V5

long durationTank;
float distanceTank;
int levelPercent = 0;
bool autoModeTank = true;
bool manualRequest = false;
bool motorState = false;
bool wifiConnected = false;

//------------WATERING-------------------

#define VWATERING_MODE        V14  // Mode: 0 = AUTO, 1 = MANUAL
#define VWATERING_MANUAL_BTN  V15  // Manual button
#define VWATERING_SCHEDULE    V16  // Time input
#define VWATERING_LED_SERVO   V17  // Servo LED
#define VWATERING_LED_SOIL    V18  // Soil dry LED
#define VWATERING_LED_FLOOD   V19  // Flood warning LED (new!)

//WATERING SERVO
Servo wateringServo;
const int wateringServoPin = 33;  
const long WATERING_OPEN_DURATION_MS = 5000;
unsigned long wateringServoCloseTime = 0;
bool wateringServoOpened = false;

// ------------------- SOIL MOISTURE -------------------
#define WATERING_SOIL_PIN 34  // Analog pin for soil sensor
int wateringSoilValue = 0;

// Soil level flags
bool wateringSoilDry = false;
bool wateringSoilWet = false;
bool wateringSoilFlood = false;

// Define thresholds (calibrate these!)
const int DRY_THRESHOLD   = 2500;  // above this = dry
const int WET_THRESHOLD   = 1500;  // below this = flood risk

//TIME SCHEDULE
int wateringHour = -1;
int wateringMinute = -1;
bool wateringActiveDays[8] = {false};
bool wateringDoneToday = false;

bool wateringAutoMode = true;
bool wateringManualRequest = false;

// ------------------- TIME -------------------
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800;  // +5:30
const int daylightOffset_sec = 0;

// ------------------- BLYNK TIMER -------------------
BlynkTimer timer;

// ------------------- FUNCTIONS -------------------
void safeVirtualWrite(int pin, int value) {
  if (Blynk.connected()) Blynk.virtualWrite(pin, value);
}

// --------- SERVO BOX ---------
void openServo() {
  servo.write(90);
  servoCloseTime = millis() + OPEN_DURATION_MS;
  safeVirtualWrite(V7, 1);
  Serial.println("Servo Opened!");
}

void handleServo() {
  if (servoCloseTime != 0 && millis() >= servoCloseTime) {
    servo.write(0);
    servoCloseTime = 0;
    safeVirtualWrite(V7, 0);
    Serial.println("Servo Closed!");
  }
}

void checkServoSchedule() {
  if (openHour < 0 || openMinute < 0) return;
  if (servoCloseTime != 0) return;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  int hour = timeinfo.tm_hour;
  int minute = timeinfo.tm_min;
  int weekday = timeinfo.tm_wday;
  if (weekday == 0) weekday = 7;

  if (hour == 0 && minute == 0 && openedToday) openedToday = false;

  if (hour == openHour && minute == openMinute && !openedToday && activeDays[weekday]) {
    Serial.printf("Servo Auto Open %02d:%02d Day %d\n", hour, minute, weekday);
    openServo();
    openedToday = true;
  }
}

// --------- SERVO BOX LEVEL ---------
void measureServoLevel() {
  digitalWrite(TRIG_SERVO, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_SERVO, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_SERVO, LOW);

  durationServo = pulseIn(ECHO_SERVO, HIGH, 30000);
  if (durationServo == 0) return;

  distanceServo = (durationServo * 0.0343) / 2;
  distanceServo = constrain(distanceServo, SERVO_FULL_DISTANCE, SERVO_EMPTY_DISTANCE);
  servoLevelPercent = map(distanceServo, SERVO_EMPTY_DISTANCE, SERVO_FULL_DISTANCE, 0, 100);
  servoLevelPercent = constrain(servoLevelPercent, 0, 100);

  safeVirtualWrite(VLEVEL_SERVO, servoLevelPercent);
  Serial.printf("Servo Box Level: %d%%\n", servoLevelPercent);
}


// --------- BEE BUZZER ---------
void beeBuzz() {
  // ✅ This is your exact original tone logic
  for (int i = 0; i < 200; i++) {  
    int freq = 360 + random(-30, 30);
    tone(BUZZER_PIN, freq);
    delay(10);
  }
  noTone(BUZZER_PIN);
  delay(100);
}

void checkBeeAuto() {
  if (buzzerMode != 0) return;
  if (startHourBuzz < 0 || endHourBuzz < 0) return;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  int hour = timeinfo.tm_hour;
  int minute = timeinfo.tm_min;
  int weekday = timeinfo.tm_wday;
  if (weekday == 0) weekday = 7;

  int startM = startHourBuzz * 60 + startMinuteBuzz;
  int endM = endHourBuzz * 60 + endMinuteBuzz;
  int currM = hour * 60 + minute;

  bool shouldBuzz = activeDaysBuzz[weekday] &&
                    ((endM > startM && currM >= startM && currM < endM) ||
                     (endM <= startM && (currM >= startM || currM < endM)));

  if (shouldBuzz && !buzzerState) {
    buzzerState = true;
    autoBuzzing = true;
    safeVirtualWrite(V8, 1);
    Serial.println("Bee Buzzer AUTO ON");
  } else if (!shouldBuzz && buzzerState && autoBuzzing) {
    buzzerState = false;
    autoBuzzing = false;
    noTone(BUZZER_PIN);
    safeVirtualWrite(V8, 0);
    Serial.println("Bee Buzzer AUTO OFF");
  }

  if (buzzerState) beeBuzz();
}

void startBuzzer() {
  buzzerState = true;
  autoBuzzing = false;
  Blynk.virtualWrite(V8, 1);
  Serial.println("Bee Buzzer MANUAL ON");
}

void stopBuzzer() {
  buzzerState = false;
  autoBuzzing = false;
  noTone(BUZZER_PIN);
  Blynk.virtualWrite(V8, 0);
  Serial.println("Bee Buzzer OFF");
}

// --------- WATER TANK ---------
void measureTankLevel() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  durationTank = pulseIn(ECHO_PIN, HIGH, 30000);
  if (durationTank == 0) return;

  distanceTank = (durationTank * 0.0343) / 2;
  distanceTank = constrain(distanceTank, FULL_DISTANCE, EMPTY_DISTANCE);
  levelPercent = map(distanceTank, EMPTY_DISTANCE, FULL_DISTANCE, 0, 100);
  levelPercent = constrain(levelPercent, 0, 100);

  if (autoModeTank) {
    motorState = levelPercent <= 20;
    if (levelPercent >= 95) motorState = false;
  } else {
    if (manualRequest && levelPercent < 95) motorState = true;
    else {
      motorState = false;
      if (manualRequest && levelPercent >= 95) {
        manualRequest = false;
        safeVirtualWrite(VMANUAL_BTN, 0);
        Serial.println("Tank full — Pump stopped (Manual)");
      }
    }
  }

  digitalWrite(RELAY_PIN, motorState ? LOW : HIGH);

  safeVirtualWrite(VLEVEL_TANK, levelPercent);
  safeVirtualWrite(VMOTOR_LED, motorState ? 1 : 0);

  Serial.printf("Tank Mode: %s | Pump: %s\n", autoModeTank ? "AUTO" : "MANUAL", motorState ? "ON" : "OFF");
}

// ------------------- WATERING -------------------
//SERVO CONTROL
void wateringOpenServo() {
  wateringServo.write(0);
  wateringServoCloseTime = millis() + WATERING_OPEN_DURATION_MS;
  wateringServoOpened = true;
  safeVirtualWrite(VWATERING_LED_SERVO, 1);
  Serial.println("💧 Watering Servo Opened!");
}

void wateringHandleServo() {
  if (wateringServoOpened && millis() >= wateringServoCloseTime) {
    wateringServo.write(75);
    wateringServoOpened = false;
    safeVirtualWrite(VWATERING_LED_SERVO, 0);
    Serial.println("💧 Watering Servo Closed!");
  }
}

//SOIL MOISTURE
void wateringReadSoil() {
  wateringSoilValue = analogRead(WATERING_SOIL_PIN);

  // 3-level detection
  if (wateringSoilValue > DRY_THRESHOLD) {
    wateringSoilDry = true;
    wateringSoilWet = false;
    wateringSoilFlood = false;
  }
  else if (wateringSoilValue < WET_THRESHOLD) {
    wateringSoilDry = false;
    wateringSoilWet = false;
    wateringSoilFlood = true;
  }
  else {
    wateringSoilDry = false;
    wateringSoilWet = true;
    wateringSoilFlood = false;
  }

  // Update LEDs
  safeVirtualWrite(VWATERING_LED_SOIL, wateringSoilDry ? 1 : 0);
  safeVirtualWrite(VWATERING_LED_FLOOD, wateringSoilFlood ? 1 : 0);

  // Log soil status
  if (wateringSoilDry)
    Serial.printf("🌱 Soil: %d → DRY\n", wateringSoilValue);
  else if (wateringSoilWet)
    Serial.printf("🌱 Soil: %d → NORMAL (WET)\n", wateringSoilValue);
  else if (wateringSoilFlood)
    Serial.printf("🌊 Soil: %d → FLOOD RISK!\n", wateringSoilValue);
    Serial.println(analogRead(34));
}

//AUTO MODE SCHEDULE
void wateringCheckSchedule() {
  if (wateringHour < 0 || wateringMinute < 0) return;
  if (wateringServoOpened) return;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  int hour = timeinfo.tm_hour;
  int minute = timeinfo.tm_min;
  int weekday = timeinfo.tm_wday;
  if (weekday == 0) weekday = 7;

  if (hour == 0 && minute == 0 && wateringDoneToday) wateringDoneToday = false;

  if (hour == wateringHour && minute == wateringMinute && !wateringDoneToday && wateringActiveDays[weekday]) {
    if (wateringAutoMode) {
      if (wateringSoilFlood) {
        Serial.println("⚠️ AUTO MODE → Flood risk! Skipping watering.");
      }
      else if (wateringSoilDry) {
        wateringOpenServo();
        Serial.println("AUTO MODE → Soil dry → Watering started!");
      }
      else {
        Serial.println("AUTO MODE → Soil wet → Skipped watering.");
      }
    }
    wateringDoneToday = true;
  }
}

// ------------------- BLYNK HANDLERS -------------------
BLYNK_WRITE(V1) {
  TimeInputParam t(param);
  if (t.hasStartTime()) {
    openHour = t.getStartHour();
    openMinute = t.getStartMinute();
    for (int i = 1; i <= 7; i++) activeDays[i] = t.isWeekdaySelected(i);
    Serial.printf("Servo Schedule %02d:%02d\n", openHour, openMinute);
  }
}

BLYNK_WRITE(V3) {
  if (param.asInt() == 1) {
    openServo();
    safeVirtualWrite(V3, 0);
  }
}

BLYNK_WRITE(V11) {
  buzzerMode = param.asInt();
  Serial.println(buzzerMode == 0 ? "Bee AUTO" : "Bee MANUAL");
  if (buzzerMode == 0) {
    safeVirtualWrite(V9, 0);
    stopBuzzer();
  }
}




BLYNK_WRITE(V9) {
  if (buzzerMode == 0) {
    safeVirtualWrite(V9, 0);
    return;
  }
  if (param.asInt() == 1) startBuzzer(); else stopBuzzer();
}


BLYNK_WRITE(V10) {
  TimeInputParam t(param);
  if (t.hasStartTime() && t.hasStopTime()) {
    startHourBuzz = t.getStartHour();
    startMinuteBuzz = t.getStartMinute();
    endHourBuzz = t.getStopHour();
    endMinuteBuzz = t.getStopMinute();
    for (int i = 1; i <= 7; i++) activeDaysBuzz[i] = t.isWeekdaySelected(i);
    Serial.printf("Bee Schedule %02d:%02d to %02d:%02d\n", startHourBuzz, startMinuteBuzz, endHourBuzz, endMinuteBuzz);
  }
}


BLYNK_WRITE(VMODE) {
  autoModeTank = (param.asInt() == 0);
  manualRequest = false;
  safeVirtualWrite(VMANUAL_BTN, 0);
}

BLYNK_WRITE(VMANUAL_BTN) {
  if (!autoModeTank) manualRequest = param.asInt();
  else safeVirtualWrite(VMANUAL_BTN, 0);
}

BLYNK_WRITE(VWATERING_SCHEDULE) {
  TimeInputParam t(param);
  if (t.hasStartTime()) {
    wateringHour = t.getStartHour();
    wateringMinute = t.getStartMinute();
    for (int i = 1; i <= 7; i++) wateringActiveDays[i] = t.isWeekdaySelected(i);
    Serial.printf("⏰ Watering Schedule: %02d:%02d set\n", wateringHour, wateringMinute);
  }
}

BLYNK_WRITE(VWATERING_MODE) {
  wateringAutoMode = (param.asInt() == 0);
  wateringManualRequest = false;
  safeVirtualWrite(VWATERING_MANUAL_BTN, 0);
  Serial.println(wateringAutoMode ? "Mode: AUTO" : "Mode: MANUAL");
}

BLYNK_WRITE(VWATERING_MANUAL_BTN) {
  if (!wateringAutoMode && param.asInt() == 1) {
    if (wateringSoilFlood) {
      Serial.println("⚠️ MANUAL MODE → Flood risk! Watering canceled.");
      safeVirtualWrite(VWATERING_MANUAL_BTN, 0);
      return;
    }
    wateringOpenServo();
    wateringManualRequest = true;
    safeVirtualWrite(VWATERING_MANUAL_BTN, 0);
    Serial.println("MANUAL MODE → Watering started manually");
  }
}


// ------------------- SETUP -------------------
void setup() {
  Serial.begin(115200);

  servo.attach(servoPin);
  servo.write(0);
  safeVirtualWrite(V7, 0);

  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);
  safeVirtualWrite(V8, 0);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);

  pinMode(TRIG_SERVO, OUTPUT);
  pinMode(ECHO_SERVO, INPUT);

  wateringServo.attach(wateringServoPin);
  wateringServo.write(75);
  safeVirtualWrite(VWATERING_LED_SERVO, 0);

  pinMode(WATERING_SOIL_PIN, INPUT);

  WiFi.begin(ssid, pass);
  Serial.print("Connecting to WiFi...");
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 6000) {
    delay(500);
    Serial.print(".");
  }
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (wifiConnected) {
    Serial.println("\nWiFi Connected");
    Blynk.config(BLYNK_AUTH_TOKEN);
    Blynk.connect(5000);
  } else Serial.println("\nWiFi not connected — Offline Mode");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Time synced!");

  timer.setInterval(2000L, measureTankLevel);
  timer.setInterval(2000L, measureServoLevel);
    timer.setInterval(2000L, wateringReadSoil);

}


// ------------------- LOOP -------------------
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    WiFi.reconnect();
  } else wifiConnected = true;

  if (wifiConnected && Blynk.connected()) Blynk.run();

  timer.run();
  handleServo();
  checkServoSchedule();
  checkBeeAuto();
  wateringHandleServo();
  wateringCheckSchedule();

  if (buzzerState) beeBuzz();

}






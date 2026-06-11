#define BLYNK_TEMPLATE_ID "TMPLxxxxxx"
#define BLYNK_TEMPLATE_NAME "Presence Domicile"
#define BLYNK_AUTH_TOKEN "VotreAuthToken"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <DHTesp.h>
#include <LiquidCrystal_I2C.h>

// Brochage impose par le cahier des charges.
const uint8_t PIN_PIR = 14;
const uint8_t PIN_TRIG = 5;
const uint8_t PIN_ECHO = 18;
const uint8_t PIN_DHT = 4;
const uint8_t PIN_DOORBELL = 13;
const uint8_t PIN_LED_OCCUPIED = 25;
const uint8_t PIN_LED_VISITOR = 26;
const uint8_t PIN_BUZZER = 12;

// Wi-Fi simule par Wokwi. Remplacer uniquement les constantes Blynk plus haut.
char ssid[] = "Wokwi-GUEST";
char pass[] = "";

// Valeurs reelles demandees : absent apres 10 minutes.
// Valeurs raccourcies pour demonstration Wokwi et soutenance.
const unsigned long REAL_EMPTY_TIMEOUT_MS = 10UL * 60UL * 1000UL;
const unsigned long SIM_EMPTY_TIMEOUT_MS = 30UL * 1000UL;
const unsigned long SIM_LONG_ABSENCE_TIMEOUT_MS = 60UL * 1000UL;
const unsigned long SENSOR_INTERVAL_MS = 500UL;
const unsigned long LCD_INTERVAL_MS = 1000UL;
const unsigned long BLYNK_INTERVAL_MS = 2000UL;
const unsigned long VISITOR_DISPLAY_MS = 5000UL;
const unsigned long BUZZER_RING_MS = 900UL;
const unsigned long OCCUPANCY_ACCUM_INTERVAL_MS = 1000UL;

const float PRESENCE_DISTANCE_CM = 200.0;
const float LOW_TEMPERATURE_C = 10.0;

enum HomeState {
  VIDE,
  OCCUPE,
  VISITEUR,
  ABSENCE_PROLONGEE
};

struct SensorData {
  bool pirActive;
  float distanceCm;
  float temperatureC;
  float humidityPct;
  bool doorbellPressed;
};

DHTesp dht;
LiquidCrystal_I2C lcd(0x27, 16, 2);
BlynkTimer blynkTimer;

SensorData sensors = {false, 999.0, 0.0, 0.0, false};
HomeState currentState = VIDE;
HomeState lastPublishedState = VIDE;

bool vacationMode = false;
bool lowTempAlertSent = false;
bool vacationAlertSent = false;
bool lastDoorbellRaw = HIGH;
bool lastConfirmedPresence = false;

unsigned long lastPresenceMs = 0;
unsigned long lastSensorReadMs = 0;
unsigned long lastLcdUpdateMs = 0;
unsigned long lastBlynkUpdateMs = 0;
unsigned long visitorStartedMs = 0;
unsigned long buzzerStartedMs = 0;
unsigned long lastOccupancyAccumMs = 0;
unsigned long occupiedSecondsToday = 0;

String lastZone = "Aucune";
String latestEvent = "Systeme initialise";

/**
 * Convertit l'etat interne en texte lisible pour LCD, Serial et Blynk.
 */
String stateToText(HomeState state) {
  switch (state) {
    case VIDE:
      return "Vide";
    case OCCUPE:
      return "Occupe";
    case VISITEUR:
      return "Visiteur";
    case ABSENCE_PROLONGEE:
      return "Absence prolongee";
    default:
      return "Inconnu";
  }
}

/**
 * Ajoute un evenement court au journal et l'envoie vers Serial/Blynk.
 */
void logEvent(const String& eventText) {
  latestEvent = "T+" + String(millis() / 1000) + "s - " + eventText;
  Serial.println(latestEvent);
  Blynk.virtualWrite(V2, latestEvent);
}

/**
 * Envoie une notification Blynk.
 * Les codes evenement doivent etre crees dans Blynk Console.
 */
void sendBlynkEvent(const char* eventCode, const String& message) {
  Blynk.logEvent(eventCode, message);
}

/**
 * Mesure la distance avec HC-SR04 en centimetres.
 */
float readDistanceCm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  unsigned long duration = pulseIn(PIN_ECHO, HIGH, 30000UL);
  if (duration == 0) {
    return 999.0;
  }
  return duration / 58.0;
}

/**
 * Lit tous les capteurs et entrees utilisateur.
 */
void readSensors() {
  sensors.pirActive = digitalRead(PIN_PIR) == HIGH;
  sensors.distanceCm = readDistanceCm();

  TempAndHumidity th = dht.getTempAndHumidity();
  if (!isnan(th.temperature)) {
    sensors.temperatureC = th.temperature;
    sensors.humidityPct = th.humidity;
  }

  bool currentDoorbellRaw = digitalRead(PIN_DOORBELL);
  sensors.doorbellPressed = (lastDoorbellRaw == HIGH && currentDoorbellRaw == LOW);
  lastDoorbellRaw = currentDoorbellRaw;
}

/**
 * Applique la fusion PIR + ultrason pour confirmer une presence.
 */
bool isPresenceConfirmed() {
  return sensors.pirActive && sensors.distanceCm < PRESENCE_DISTANCE_CM;
}

/**
 * Declenche la logique visiteur : LED bleue, buzzer, journal et notification.
 */
void handleDoorbell() {
  if (!sensors.doorbellPressed) {
    return;
  }

  currentState = VISITEUR;
  visitorStartedMs = millis();
  buzzerStartedMs = millis();
  digitalWrite(PIN_LED_VISITOR, HIGH);
  tone(PIN_BUZZER, 1200);
  logEvent("Visiteur a la porte");
  sendBlynkEvent("visiteur_porte", "Visiteur a la porte");
}

/**
 * Met a jour la machine d'etats selon les capteurs et les delais.
 */
void updatePresenceState() {
  bool confirmedPresence = isPresenceConfirmed();

  if (confirmedPresence) {
    lastPresenceMs = millis();
    lastZone = "Entree + salon";
    lowTempAlertSent = false;

    if (!lastConfirmedPresence) {
      logEvent("Presence confirmee");
      if (vacationMode && !vacationAlertSent) {
        sendBlynkEvent("mouvement_vacances", "Mouvement detecte en mode vacances");
        vacationAlertSent = true;
      }
    }

    if (currentState != VISITEUR) {
      currentState = OCCUPE;
    }
  }

  lastConfirmedPresence = confirmedPresence;

  if (currentState == VISITEUR && millis() - visitorStartedMs > VISITOR_DISPLAY_MS) {
    currentState = confirmedPresence ? OCCUPE : VIDE;
    digitalWrite(PIN_LED_VISITOR, LOW);
  }

  unsigned long elapsedNoPresence = millis() - lastPresenceMs;
  if (!confirmedPresence && currentState != VISITEUR) {
    if (elapsedNoPresence > SIM_LONG_ABSENCE_TIMEOUT_MS) {
      currentState = ABSENCE_PROLONGEE;
    } else if (elapsedNoPresence > SIM_EMPTY_TIMEOUT_MS) {
      currentState = VIDE;
    }
  }

  if (currentState == ABSENCE_PROLONGEE && sensors.temperatureC < LOW_TEMPERATURE_C && !lowTempAlertSent) {
    logEvent("Absence prolongee + temperature basse");
    sendBlynkEvent("temperature_basse_absence", "Absence prolongee et temperature sous 10 degC");
    lowTempAlertSent = true;
  }

  if (currentState != lastPublishedState) {
    logEvent("Etat logement: " + stateToText(currentState));
    sendBlynkEvent("changement_etat", "Etat logement: " + stateToText(currentState));
    lastPublishedState = currentState;
  }
}

/**
 * Cumule une estimation simple du temps d'occupation journalier.
 */
void updateOccupancyCounter() {
  unsigned long now = millis();
  if (now - lastOccupancyAccumMs < OCCUPANCY_ACCUM_INTERVAL_MS) {
    return;
  }
  lastOccupancyAccumMs = now;

  if (currentState == OCCUPE || currentState == VISITEUR) {
    occupiedSecondsToday++;
  }
}

/**
 * Met a jour les sorties physiques locales.
 */
void updateOutputs() {
  digitalWrite(PIN_LED_OCCUPIED, currentState == OCCUPE || currentState == VISITEUR);

  if (millis() - buzzerStartedMs > BUZZER_RING_MS) {
    noTone(PIN_BUZZER);
  }
}

/**
 * Actualise l'ecran LCD 16x2.
 */
void updateLcd() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(stateToText(currentState).substring(0, 16));

  lcd.setCursor(0, 1);
  unsigned long secondsSincePresence = (millis() - lastPresenceMs) / 1000;
  String line2 = lastZone + " " + String(secondsSincePresence) + "s";
  lcd.print(line2.substring(0, 16));
}

/**
 * Publie les donnees vers Blynk.
 */
void sendToBlynk() {
  Blynk.virtualWrite(V0, stateToText(currentState));
  Blynk.virtualWrite(V1, sensors.temperatureC);
  Blynk.virtualWrite(V2, latestEvent);
  Blynk.virtualWrite(V4, sensors.distanceCm);
  Blynk.virtualWrite(V5, sensors.pirActive ? 1 : 0);
  Blynk.virtualWrite(V6, occupiedSecondsToday / 3600.0);
}

/**
 * Reception du switch Blynk pour le mode vacances.
 */
BLYNK_WRITE(V3) {
  vacationMode = param.asInt() == 1;
  vacationAlertSent = false;
  logEvent(vacationMode ? "Mode vacances active" : "Mode vacances desactive");
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_DOORBELL, INPUT_PULLUP);
  pinMode(PIN_LED_OCCUPIED, OUTPUT);
  pinMode(PIN_LED_VISITOR, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  dht.setup(PIN_DHT, DHTesp::DHT22);
  lcd.init();
  lcd.backlight();
  lcd.print("Projet 10 IoT");
  lcd.setCursor(0, 1);
  lcd.print("Initialisation");

  lastPresenceMs = millis();
  lastOccupancyAccumMs = millis();

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  logEvent("Systeme initialise");
}

void loop() {
  Blynk.run();

  unsigned long now = millis();
  if (now - lastSensorReadMs >= SENSOR_INTERVAL_MS) {
    lastSensorReadMs = now;
    readSensors();
    handleDoorbell();
    updatePresenceState();
    updateOutputs();
    updateOccupancyCounter();

    Serial.print("PIR=");
    Serial.print(sensors.pirActive);
    Serial.print(" Distance=");
    Serial.print(sensors.distanceCm);
    Serial.print("cm T=");
    Serial.print(sensors.temperatureC);
    Serial.print("C Etat=");
    Serial.println(stateToText(currentState));
  }

  if (now - lastLcdUpdateMs >= LCD_INTERVAL_MS) {
    lastLcdUpdateMs = now;
    updateLcd();
  }

  if (now - lastBlynkUpdateMs >= BLYNK_INTERVAL_MS) {
    lastBlynkUpdateMs = now;
    sendToBlynk();
  }
}


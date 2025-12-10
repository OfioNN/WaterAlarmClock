#include <LiquidCrystal_I2C.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>
#include <Preferences.h>

Preferences preferences;

// GPIO 8 , 9 - LCD
LiquidCrystal_I2C lcd(0x27, 16, 2); // OR 0x27

// GPIO 5, 6, 7 - Zegar
ThreeWire myWire(5, 6, 7); // DAT, CLK, RST
RtcDS1302<ThreeWire> Rtc(myWire);

// GPIO 4 - przekaźnik
#define RELAY_PIN 4

// GPIO 3 - przycisk +
#define BUTTON_PLUS 3

// GPIO 2 - przycisk -
#define BUTTON_MINUS 2

// GPIO 1 - przycisk OK
#define BUTTON_OK 1

// GPIO 0 - 5 x LED (czerwony)
#define LED_CONTROL 0

int alarmHour;
int alarmMinute;

// Wciśnięcie przycisku OK
bool toogleOK = false;

// lcd, przyciski (5sekund + włącza, - wyłącza)
bool lcdEnabled = true; 

unsigned long buttonPressStartPlus = 0;
unsigned long buttonPressStartMinus = 0;

// Zmienna do debouncowania (Do naprawy problemu "drgania przycisku" wciska się kilka razy w przeciągu ms)
unsigned long lastPressTime = 0;
const unsigned long debounceDelay = 300; // ms

// Odświeżanie LCD co 0.5s
unsigned long lastLCDUpdate = 0;
unsigned long lastAlarmCheck = 0;


// ====== STAN ALARMU ======
bool alarmActive = false;
unsigned long alarmTimer = 0;
int alarmStep = 0;

// ====== STAN LED ======
bool ledState = false;
bool ledActive = false;
unsigned long ledTimer = 0;
int ledStep = 0;
int ledCount = 0;

bool alarmOn = false;

void setup() {
  // Tworzenie namespace, tutaj są wszystkie ustawienia do momentu preferences.end()
  preferences.begin("alarm", false);

  // Pobieranie danych z pamięci, jeśli nie ma to ustawianie domyślnie
  alarmHour = preferences.getInt("hour", 8);
  alarmMinute = preferences.getInt("minute", 0);

  Serial.begin(115200);

  // Przyciski
  pinMode(BUTTON_PLUS, INPUT_PULLUP);
  pinMode(BUTTON_MINUS, INPUT_PULLUP);
  pinMode(BUTTON_OK, INPUT_PULLUP);

  // Przekaźnik
  pinMode(RELAY_PIN, OUTPUT);

  // LEDY
  pinMode(LED_CONTROL, OUTPUT);

  // Inicjalizacja wyświetlacza
  lcd.init();
  lcd.backlight();
  lcd.clear();

  // Start zegara
  Rtc.Begin();

  // Podstawowe nazwy
  setupLCD();

  // Tylko raz, żeby pobrać godzinę i datę z komputera (potem już pamięta bateria)
  // RtcDateTime currentTime = RtcDateTime(__DATE__, __TIME__);
  // Rtc.SetDateTime(currentTime);

}

void setupLCD(){
  // Data
  lcd.setCursor(0, 0);
  lcd.print("Date: ");

  // Godzina
  lcd.setCursor(0, 1);
  lcd.print("Time: ");
}

void loop() {
  RtcDateTime now = Rtc.GetDateTime();

  if(millis() - lastLCDUpdate >= 250){

    lastLCDUpdate = millis();

    if (lcdEnabled) {
      if (toogleOK == false) LCDScreen(now);
      else if (toogleOK == true) setAlarm(now);
    }
  }


  if (millis() - lastAlarmCheck >= 500) {

    lastAlarmCheck = millis();

    // Sprawdzenie alarmu
    if (!alarmOn){
      isAlarm(now, alarmHour, alarmMinute);
    }
  }

  handleAlarm();
  handleAlarmLED();

  // Sprawdzenie przycisków
  checkButton();

}

void isAlarm(RtcDateTime now, int Hour, int Minute) {
  if (now.Hour() == Hour && now.Minute() == (Minute-1) && now.Second() == 30){
    startAlarmLED();
    alarmOn = true;
  } 

  if (now.Hour() == Hour && now.Minute() == Minute && now.Second() == 0) {
    startAlarm();
    alarmOn = true;
  }

}

void checkButton() {
  // ===== PRZYCISK PLUS =====
  if (digitalRead(BUTTON_PLUS) == LOW) {
    if (buttonPressStartPlus == 0) {
      buttonPressStartPlus = millis();
    }
    if (millis() - buttonPressStartPlus > 5000) {
      // Włącz LCD
      lcdEnabled = true;
      lcd.display();
      lcd.backlight();
      buttonPressStartPlus = 0;
      delay(500); // aby nie włączało w pętli
    }
  } else {
    if (buttonPressStartPlus != 0 && millis() - buttonPressStartPlus < 5000) {
      // Krótkie naciśnięcie +
      alarmMinute += 15;
      if (alarmMinute >= 60) {
        alarmHour++;
        alarmMinute = 0;
      }
      if (alarmHour > 23) {
        alarmHour = 0;
      }
    }
    buttonPressStartPlus = 0;
  }

  // ===== PRZYCISK MINUS =====
  if (digitalRead(BUTTON_MINUS) == LOW) {
    if (buttonPressStartMinus == 0) {
      buttonPressStartMinus = millis();
    }
    if (millis() - buttonPressStartMinus > 5000) {
      // Wyłącz LCD
      lcdEnabled = false;
      lcd.noDisplay();
      lcd.noBacklight();
      buttonPressStartMinus = 0;
      delay(500); // aby nie wyłączało w pętli
    }
  } else {
    if (buttonPressStartMinus != 0 && millis() - buttonPressStartMinus < 5000) {
      // Krótkie naciśnięcie -
      alarmMinute -= 15;
      if (alarmMinute < 0) {
        alarmHour--;
        alarmMinute = 45;
      }
      if (alarmHour < 0) {
        alarmHour = 23;
      }
    }
    buttonPressStartMinus = 0;
  }

  // ===== PRZYCISK OK =====
  if(digitalRead(BUTTON_OK) == LOW){

    // Pobieranie czasu od momentu pracy programu w ms
    unsigned long currentTime = millis();

    // Zapobieganie "drgania" przycisku
    if (currentTime - lastPressTime > debounceDelay) {

      if (toogleOK == false){
        toogleOK = true;
        lcd.clear();
      }
      else if (toogleOK == true){

        // zapisz do nieulotnej pamięci
        preferences.putInt("hour", alarmHour);
        preferences.putInt("minute", alarmMinute);

        toogleOK = false;
        lcd.clear();
        setupLCD();
      }

      // Pobieranie czasu od ostatniego wciśnięcia (Żeby sprawdzić, czy minęło 300ms)
      lastPressTime = currentTime;
    }
  }

}

void setAlarm(RtcDateTime now){

  // CURRENT TIME
  lcd.setCursor(1, 0);
  lcd.print("Time: ");
  
  lcd.setCursor(7, 0);
  if (now.Hour() < 10) lcd.print("0");
  lcd.print(now.Hour());

  lcd.print(":");

  if (now.Minute() < 10) lcd.print("0");
  lcd.print(now.Minute());

  lcd.print(":");

  if (now.Second() < 10) lcd.print("0");
  lcd.print(now.Second());

  // ALARM TIME

  lcd.setCursor(2, 1);
  lcd.print("Alarm: ");

  lcd.setCursor(9, 1);
  if (alarmHour < 10) lcd.print("0");
  lcd.print(alarmHour);

  lcd.print(":");

  if (alarmMinute < 10) lcd.print("0");
  lcd.print(alarmMinute);
}

void LCDScreen(RtcDateTime now){
  // Data
  lcd.setCursor(7, 0);
  lcd.print(now.Day());
  lcd.print("/");
  lcd.print(now.Month());
  lcd.print("/");
  lcd.print(now.Year());

  // Godzina
  lcd.setCursor(7, 1);
  if (now.Hour() < 10) lcd.print("0");
  lcd.print(now.Hour());

  lcd.print(":");

  if (now.Minute() < 10) lcd.print("0");
  lcd.print(now.Minute());

  lcd.print(":");

  if (now.Second() < 10) lcd.print("0");
  lcd.print(now.Second());
}

// ====== LED 30s PRZED ALARMEM ======
void startAlarmLED() {
  ledActive = true;
  ledStep = 0;
  ledCount = 0;
  ledTimer = millis();
}

void handleAlarmLED() {
  if (!ledActive) return;

  unsigned long now = millis();

  switch (ledStep) {
    case 0: // ETAP 1: miganie co 1s, 20s (10 cykli ON/OFF)
      if (now - ledTimer >= 1000) {
        ledTimer = now;
        ledState = !ledState;
        digitalWrite(LED_CONTROL, ledState);
        if (!ledState) { // zliczaj pełne cykle (ON+OFF)
          ledCount++;
          if (ledCount >= 10) { 
            ledStep = 1;
            ledCount = 0;
          }
        }
      }
      break;

    case 1: // ETAP 2: miganie co 0.25s, 10s (20 cykli ON/OFF)
      if (now - ledTimer >= 250) {
        ledTimer = now;
        ledState = !ledState;
        digitalWrite(LED_CONTROL, ledState);
        if (!ledState) {
          ledCount++;
          if (ledCount >= 20) { 
            ledStep = 2; 
          }
        }
      }
      break;

    case 2: // koniec
      digitalWrite(LED_CONTROL, LOW);
      ledActive = false;
      alarmOn = false;
      break;
  }
}


// ====== START ALARMU ======
void startAlarm() {
  alarmActive = true;
  alarmStep = 0;
  alarmTimer = millis();
  digitalWrite(LED_CONTROL, HIGH); // LED świeci przez czas alarmu
}

// ====== OBSŁUGA ALARMU ======
void handleAlarm() {
  if (!alarmActive) return;

  unsigned long now = millis();

  switch (alarmStep) {
    case 0: // pierwszy ON
      digitalWrite(RELAY_PIN, HIGH);
      if (now - alarmTimer >= 2000) {  // 2 sekundy ON
        digitalWrite(RELAY_PIN, LOW);
        alarmTimer = now;
        alarmStep = 1;
      }
      break;

    case 1: // OFF 5s
      if (now - alarmTimer >= 5000) {
        digitalWrite(RELAY_PIN, HIGH);
        alarmTimer = now;
        alarmStep = 2;
      }
      break;

    case 2: // drugi ON
      if (now - alarmTimer >= 2000) {
        digitalWrite(RELAY_PIN, LOW);
        alarmTimer = now;
        alarmStep = 3;
      }
      break;

    case 3: // OFF 2.5s
      if (now - alarmTimer >= 2500) {
        digitalWrite(RELAY_PIN, HIGH);
        alarmTimer = now;
        alarmStep = 4;
      }
      break;

    case 4: // trzeci ON
      if (now - alarmTimer >= 2000) {
        digitalWrite(RELAY_PIN, LOW);
        digitalWrite(LED_CONTROL, LOW);
        alarmActive = false; // koniec alarmu
        alarmOn = false;
        setupLCD();
      }
      break;
  }
}


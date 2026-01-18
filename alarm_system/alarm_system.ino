#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <virtuabotixRTC.h>

// Inicjalizacja RTC (CLK, DAT, RST) 
virtuabotixRTC myRTC(12, 13, A0);
// Inicjalizacja LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

const byte ROWS = 4; 
const byte COLS = 3; 
char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};
byte rowPins[ROWS] = {9, 8, 7, 6}; 
byte colPins[COLS] = {5, 4, 3}; 
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

const int trigPin = 10;
const int echoPin = 11;
const int buzzerPin = A1;
const int buttonPin = 2; // PD2
unsigned long lastEntryBeepTime = 0;
const long entryBeepInterval = 1000; // Pikanie co 1 sekundę
unsigned long lastArmingBeepTime = 0;
const long armingBeepInterval = 1000;

enum AlarmState {
  DISARMED,
  ARMING,
  ARMED,
  ENTRY_DELAY,
  ALARM
};

AlarmState state = DISARMED;

bool lastButtonState = HIGH;

const unsigned long armingDuration = 5000; 
unsigned long armingStartTime = 0;        
bool armingInProgress = false;             

const int alarmDistance = 30;
const unsigned long alarmDelay = 10000;
unsigned long alarmStartTime = 0;

String inputCode = "";
String password = "1234";

// --- zmienne do migania diod ---
unsigned long lastBlinkTime = 0;
const long blinkInterval = 500;

void setup() {
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();

  // Ustawienie czasu (odkomentuj i wpisz datę RAZ, aby ustawić zegar, potem zakomentuj)
  // myRTC.setDS1302Time(00, 00, 00, 0, 00, 00, 0000); // sek, min, godz, dzień_tyg, dzień, mies, rok

  // ustawienie pinów przez rejestry
  // LEDy i buzzer (A1 = PC1, A2 = PC2, A3 = PC3)
  DDRC |= (1 << DDC1) | (1 << DDC2) | (1 << DDC3); // wyjścia
  PORTC &= ~((1 << PORTC1) | (1 << PORTC2) | (1 << PORTC3)); // start LOW

  // Przycisk (D2 = PD2)
  DDRD &= ~(1 << DDD2);  // wejście
  PORTD |= (1 << PORTD2); // pull-up

  // Ultrasoniczny (trig = PB2, echo = PB3)
  DDRB |= (1 << DDB2);  // trig jako wyjście
  DDRB &= ~(1 << DDB3); // echo jako wejście

  logEvent("ALARM ROZBROJONY - Uruchomienie systemu");
}

void loop() {
  switch(state)
  {
    case DISARMED:
      handleDisarmedState();
      break;
    case ARMING:
      handleArmingState();
      break;
    case ARMED:
      handleArmedState();
      break;
    case ENTRY_DELAY:
      handleEntryDelayState();
      break;
    case ALARM:
      handleAlarmState();
      break;
  }
}

// ----------- STANY ---------------

void handleDisarmedState()
{
  // zielona dioda ON, niebieska OFF
  PORTC |= (1 << PORTC2);
  PORTC &= ~(1 << PORTC3);
  PORTC &= ~(1 << PORTC1); // buzzer OFF

  updateStateDisplay("ALARM ROZBROJONY");
  
  bool currentButtonState = PIND & (1 << PIND2);
  
  if(lastButtonState && !currentButtonState)
  {
    state = ARMING;
    logEvent("UZBRAJANIE ALARMU...");
    clearDisplay(0);
  }
  lastButtonState = currentButtonState;
}

void handleArmingState()
{
  if (!armingInProgress) {
    armingStartTime = millis();
    armingInProgress = true;
  }
  
  unsigned long elapsed = millis() - armingStartTime;
  int remainingSeconds = (armingDuration - elapsed) / 1000 + 1;
  if (remainingSeconds < 0) remainingSeconds = 0;
  
  // Pikanie buzzera co sekundę
  if(millis() - lastArmingBeepTime >= armingBeepInterval) {
    lastArmingBeepTime = millis();
    tone(buzzerPin, 800, 50); // Bardzo krótki sygnał
  }

  // miganie niebieskiej diody
  if(millis() - lastBlinkTime >= blinkInterval) {
    lastBlinkTime = millis();
    PORTC ^= (1 << PORTC3); // toggle ledBlue
  }
  PORTC &= ~(1 << PORTC2); // zielona OFF
  PORTC &= ~(1 << PORTC1); // buzzer OFF

  updateStateDisplay("UZBRAJANIE...");

  // Odświeżanie czasu na LCD
  static int lastSec = -1;
  if (remainingSeconds != lastSec) {
    updateMsgDisplay(String("Czas: ") + remainingSeconds + "s");
    lastSec = remainingSeconds;
  }
  
  if (elapsed >= armingDuration) {
    state = ARMED;
    logEvent("ALARM UZBROJONY");
    armingInProgress = false;
    lastSec = -1;
    noTone(buzzerPin);
    clearDisplay(0);
    clearDisplay(1);
    armingInProgress = false;
    PORTC |= (1 << PORTC3); // ledBlue ON
  }
}

void handleArmedState()
{
  updateStateDisplay("ALARM UZBROJONY");
  
  if(handleKeypad()) {
    state = DISARMED;
    logEvent("ALARM ROZBROJONY - Podano poprawny kod");
    clearDisplay(0);
    clearDisplay(1);
    PORTC |= (1 << PORTC2);  // Zielona LED ON
    PORTC &= ~(1 << PORTC3); // Niebieska LED OFF
    return; // Kończymy funkcję, żeby nie sprawdzać już czujnika w tej pętli
  }

  if(isIntruderDetected())
  {
    state = ENTRY_DELAY;
    logEvent("WYKRYTO INTRUZA - Oczekiwanie na kod");
    alarmStartTime = millis();
    clearDisplay(0);
    clearDisplay(1);
  }
}

bool isIntruderDetected()
{
  // wysyłanie impulsu
  PORTB &= ~(1 << PORTB2); // LOW
  delayMicroseconds(2);
  PORTB |= (1 << PORTB2);  // HIGH
  delayMicroseconds(10);
  PORTB &= ~(1 << PORTB2); // LOW

  long duration = pulseIn(11, HIGH); // echoPin = 11

  if (duration == 0) return false;

  long distance = duration / 58;
  return (distance > 0 && distance <= alarmDistance);
}

void handleEntryDelayState()
{
  updateStateDisplay("PODAJ KOD ALARMU!");

  if(millis() - lastEntryBeepTime >= entryBeepInterval) {
    lastEntryBeepTime = millis();
    tone(buzzerPin, 800, 50); // Krótki "chirp" 50ms
  }

  if(handleKeypad()) {
    state = DISARMED;
    logEvent("ALARM ROZBROJONY");
    noTone(buzzerPin);  
    clearDisplay(0);
    clearDisplay(1);
    PORTC |= (1 << PORTC2); // zielona ON
    PORTC &= ~(1 << PORTC3); // niebieska OFF
  }

  if(millis() - alarmStartTime >= alarmDelay)
  {
    state = ALARM;
    logEvent("ALARM WŁAMANIOWY!");
    noTone(buzzerPin);
    clearDisplay(0);
    alarmStartTime = millis();
  }
}

void handleAlarmState()
{
  updateStateDisplay("ALARM!");
  tone(buzzerPin, 1000);

  // miganie obu diod
  if(millis() - lastBlinkTime >= blinkInterval) {
    lastBlinkTime = millis();
    PORTC ^= (1 << PORTC2); // toggle ledGreen
    PORTC ^= (1 << PORTC3); // toggle ledBlue
  }
  
  if(handleKeypad())
  {
    state = DISARMED;
    logEvent("ALARM ROZBROJONY - Podano poprawny kod");
    noTone(buzzerPin); // buzzer OFF
    PORTC |= (1 << PORTC2); // zielona ON
    PORTC &= ~(1 << PORTC3); // niebieska OFF
    clearDisplay(0);
    clearDisplay(1);
  }
}

// Funkcja do logowania zdarzeń z czasem rzeczywistym
void logEvent(String event) {
  myRTC.updateTime();
  char timestamp[25];

  // Formatowanie do tablicy znaków
  sprintf(timestamp, "[%02d/%02d/%04d %02d:%02d:%02d]",
          myRTC.dayofmonth, myRTC.month, myRTC.year, 
          myRTC.hours, myRTC.minutes, myRTC.seconds);
        
  Serial.print(timestamp);
  Serial.print(" EVENT: ");
  Serial.println(event);
}

// ----------- KLAWIATURA ---------------

bool handleKeypad()
{
  char key = keypad.getKey();
  
  if (key) {
    if (key == '#') { 
      if (inputCode == password) {
        inputCode = "";
        return true;
      } else {
        logEvent("BLEDNY KOD: " + inputCode);
        inputCode = "";
        updateMsgDisplay("Bledny kod!");
        return false;
      }
    } else if (key == '*' || inputCode.length() > 4) { 
      inputCode = "";
      updateMsgDisplay("Kod: " + inputCode);
    } else {
      inputCode += key;
      updateMsgDisplay("Kod: "+ inputCode);
    }
  }
  return false;
}


// ----------- LCD ---------------

void updateStateDisplay(String msg)
{
  lcd.setCursor(0, 0);
  lcd.print(msg);
}

void updateMsgDisplay(String msg)
{
  clearDisplay(1);
  lcd.setCursor(0, 1);
  lcd.print(msg);
}

void clearDisplay(int line)
{
  lcd.setCursor(0, line);
  lcd.print("                "); 
}

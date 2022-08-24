/*
 Name:		ADAR_1.ino
 Created:	8/1/2022 12:34:36 AM
 Author:	matias.ventura
*/

#include <AD9833.h>
#include <digitalWriteFast.h>
#include <LiquidCrystal_I2C.h>
#include <Encoder.h>
#include <EEPROM.h>

#define PIN_VFO_FNC 10  //SS Pin VFO
#define PIN_SMETER A7
#define PIN_ENC_A 2
#define PIN_ENC_B 3
#define PIN_ENC_BTN 4
#define PIN_TONE_OUT 7

#define FRAME_MS 33
#define DEBOUNCE_DELAY_MS 50
#define LONG_PRESS_MS 1000

#define BFO_FREQ 6000000
#define START_FREQ 1100000
//#define START_FREQ 6000000

const char LCD_BLOCK_CHAR = 0xff;
const char LCD_BLANK_CHAR = ' ';

int freqCal = 0;
int Smin = 0;
int Smax = 1023;

AD9833 vfo(PIN_VFO_FNC); // Defaults to 25MHz internal reference frequency
LiquidCrystal_I2C lcd(0x27, 16, 2);  // set the LCD address to 0x3F for a 16 chars and 2 line display
Encoder enc(PIN_ENC_A, PIN_ENC_B);

float currFreq = START_FREQ;
long currMultiplier = 10;

void setup() {
    setupVFO();
    lcd.init();
    lcd.backlight();
    setupLCDMain();
    enc.write(0);
    pinMode(PIN_ENC_BTN, INPUT_PULLUP);
}

void setupVFO() {
    // This MUST be the first command after declaring the AD9833 object
    vfo.Begin();
    // Each one can be programmed for:
    //   Signal type - SINE_WAVE, TRIANGLE_WAVE, SQUARE_WAVE, and HALF_SQUARE_WAVE
    //   Frequency - 0 to 12.5 MHz
    //   Phase - 0 to 360 degress (this is only useful if it is 'relative' to some other signal
    //           such as the phase difference between REG0 and REG1).
    // In ApplySignal, if Phase is not given, it defaults to 0.
    //gen.Reset();
    vfo.ApplySignal(SINE_WAVE, REG0, currFreq);
    vfo.EnableOutput(true);   // Turn ON the output - it defaults to OFF
}

unsigned long tLastFrame = 0;
unsigned long tEncBtn = 0;
bool encLow = false;
bool encSP = false;
//bool encLP = false;
bool menu = false;

void loop() {
    if ((millis() - tLastFrame) > FRAME_MS) {
        tLastFrame = millis();
        if (menu) frameMenu();
        else frameMain();
    }

    uint8_t encBtnState = digitalRead(PIN_ENC_BTN);

    if (encBtnState == LOW && encLow == false) {
        tEncBtn = millis();
        encLow = true;
    }

    if (encLow && encBtnState == HIGH) {
        if ((millis() - tEncBtn) > DEBOUNCE_DELAY_MS) {
            if ((millis() - tEncBtn) > LONG_PRESS_MS) {
                //encLP = true;
                if (menu) menuDisplayItem();
                else setupLCDMain();
                menu = !menu;
            }
            else {
                encSP = true;
            }
        }
        encLow = false;
    }

    delay(5);
}

//// ---------- EEPROM ----------

int readIntFromEEPROM(int address)
{
    return (EEPROM.read(address) << 8) + EEPROM.read(address + 1);
}

void writeIntToEEPROM(int address, int number)
{
    EEPROM.write(address, number >> 8);
    EEPROM.write(address + 1, number & 0xFF);
}

void loadSettings() {
    freqCal = readIntFromEEPROM(0);
    Smin = readIntFromEEPROM(2);
    Smax = readIntFromEEPROM(4);
}

void storeSettings() {
    writeIntToEEPROM(0, freqCal);
    writeIntToEEPROM(2, Smin);
    writeIntToEEPROM(4, Smax);
}

//// ---------- ENCODER ----------

bool readEncSP() {
    if (encSP) {
        encSP = false;
        return true;
    }
    else return false;
}

/*bool readEncLP() {
    if (encLP) {
        encLP = false;
        return true;
    }
    else return false;
}*/

long encPos = 0;
byte encCalCounter = 0;
int readEncoder() {
    int diff;
    long newEncPos = enc.read();
    if (encPos != newEncPos) {
        diff = newEncPos - encPos;

        if (diff < 4 && diff > -4) {
            encCalCounter++;
            if (encCalCounter > 150) {
                encPos = newEncPos;
                encCalCounter = 0;
            }
            return 0;
        }

        diff = round(diff / 4.0);
        encPos += diff * 4;
        encCalCounter = 0;

        if (encPos > 5000 || encPos < -5000) {
            enc.write(0);
            encPos = 0;
        }
    }
    else return 0;

    return diff;
}

float khz(float freq) {
    return freq * 1000;
}


//// ---------- MAIN SCREEN ----------

void setupLCDMain() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print('[');
    lcd.setCursor(10, 0);
    lcd.print("] S0");
    lcd.setCursor(11, 1);
    lcd.print("kHz");
    refreshLCDFreq();
    printMultiplier();
}


void frameMain() {
    if (readEncSP()) {
        if (currMultiplier >= 100000) currMultiplier = 1;
        else currMultiplier *= 10;
        printMultiplier();
    }

    setFreq(currFreq + (readEncoder() * currMultiplier));
    refreshSMeter();
}

void printMultiplier() {
    lcd.setCursor(15, 1);
    switch (currMultiplier)
    {
    case 1:
        lcd.print(1);
        break;
    case 10:
        lcd.print(2);
        break;
    case 100:
        lcd.print(3);
        break;
    case 1000:
        lcd.print(4);
        break;
    case 10000:
        lcd.print(5);
        break;
    case 100000:
        lcd.print(6);
        break;
    default:
        break;
    }
}

void setFreq(float freq) {
    if (freq <= 0) return;
    if (freq == currFreq) return;
    currFreq = freq;
    vfo.SetFrequency(REG0, freq);
    refreshLCDFreq();
}

void refreshLCDFreq() {
    char buff[9];
    lcd.setCursor(2, 1);
    dtostrf((currFreq + BFO_FREQ + freqCal) / 1000, 8, 3, buff);
    lcd.print(buff);
}

int lastSValue = 0;
void refreshSMeter() {
    int S = constrain(map(analogRead(PIN_SMETER), Smin, Smax, 0, 9), 0, 9);
    if (S != lastSValue) {
        char buff[10];
        if (S > lastSValue) {
            lcd.setCursor(lastSValue + 1, 0);
            for (int i = 0; i < S - lastSValue; i++) buff[i] = LCD_BLOCK_CHAR;
            buff[S - lastSValue] = '\0';
            lcd.print(buff);
        }
        else {
            lcd.setCursor(S + 1, 0);
            for (int i = 0; i < lastSValue - S; i++) buff[i] = LCD_BLANK_CHAR;
            buff[lastSValue - S] = '\0';
            lcd.print(buff);
        }

        lcd.setCursor(13, 0);
        lcd.print(S);
        lastSValue = S;
    }
}

//// ---------- MENU SCREEN ----------
char menuItem = 0;
bool editing = false;

void frameMenu() {
    int encRead = readEncoder();
    if (encRead) {
        if (editing) {
            menuSetItem(encRead);
        }
        else {
            menuItem += encRead;
            if (menuItem > 2) menuItem = 0;
            if (menuItem < 0) menuItem = 2;
            menuDisplayItem();
        }
    }
}

void menuDrawTop(const char* str) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(str);
}

void menuDrawValue(double value, unsigned char prec = 0) {
    char buff[9];
    lcd.setCursor(2, 1);
    dtostrf(value, 8, prec, buff);
    lcd.print(buff);
}

void menuDrawBottom(double value, const char* unit, unsigned char prec = 0) {
    if (strnlen(unit, 1)) {
        lcd.setCursor(11, 1);
        lcd.print(unit);
    }
    menuDrawValue(value, prec);
}

void menuDisplayItem() {
    switch (menuItem)
    {
    case 0:
        menuDrawTop("Freq Cal");
        menuDrawBottom((currFreq + BFO_FREQ + freqCal) / 1000, "kHz", 3);
        break;
    case 1:
        menuDrawTop("S Min");
        menuDrawBottom(Smin, "");
        break;
    case 2:
        menuDrawTop("S Max");
        menuDrawBottom(Smax, "");
        break;
    default:
        break;
    }
}

void menuSetItem(double val) {
    switch (menuItem)
    {
    case 0:
        freqCal += val;
        menuDrawValue((currFreq + BFO_FREQ + freqCal) / 1000, 3);
        break;
    case 1:
        Smin += val;
        menuDrawValue(Smin);
        break;
    case 2:
        Smax += val;
        menuDrawValue(Smax);
        break;
    default:
        break;
    }
}

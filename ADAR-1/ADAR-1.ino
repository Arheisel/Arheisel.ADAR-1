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
#define PIN_TX_SENSE 5
#define PIN_SWR_FWD A2
#define PIN_SWR_REF A3


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
int TxFwdMin = 0;
int TxFwdMax = 1023;
int TxRefMin = 0;
int TxRefMax = 1023;

AD9833 vfo(PIN_VFO_FNC); // Defaults to 25MHz internal reference frequency
LiquidCrystal_I2C lcd(0x27, 16, 2);  // set the LCD address to 0x3F for a 16 chars and 2 line display
Encoder enc(PIN_ENC_A, PIN_ENC_B);

float currFreq = START_FREQ;
double currDispFreq = 0;
long currMultiplier = 10;

void setup() {
    loadSettings();
    setupVFO();
    lcd.init();
    lcd.backlight();
    setupLCDMain();
    enc.write(0);
    pinMode(PIN_ENC_BTN, INPUT_PULLUP);
    pinMode(PIN_TX_SENSE, INPUT);
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
bool encLP = false;
bool menu = false;
bool tx = false;

void loop() {
    if ((millis() - tLastFrame) > FRAME_MS) {
        tLastFrame = millis();
        if (tx) frameTx;
        else if (menu) frameMenu();
        else frameMain();
    }

    uint8_t encBtnState = digitalRead(PIN_ENC_BTN);


    if (encBtnState == LOW && encLow == false) {
        tEncBtn = millis();
        encLow = true;
    }

    if (encLow) {
        if (encBtnState == HIGH) {
            if (encLP) {
                encLow = false;
                encLP = false;
            }
            else if ((millis() - tEncBtn) > DEBOUNCE_DELAY_MS) {
                encSP = true;
                encLow = false;
            }
        }
        else {
            if (encLP == false && (millis() - tEncBtn) > LONG_PRESS_MS) {
                menu = !menu;
                if (menu) {
                    menuDisplayItem();
                }
                else {
                    storeSettings();
                    setupLCDMain();
                }
                encLP = true;
            }
        }
    }

    uint8_t txState = digitalRead(PIN_TX_SENSE);

    if (txState == HIGH && tx == false) {
        SetupLcdTx();
        tx = true;
    }
    else if (txState == LOW && tx == true) {
        if (menu) {
            menuDisplayItem();
        }
        else {
            setupLCDMain();
        }
        tx = false;
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
    currDispFreq = (currFreq + BFO_FREQ + freqCal) / 1000.0;
    Smin = readIntFromEEPROM(2);
    Smax = readIntFromEEPROM(4);
    TxFwdMin = readIntFromEEPROM(6);
    TxFwdMax = readIntFromEEPROM(8);
    TxRefMin = readIntFromEEPROM(10);
    TxRefMax = readIntFromEEPROM(12);
}

void storeSettings() {
    if(freqCal != readIntFromEEPROM(0)) writeIntToEEPROM(0, freqCal);
    if(Smin != readIntFromEEPROM(2)) writeIntToEEPROM(2, Smin);
    if(Smax != readIntFromEEPROM(4)) writeIntToEEPROM(4, Smax);
    if(TxFwdMin != readIntFromEEPROM(6)) writeIntToEEPROM(6, TxFwdMin);
    if(TxFwdMax != readIntFromEEPROM(8)) writeIntToEEPROM(8, TxFwdMax);
    if(TxRefMin != readIntFromEEPROM(10)) writeIntToEEPROM(10, TxRefMin);
    if(TxRefMax != readIntFromEEPROM(12)) writeIntToEEPROM(12, TxRefMax);
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
uint8_t encCalCounter = 0;
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

/*float khz(float freq) {
    return freq * 1000;
}*/


//// ---------- MAIN SCREEN ----------

void setupLCDMain() {
    lcd.clear();
    drawSignalBox(0);
    lcd.setCursor(12, 0);
    lcd.print("S0");
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

    double newDispFreq = (freq + BFO_FREQ + freqCal) / 1000.0;
    if (newDispFreq < 7005.0 || newDispFreq > 7298.0) return;

    currFreq = freq;
    vfo.SetFrequency(REG0, freq);
    currDispFreq = newDispFreq;
    refreshLCDFreq();
}

void refreshLCDFreq() {
    char buff[9];
    lcd.setCursor(2, 1);
    dtostrf(currDispFreq, 8, 3, buff);
    lcd.print(buff);
}

uint8_t lastSValue = 0;
void refreshSMeter() {
    uint8_t S = constrain(map(analogRead(PIN_SMETER), Smin, Smax, 0, 9), 0, 9);
    if (S != lastSValue) {
        drawSignalBar(S, lastSValue, 0);
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

    if (readEncSP()) {
        lcd.setCursor(0, 1);
        editing = !editing;

        if (editing) {
            lcd.print('E');
        }
        else {
            lcd.print(LCD_BLANK_CHAR);
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
        menuDrawBottom(currDispFreq, "kHz", 3);
        break;
    case 1:
        menuDrawTop("S Min");
        menuDrawBottom(Smin, "");
        break;
    case 2:
        menuDrawTop("S Max");
        menuDrawBottom(Smax, "");
        break;
    case 3:
        menuDrawTop("SWR Fwd Min");
        menuDrawBottom(TxFwdMin, "");
        break;
    case 4:
        menuDrawTop("SWR Fwd Max");
        menuDrawBottom(TxFwdMax, "");
        break;
    case 5:
        menuDrawTop("SWR Ref Min");
        menuDrawBottom(TxRefMin, "");
        break;
    case 6:
        menuDrawTop("SWR Ref Max");
        menuDrawBottom(TxRefMax, "");
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
        currDispFreq = (currFreq + BFO_FREQ + freqCal) / 1000.0;
        menuDrawValue(currDispFreq, 3);
        break;
    case 1:
        Smin += val;
        menuDrawValue(Smin);
        break;
    case 2:
        Smax += val;
        menuDrawValue(Smax);
        break;
    case 3:
        TxFwdMin += val;
        menuDrawValue(TxFwdMin);
        break;
    case 4:
        TxFwdMax += val;
        menuDrawValue(TxFwdMax);
        break;
    case 5:
        TxRefMin += val;
        menuDrawValue(TxRefMin);
        break;
    case 6:
        TxRefMax += val;
        menuDrawValue(TxRefMax);
        break;
    default:
        break;
    }
}

//// ---------- TX SCREEN ----------

void SetupLcdTx() {
    lcd.clear();
    drawSignalBox(0);
    drawSignalBox(1);
    lcd.setCursor(11, 0);
    lcd.print('F');
    lcd.setCursor(15, 0);
    lcd.print('W');
    lcd.setCursor(11, 1);
    lcd.print('R');
}

uint8_t lastFwd = 0;
uint8_t lastRef = 0;
char txBuff[4];

void frameTx() {
    uint8_t fwd = constrain(map(analogRead(PIN_SWR_FWD), TxFwdMin, TxFwdMax, 0, 30), 0, 30);
    if (fwd != lastFwd) {
        drawSignalBar(fwd / 3, lastFwd / 3, 0);
        lcd.setCursor(13, 0);
        dtostrf(fwd / 2.0, 2, 0, txBuff);
        lcd.print(txBuff);
        lastFwd = fwd;
    }
    

    uint8_t ref = constrain(map(analogRead(PIN_SWR_REF), TxFwdMin, TxFwdMax, 0, 30), 0, 30);
    if (ref != lastRef) {
        drawSignalBar(ref / 3, lastRef / 3, 1);
        lcd.setCursor(13, 0);
        dtostrf(ref / 10.0, 3, 1, txBuff);
        lcd.print(txBuff);
        lastRef = ref;
    }
}


//// ---------- MISC ----------

inline void drawSignalBox(int row) {
    lcd.setCursor(0, row);
    lcd.print('[');
    lcd.setCursor(10, row);
    lcd.print(']');
}

void drawSignalBar(int value, int lastValue, int row) {
    if (value != lastValue) {
        char buff[10];
        if (value > lastValue) {
            lcd.setCursor(lastValue + 1, row);
            for (int i = 0; i < value - lastValue; i++) buff[i] = LCD_BLOCK_CHAR;
            buff[value - lastValue] = '\0';
            lcd.print(buff);
        }
        else {
            lcd.setCursor(value + 1, row);
            for (int i = 0; i < lastValue - value; i++) buff[i] = LCD_BLANK_CHAR;
            buff[lastValue - value] = '\0';
            lcd.print(buff);
        }
    }
}
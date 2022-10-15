//Arduino Clock with Neopixel Ring Animation
//Author: Alexander, Published on: May 7, 2016
//https://create.arduino.cc/projecthub/sfrwmaker/arduino-clock-with-neopixel-ring-animation-ff3422

//The clock with 4x7-segment indicator with a TM1637 and neopixel rgb 12 leds ring @ arduino nano
//Modefied by 7M4MON, on July 2021
//https://github.com/7m4mon/Neopixel_Ring_TMDSPL_Clock

#define NO_SHOW_DATE_AND_ALARM      // The display only shows the time.
#define DISABLE_DARK                // Disables night light, fireplace animation.

#include <EEPROM.h>
#include <Wire.h>
#include <DS3232RTC.h>
#include <TM1637Display.h>          // Uses TM1637 display instead of shift register.
#include <Time.h>
#include <TimeLib.h>

// The 4 7-segment indicator with the TM1637
const byte tmClockPin = 12;
const byte tmDataPin = 11;

TM1637Display display(tmClockPin, tmDataPin, 20);

// Neopixel ring
#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
  #include <avr/power.h>
#endif
const byte NEOPIXEL = 10;                           // Pin of Neopixel Ring
const byte RingSize = 12;                           // 24 -> 12
const byte HZ = 10;                                 // Redraw neopixel ring frequency (times per second)
    
const byte BTN_MENU_PIN = 2;
const byte BTN_INCR_PIN = 3;

Adafruit_NeoPixel strip = Adafruit_NeoPixel(RingSize, NEOPIXEL, NEO_GRB + NEO_KHZ800);

//------------------------------------------ Configuration data ------------------------------------------------
/* Config record in the EEPROM has the following format:
  uint32_t ID                           each time increment by 1
  struct cfg                            config data
  byte CRC                              the checksum
*/
struct cfg {
  uint16_t  alarm_time;                             // Alarm time in minuites from midnight
  byte      wday_mask;                              // Week day alarm mask 0 - sunday, 7 - saturday
  bool      active;                                 // Whether the alarm is active
  byte      nl_br;                                  // The night light brightness 0 - off, [1 - 6]
  byte      morning;                                // Morning time (10-minutes intervals), startinf neopixel animation
  byte      evening;                                // Evening time (10-minutes)
};

class CONFIG {
  public:
    CONFIG() {
      can_write     = false;
      buffRecords   = 0;
      rAddr = wAddr = 0;
      eLength       = 0;
      nextRecID     = 0;
      byte rs = sizeof(struct cfg) + 5;               // The total config record size
      // Select appropriate record size; The record size should be power of 2, i.e. 8, 16, 32, 64, ... bytes
      for (record_size = 8; record_size < rs; record_size <<= 1);
    }
    void init();
    void load(void);
    void getConfig(struct cfg &Cfg);                  // Copy config structure from this class
    void updateConfig(struct cfg &Cfg);               // Copy updated config into this class
    bool save(void);                                  // Save current config copy to the EEPROM
    bool saveConfig(struct cfg &Cfg);                 // write updated config into the EEPROM
    
  private:
    void     defaultConfig(void);
    struct   cfg Config;
    bool     readRecord(uint16_t addr, uint32_t &recID);
    bool     can_write;                               // The flag indicates that data can be saved
    byte     buffRecords;                             // Number of the records in the outpt buffer
    uint16_t rAddr;                                   // Address of thecorrect record in EEPROM to be read
    uint16_t wAddr;                                   // Address in the EEPROM to start write new record
    uint16_t eLength;                                 // Length of the EEPROM, depends on arduino model
    uint32_t nextRecID;                               // next record ID
    byte     record_size;                             // The size of one record in bytes
};

 // Read the records until the last one, point wAddr (write address) after the last record
void CONFIG::init(void) {
  eLength = EEPROM.length();
  uint32_t recID;
  uint32_t minRecID = 0xffffffff;
  uint16_t minRecAddr = 0;
  uint32_t maxRecID = 0;
  uint16_t maxRecAddr = 0;
  byte     records = 0;

  nextRecID = 0;

  // read all the records in the EEPROM find min and max record ID
  for (uint16_t addr = 0; addr < eLength; addr += record_size) {
    if (readRecord(addr, recID)) {
      ++records;
      if (minRecID > recID) {
        minRecID   = recID;
        minRecAddr = addr;
      }
      if (maxRecID < recID) {
        maxRecID   = recID;
        maxRecAddr = addr;
      }
    } else {
      break;
    }
  }

  if (records == 0) {
    wAddr = rAddr = 0;
    can_write = true;
    return;
  }

  rAddr = maxRecAddr;
  if (records < (eLength / record_size)) {            // The EEPROM is not full
    wAddr = rAddr + record_size;
    if (wAddr > eLength) wAddr = 0;
  } else {
    wAddr = minRecAddr;
  }
  can_write = true;
}

void CONFIG::getConfig(struct cfg &Cfg) {
  memcpy(&Cfg, &Config, sizeof(struct cfg));
}

void CONFIG::updateConfig(struct cfg &Cfg) {
  memcpy(&Config, &Cfg, sizeof(struct cfg));
}

bool CONFIG::saveConfig(struct cfg &Cfg) {
  updateConfig(Cfg);
  return save();                                      // Save new data into the EEPROM
}

bool CONFIG::save(void) {
  if (!can_write) return can_write;
  if (nextRecID == 0) nextRecID = 1;

  uint16_t startWrite = wAddr;
  uint32_t nxt = nextRecID;
  byte summ = 0;
  for (byte i = 0; i < 4; ++i) {
    EEPROM.write(startWrite++, nxt & 0xff);
    summ <<=2; summ += nxt;
    nxt >>= 8;
  }
  byte* p = (byte *)&Config;
  for (byte i = 0; i < sizeof(struct cfg); ++i) {
    summ <<= 2; summ += p[i];
    EEPROM.write(startWrite++, p[i]);
  }
  summ ++;                                            // To avoid empty records
  EEPROM.write(wAddr+record_size-1, summ);

  rAddr = wAddr;
  wAddr += record_size;
  if (wAddr > EEPROM.length()) wAddr = 0;
  return true;
}

void CONFIG::load(void) {
  bool is_valid = readRecord(rAddr, nextRecID);
  nextRecID ++;
  if (!is_valid) defaultConfig();
  return;
}

bool CONFIG::readRecord(uint16_t addr, uint32_t &recID) {
  byte Buff[record_size];

  for (byte i = 0; i < record_size; ++i) 
    Buff[i] = EEPROM.read(addr+i);
  
  byte summ = 0;
  for (byte i = 0; i < sizeof(struct cfg) + 4; ++i) {

    summ <<= 2; summ += Buff[i];
  }
  summ ++;                                            // To avoid empty fields
  if (summ == Buff[record_size-1]) {                  // Checksumm is correct
    uint32_t ts = 0;
    for (char i = 3; i >= 0; --i) {
      ts <<= 8;
      ts |= Buff[byte(i)];
    }
    recID = ts;
    memcpy(&Config, &Buff[4], sizeof(struct cfg));
    return true;
  }
  return false;
}

void CONFIG::defaultConfig(void) {
  Config.alarm_time = 0;
  Config.wday_mask  = 0;
  Config.active     = false;
  Config.nl_br      = 3;
  Config.morning    = 60;                             // 10:00
  Config.evening    = 126;                            // 21:00
}

//------------------------------------------ class BUTTON ------------------------------------------------------
class BUTTON {
  public:
    BUTTON(byte ButtonPIN, uint16_t timeout_ms = 3000) {
      buttonPIN = ButtonPIN; 
      pt = tickTime = 0; 
      overPress = timeout_ms;
    }
    void init(void) { pinMode(buttonPIN, INPUT_PULLUP); }
    void setTimeout(uint16_t timeout_ms = 3000) { overPress = timeout_ms; }
    byte buttonCheck(void);
    byte intButtonStatus(void) { byte m = mode; mode = 0; return m; }
    bool buttonTick(void);
    void buttonCnangeINTR(void);
  private:
    const uint16_t shortPress = 900;                // If the button was pressed less that this timeout, we assume the short button press
    const uint16_t tickTimeout = 200;               // Period of button tick, while tha button is pressed
    const byte bounce = 50;                         // Bouncing timeout (ms)  
    uint16_t overPress;                             // Maxumum time in ms the button can be pressed
    volatile byte mode;                             // The button mode: 0 - not presses, 1 - short press, 2 - long press
    volatile uint32_t pt;                           // Time in ms when the button was pressed (press time)
    volatile uint32_t tickTime;                     // The time in ms when the button Tick was set
    byte buttonPIN;                                 // The pin number connected to the button
};

byte BUTTON::buttonCheck(void) {                    // Check the button state, called each time in the main loop

  mode = 0;
  bool keyUp = digitalRead(buttonPIN);              // Read the current state of the button
  uint32_t now_t = millis();
  if (!keyUp) {                                     // The button is pressed
    if ((pt == 0) || (now_t - pt > overPress)) pt = now_t;
  } else {
    if (pt == 0) return 0;
    if ((now_t - pt) < bounce) return 0;
    if ((now_t - pt) > shortPress)                  // Long press
      mode = 2;
    else
      mode = 1;
    pt = 0;
  } 
  return mode;
}

bool BUTTON::buttonTick(void) {                     // When the button pressed for a while, generate periodical ticks

  bool keyUp = digitalRead(buttonPIN);              // Read the current state of the button
  uint32_t now_t = millis();
  if (!keyUp && (now_t - pt > shortPress)) {        // The button have been pressed for a while
    if (now_t - tickTime > tickTimeout) {
       tickTime = now_t;
       return (pt != 0);
    }
  } else {
    if (pt == 0) return false;
    tickTime = 0;
  } 
  return false;
}

void BUTTON::buttonCnangeINTR(void) {               // Interrupt function, called when the button status changed
  
  bool keyUp = digitalRead(buttonPIN);
  unsigned long now_t = millis();
  if (!keyUp) {                                     // The button has been pressed
    if ((pt == 0) || (now_t - pt > overPress)) pt = now_t; 
  } else {
    if ((now_t - pt) < bounce) return;
    if (pt > 0) {
      if ((now_t - pt) < shortPress) mode = 1;      // short press
        else mode = 2;                              // long press
      pt = 0;
    }
  }
}


//------------------------------------------ class LED display with TM1637 -------------------------
class TMDSPL {
  public:
    TMDSPL(const byte data, const byte clck) {
       dataPIN  = data;
       clockPIN = clck;
    }
    void init(void);                                // Initialize the clock display
    void show(byte dot, byte displayMask = 0xf);    // Should be called periodicaly to redraw the data on the LED display
    void showTime(byte Hour, byte Minute);          // Set time to the display
    void showDash(void);                            // show dash line if the alarm is not setup
    void showWdayGeneral(byte wday_mask);
    void showWdayFull(byte wday_mask);
  private:
    byte dataPIN, clockPIN;                        // TM1637 interface pins
    //byte digit_pin[4];                           // The pin of segmants
    byte symbol[4];                                 // The symbols to be displyed
    //
    //      A
    //     ---
    //  F |   | B
    //     -G-
    //  E |   | C
    //     ---
    //      D
    const byte hex[16] = {  // XGFEDCBA
      0b00111111,    // 0
      0b00000110,    // 1
      0b01011011,    // 2
      0b01001111,    // 3
      0b01100110,    // 4
      0b01101101,    // 5
      0b01111101,    // 6
      0b00000111,    // 7
      0b01111111,    // 8
      0b01101111,    // 9
      0b01110111,    // A
      0b01111100,    // b
      0b00111001,    // C
      0b01011110,    // d
      0b01111001,    // E
      0b01110001     // F
      };
    const byte wday[7] = {  0b00100000, 0b00000001, 0b00000010, 0b01000000, 0b00000100, 0b00001000, 0b00010000 };   // turning around bit
    };

void TMDSPL::init(void) {
  pinMode(dataPIN,  OUTPUT);
  pinMode(clockPIN, OUTPUT);
    // 7 Segment Displayの初期化
    display.setBrightness(0x06);
}

//この関数はshowTimeとかshowDashでsymbol[]がセットされたあとに必ず呼び出される。
void TMDSPL::show(byte dot, byte displayMask) {
  byte s[4];
  byte mask = 1;
  for (byte d = 0; d < 4; ++d) {                    // Print out digit from right to left
    s[3-d] = symbol[d];                             // Digit は下からなので。
    if (dot & mask) s[3-d] |= 0b10000000;              // 最上位ビットがドット
    if (displayMask & mask) {
      //s[3-d] = 0;       // 1 だったらそのまま表示
    }else{
      s[3-d] = 0;         // 0 なければ消灯
    }
    mask <<= 1;
  }
  display.setSegments(s);
}

void TMDSPL::showTime(byte Hour, byte Minute) {
  byte digit;
  
  for (byte d = 0; d < 4; ++d) {                    // Print out digit from right to left
    if (d < 2) {                                    // Minutes
      digit = Minute % 10;
      Minute /= 10;
    } else {                                        // Hours
      digit = Hour % 10;
      Hour /= 10;
    }
    symbol[d] = hex[digit];
  }
}

void TMDSPL::showDash(void) {
  for (byte d = 0; d < 4; ++d)
    symbol[d] = 0b01000000;                         // dash
}

void TMDSPL::showWdayGeneral(byte wday_mask) {
  byte wd = 0;
  byte m  = 1;
  symbol[3] = hex[10] | 0x80;                          // 'A.'
  switch (wday_mask) {
    case 0b00111110:                                // Working days
      symbol[2] = hex[1];
      symbol[1] = 0b01000000;                       // dash
      symbol[0] = hex[5];
      break;
    case 0b00011110:
      symbol[2] = hex[1];
      symbol[1] = 0b01000000;                       // dash
      symbol[0] = hex[4];
      break;
    case 0b01111111:
      symbol[2] = hex[10];                          // A
      symbol[1] = symbol[0] = 0b00111000;           // L
      break;
    case 0:
      showDash();
    default:                                        // 0 or unknown
      for (byte i = 0; i <= 6; ++i) {
        if (m & wday_mask) wd |= wday[i];
        m <<= 1;
      }
      symbol[2] = wd;
      break;
  }
}

void TMDSPL::showWdayFull(byte wday_mask) {
  symbol[3] = hex[10] | 0x80;                          // 'A.'
  symbol[2] = hex[wday_mask / 16];
  symbol[1] = hex[wday_mask % 16] | 0x80;
  byte wd = 0;
  byte m  = 1;
  for (byte i = 0; i <= 6; ++i) {
    if (m & wday_mask) wd |= wday[i];
    m <<= 1;
  }
  symbol[0] = wd;
}


//------------------------------------------ class NEOPIXEL ANIMATION ------------------------------------------
class ANIMATION {
  public:
    ANIMATION() {
      for (byte i = 0; i < 3; ++i)
        rgb[i] = random(32) << 2;
    }
    void          switchOffRing(void);
    uint32_t      Wheel(byte WheelPos);
    virtual void  show(uint16_t S,  uint32_t Color) = 0;
    virtual void  clean(uint16_t S, uint32_t Color) = 0;
    virtual void  handle(byte k)                    { }
  protected:
    void flashLabels(uint16_t S, byte pos = 255);
    void fadeRing(uint32_t Color, byte shift);
    void initXcolor(void);                          // Initialize the color of cross-clock pixels
    byte rgb[3];                                    // The color of the cross-clock pixels: 0h, 3h, 6h, 9h
};

void ANIMATION::switchOffRing(void) {
  for (byte i = 0; i < strip.numPixels(); ++i) strip.setPixelColor(i, 0);
  strip.show();
}

// 4 indicators (0h, 3h, 6h, 9h) is flashing every 5 seconds
void ANIMATION::flashLabels(uint16_t S, byte pos) {
 static byte br_lev[] = { 127, 64, 50, 40, 30, 25, 30, 40, 50, 64 };
 const uint16_t tick = 5 * HZ;                      // ticks in 5 seconds

  byte secPart  =  S % tick; 
  byte divider = tick / 10;                         // Normalize time interval to the array size
  byte pos1 = secPart / divider;                    // index in the brightness array 
  byte k = secPart % divider;
  byte pos2 = pos1 + 1; if (pos2 >= 10) pos2 = 0;
  byte br = map(k, 0, divider, br_lev[pos1], br_lev[pos2]);
  int r = int(rgb[0]) * br; r >>= 7;
  int g = int(rgb[1]) * br; g >>= 7;
  int b = int(rgb[3]) * br; b >>= 7;
  uint32_t white = strip.Color(r, g, b);
  int n = strip.numPixels();
  for (byte label = 0; label < n; label += n/4) { 
    if (label == 0 || label != pos) strip.setPixelColor(label, white);
  }
}

uint32_t ANIMATION:: Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if (WheelPos < 85) {
    return strip.Color((255 - WheelPos * 3) >> 3, 0, (WheelPos * 3) >> 3);
  }
  if(WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, (WheelPos * 3) >> 3, (255 - WheelPos * 3) >> 3);
  }
  WheelPos -= 170;
  return strip.Color((WheelPos * 3) >> 3, (255 - WheelPos * 3) >> 3, 0);
}

void ANIMATION::fadeRing(uint32_t Color, byte shift) {
  byte b = Color;
  byte g = Color >> 8;
  byte r = Color >> 16;
  r >>= shift;
  g >>= shift;
  b >>= shift;
  uint32_t Color_faded = strip.Color(r, g, b);
  for (byte i = 0; i < strip.numPixels(); ++i) strip.setPixelColor(i, Color_faded);
}

void ANIMATION::initXcolor(void) {
  for (byte i = 0; i < 3; ++i)
    rgb[i] = random(32) << 2;
}

//------------------------------ Animation: Fill ring as seconds past, new dot every 2.5 sec -------------------
class FillRing : public ANIMATION {
  public:
    FillRing()                                      { old_pos = 0; }
    virtual void  show(uint16_t S,  uint32_t Color);
    virtual void  clean(uint16_t S, uint32_t Color);
  private:
    byte old_pos;                                   // Last position filed with new color
};

void FillRing::show(uint16_t S, uint32_t Color) {
  if (S == 0) initXcolor();

  if (S < RingSize) strip.setPixelColor(S, 0);      // Clear the ring in the beginning of the new minutes
  byte pos = (uint32_t)S * RingSize / ((uint32_t)HZ * 60);
  if (pos != old_pos) {
    strip.setPixelColor(pos, Color);
    old_pos = pos;
  }
  flashLabels(S, pos);
}

void FillRing::clean(uint16_t S, uint32_t Color) {
  const uint16_t Last8Ticks = 60 * HZ - 8;
  if (S > Last8Ticks) {
    S -= Last8Ticks;
    fadeRing(Color, S);
    flashLabels(S);
  } else {
    show(S, Color);
  }
}

//------------------------------ Animation: Each dot is coming counterclockwize from 12 o'clock ----------------
class cClockRing : public ANIMATION {
  public:
    cClockRing()                                    { run_pos = 255; }
    virtual void  show(uint16_t S,  uint32_t Color);
    virtual void  clean(uint16_t S, uint32_t Color);
  private:
    byte run_pos;                                   // Last position filed with new color
};

void cClockRing::show(uint16_t S, uint32_t Color) {
 const byte onedot = ((uint16_t)HZ * 60) / RingSize;

  if (S == 0) initXcolor();

  byte pos = (uint32_t)S * RingSize / ((uint32_t)HZ * 60);
  byte runSecond = S % onedot;
  byte newRunPos = RingSize - RingSize * runSecond / onedot;
  if ((newRunPos < RingSize) && newRunPos > pos) {
    if (newRunPos != run_pos) {
      strip.setPixelColor(newRunPos, Color);
      if (run_pos < RingSize) strip.setPixelColor(run_pos, 0);
      run_pos = newRunPos;
    } 
  }
  strip.setPixelColor(pos, Color);
  flashLabels(S, run_pos);
}

void cClockRing::clean(uint16_t S, uint32_t Color) {
 const uint16_t Last8Ticks = 60 * HZ - 8;

  if (S > Last8Ticks) {
    S -= Last8Ticks;
    fadeRing(Color, S);
    flashLabels(S);
  } else {
    show(S, Color);
  }
}

//------------------------------ Animation: Each dot dot is coming counterclockwize from last position ---------
class lpClockRing : public ANIMATION {
  public:
    lpClockRing()                                   { run_pos = 255; }
    virtual void  show(uint16_t S,  uint32_t Color);
    virtual void  clean(uint16_t S, uint32_t Color);
  private:
    byte run_pos;                                   // Last position filed with new color
};

void lpClockRing::show(uint16_t S, uint32_t Color) {
 const byte onedot = ((uint16_t)HZ * 60) / RingSize;

  if (S == 0) initXcolor();

  byte pos = uint32_t(S) * RingSize / (uint32_t(HZ) * 60);
  byte runSecond = S % onedot;
  char newRunPos = pos - RingSize * runSecond / onedot;
  if (newRunPos < 0) newRunPos += RingSize; 
  if ((newRunPos < RingSize) && newRunPos != pos) {
    if (newRunPos != run_pos) {
      strip.setPixelColor(newRunPos, Color);
      if (run_pos < RingSize) strip.setPixelColor(run_pos, 0);
      run_pos = newRunPos;
    } 
  }
  strip.setPixelColor(pos, Color);

  flashLabels(S, pos);
}

void lpClockRing::clean(uint16_t S, uint32_t Color) {
 const uint16_t LastTick = 60 * HZ - 1;

  show(S, Color);
  if (S >= LastTick) strip.setPixelColor(RingSize-1, 0);
}

//------------------------------ Animation: Rise the dot slowly ------------------------------------------------
class rsRing : public ANIMATION {
  public:
    rsRing()                                        { keep_pos = true; }
    virtual void  show(uint16_t S,  uint32_t Color);
    virtual void  clean(uint16_t S, uint32_t Color);
    virtual void  handle(byte k)                    { keep_pos = (k != 0); }
  private:
    bool keep_pos;
};

void rsRing::show(uint16_t S, uint32_t Color) {
 const byte onedot = ((uint16_t)HZ * 60) / RingSize;

  if (S == 0) initXcolor();
  if (S < RingSize) strip.setPixelColor(S, 0);      // Clear the ring in the beginning of the new minutes
  byte pos = (uint32_t)S * RingSize / ((uint32_t)HZ * 60);

  byte b1 = Color;
  byte g1 = Color >> 8;
  byte r1 = Color >> 16;
  byte runSecond = S % onedot;
  byte shift = (runSecond << 3) / onedot;
  r1 >>= (7 - shift);
  g1 >>= (7 - shift);
  b1 >>= (7 - shift);
  strip.setPixelColor(pos, strip.Color(r1, g1, b1));

  if (!keep_pos) {
    byte fadePos = RingSize-1;
    if (pos >= 1) fadePos = pos - 1;
    uint32_t fadeColor = strip.getPixelColor(fadePos);
    b1 = fadeColor;
    g1 = fadeColor >> 8;
    r1 = fadeColor >> 16;
    if (shift > 3) {
      r1 >>= 1;
      g1 >>= 1;
      b1 >>= 1;
    } else if (shift == 7) strip.setPixelColor(fadePos, 0);
    strip.setPixelColor(fadePos, strip.Color(r1, g1, b1));
  }

  flashLabels(S, pos); 
}

void rsRing::clean(uint16_t S, uint32_t Color) {
 const byte onedot = ((uint16_t)HZ * 60) / RingSize;
 const uint16_t Last8Ticks = 60 * HZ - 8;

  show(S, Color);
  byte runSecond = S % onedot;
  byte shift = (runSecond << 3) / onedot;
  if (S >  Last8Ticks) { 
    if (keep_pos) fadeRing(Color, S);
    if (!keep_pos && (shift == 7)) strip.setPixelColor(RingSize-1, 0);
  }
}

//------------------------------ Animation: Run the sector clockwise -------------------------------------------
class rSectRing : public ANIMATION {
  public:
    rSectRing()                                     { firstPos = 255; }
    virtual void  show(uint16_t S,  uint32_t Color);
    virtual void  clean(uint16_t S, uint32_t Color);
  private:
    byte firstPos;
};

void rSectRing::show(uint16_t S, uint32_t Color) {
 const byte onedot = ((uint16_t)HZ * 60) / RingSize;

  if (S == 0) initXcolor();

  byte length = (uint32_t)S * RingSize / ((uint32_t)HZ * 60) + 1;
  byte runSecond = S % onedot;
  byte newFirstPos = RingSize * runSecond / onedot;
  char lastPos = newFirstPos - length;
  if (lastPos < 0) lastPos += RingSize;
  if (newFirstPos < RingSize) {
    if (newFirstPos != firstPos) {
      strip.setPixelColor(newFirstPos, Color);
      if (lastPos >= 0) strip.setPixelColor(lastPos, 0);
      firstPos = newFirstPos;
    } 
  }
  flashLabels(S, firstPos);
}

void rSectRing::clean(uint16_t S, uint32_t Color) {
 const uint16_t Last8Ticks = 60 * HZ - 8;

  if (S == 0) initXcolor();

  if (S > Last8Ticks) {
    S -= Last8Ticks;
    fadeRing(Color, S);
    flashLabels(S);
  } else {
    show(S, Color);
  }
}

//------------------------------ Animation: Swing the sector ---------------------------------------------------
class swingRing : public ANIMATION {
  public:
    swingRing()                                     { firstPos = 255; }
    virtual void  show(uint16_t S,  uint32_t Color);
    virtual void  clean(uint16_t S, uint32_t Color);
  private:
    byte firstPos;
};

void swingRing::show(uint16_t S, uint32_t Color) {
 const byte onedot = ((uint16_t)HZ * 60) / RingSize;

  if (S == 0) initXcolor();

  byte length = (uint32_t)S * RingSize / ((uint32_t)HZ * 60) + 1;
  byte runSecond = S % onedot;
  byte newFirstPos;
  char lastPos;
  if ((length % 2) == 0) {                          // even
    newFirstPos = RingSize - RingSize * runSecond / onedot;
    lastPos = newFirstPos + length;
    if (lastPos >= RingSize) lastPos = -1;
  } else {                                          // odd
    newFirstPos = RingSize * runSecond / onedot;
    lastPos = newFirstPos - length;
    if (lastPos < 0) lastPos = -1;
  }
  if (newFirstPos < RingSize) {
    if (newFirstPos != firstPos) {
      strip.setPixelColor(newFirstPos, Color);
      if ((lastPos >= 0) && (lastPos < RingSize))
        strip.setPixelColor(lastPos, 0);
      firstPos = newFirstPos;
    } 
  }
  flashLabels(S, firstPos);
}

void swingRing::clean(uint16_t S, uint32_t Color) {
 const uint16_t Last8Ticks = 60 * HZ - 8;

  if (S > Last8Ticks) {
    S -= Last8Ticks;
    fadeRing(Color, S);
    flashLabels(S);
  } else {
    show(S, Color);
  }
}

//------------------------------ Animation: Fill ring wth the rainbow as seconds past --------------------------
class fRainRing : public ANIMATION {
  public:
    fRainRing()                                     { firstPos = 255; }
    virtual void  show(uint16_t S,  uint32_t Color);
    virtual void  clean(uint16_t S, uint32_t Color);
  private:
    byte firstPos;
};

void fRainRing::show(uint16_t S, uint32_t Color) {
  if (S == 0) initXcolor();
  
  if (S < RingSize) strip.setPixelColor(S, 0);      // Clear the ring in the beginning of the new minutes
  for(uint16_t i = 1; i <= RingSize; ++i) {
    strip.setPixelColor(RingSize - i, Wheel(i+S));
  }
  flashLabels(S);
}

void fRainRing::clean(uint16_t S, uint32_t Color) {
 const uint16_t LastRSTicks = 60 * HZ - RingSize;

  if (S > LastRSTicks) {
    S -= LastRSTicks;
    strip.setPixelColor(S, 0);
  } else {
    show(S, Color);
  }
}

//------------------------------ Animation: Fill ring wth the another rainbow as seconds past ------------------
class aRainRing : public ANIMATION {
  public:
    aRainRing()                                     { firstPos = 255; }
    virtual void  show(uint16_t S,  uint32_t Color);
    virtual void  clean(uint16_t S, uint32_t Color);
  private:
    byte firstPos;
};

void aRainRing::show(uint16_t S, uint32_t Color) {
  if (S == 0) initXcolor();

  if (S < RingSize) strip.setPixelColor(S, 0);      // Clear the ring in the beginning of the new minutes
  for(uint16_t i = 1; i <= RingSize; ++i) {
    strip.setPixelColor(RingSize - i, Wheel(((i * 256 / RingSize) + S) & 255));
  }
  flashLabels(S);
}

void aRainRing::clean(uint16_t S, uint32_t Color) {
 const uint16_t LastRSTicks = 60 * HZ - RingSize;

  if (S > LastRSTicks) {
    S -= LastRSTicks;
    strip.setPixelColor(S, 0);
  } else {
    show(S, Color);
  }
}

//------------------------------ Animation: Wake-up signal, sunrise --------------------------------------------
class sunRise : public ANIMATION {
  public:
    sunRise()                                       { loop_number = 0; started = false; for (byte i = 0; i < 3; rgb[i++] = 0); }
    virtual void  show(uint16_t S,  uint32_t Color);
    virtual void  clean(uint16_t S, uint32_t Color);
    virtual void  handle(byte k)                    { loop_number = 0; started = false; for (byte i = 0; i < 3; rgb[i++] = 0); }
  private:
   uint16_t   loop_number;                          // [0 - 4]
   bool       started;                              // Whether the sequence started correctly
   const byte rise[6][3] = {{0, 0, 0}, {33, 2, 0}, {66, 6, 2}, {99, 18, 3}, {133, 24, 4}, {255, 50, 50}};
  
};

void sunRise::show(uint16_t S, uint32_t Color) {
  if (!started) {
    if  (S > 300) return;                           // Do not start the sequence at the end of minute
    started = true;
  }
  byte rb = map(uint32_t(loop_number) * 600 + S, 0, 1800, 1, RingSize/2);
  rb = constrain(rb, 1, RingSize/2);
  byte lb = constrain(RingSize-rb, RingSize/2+1, RingSize-1);

  for (byte i = 0; i < 3; ++i)
    rgb[i] = map(S, 0, 600, rise[loop_number][i], rise[loop_number+1][i]);

  uint32_t c = strip.Color(rgb[0], rgb[1], rgb[2]); 
  for (byte i = RingSize-1; i >= lb; --i) strip.setPixelColor(i, c);
  for (byte i = 0; i <= rb; ++i) strip.setPixelColor(i, c);
  if (S == 599) {
    if (loop_number < 4) ++loop_number;
  }
}

void sunRise::clean(uint16_t S, uint32_t Color) {
 const uint16_t LastRSTicks = 60 * HZ - RingSize;

  if (S > LastRSTicks) {
    S -= LastRSTicks;
    strip.setPixelColor(S, 0);
  } else {
    show(S, Color);
  }
}


//------------------------------ Animation: night light, fireplace ---------------------------------------------
class firePlace : public ANIMATION {
  public:
    firePlace()                                     { factor = 1; indx = 0; }
    virtual void  show(uint16_t S,  uint32_t Color);
    virtual void  clean(uint16_t S, uint32_t Color);
    virtual void  handle(byte k)                    { factor = constrain(k, 1, RingSize/2); indx = factor - 1; }
  private:
    void      addColor(uint8_t position, uint32_t color);
    void      substractColor(uint8_t position, uint32_t color);
    uint32_t  blend(uint32_t color1, uint32_t color2);
    uint32_t  substract(uint32_t color1, uint32_t color2);
    int       factor;                               // Only pixels divided by the factor are lit
    int       indx;                                 // Index of the first pixel
    const uint32_t fire_color = 0x050200;
};

void firePlace::show(uint16_t S, uint32_t Color) {
  if ((S == 599) && (--indx < 0)) indx = factor-1;  // shift lit pixels every minute
  if (S % random(1, 4)) return;
  for (byte i = 0; i < RingSize; ++i) {
    strip.setPixelColor(i, 0);
    if (((i + indx) % factor) == 0) {               // Tune the brightness by the factor value
      addColor(i, fire_color);
      byte r = random(3);
      uint32_t diff_color = strip.Color(r, r/2, r/2);
      substractColor(i, diff_color);
    }
  }  
}

void firePlace::clean(uint16_t S, uint32_t Color) {
 const uint16_t LastRSTicks = 60 * HZ - RingSize;
  if (S > LastRSTicks) {
    S -= LastRSTicks;
    strip.setPixelColor(S, 0);
  } else {
    show(S, Color);
  }
}

void firePlace::addColor(uint8_t position, uint32_t color) {
  uint32_t blended_color = blend(strip.getPixelColor(position), color);
  strip.setPixelColor(position, blended_color);
}

void firePlace::substractColor(uint8_t position, uint32_t color) {
  uint32_t blended_color = substract(strip.getPixelColor(position), color);
  strip.setPixelColor(position, blended_color);
}

uint32_t firePlace::blend(uint32_t color1, uint32_t color2) {
  byte r1,g1,b1;
  byte r2,g2,b2;

  r1 = (byte)(color1 >> 16),
  g1 = (byte)(color1 >>  8),
  b1 = (byte)(color1 >>  0);

  r2 = (byte)(color2 >> 16),
  g2 = (byte)(color2 >>  8),
  b2 = (byte)(color2 >>  0);
  return strip.Color(constrain(r1+r2, 0, 255), constrain(g1+g2, 0, 255), constrain(b1+b2, 0, 255));
}

uint32_t firePlace::substract(uint32_t color1, uint32_t color2) {
  byte r1,g1,b1;
  byte r2,g2,b2;
  int16_t r,g,b;

  r1 = (byte)(color1 >> 16),
  g1 = (byte)(color1 >>  8),
  b1 = (byte)(color1 >>  0);

  r2 = (byte)(color2 >> 16),
  g2 = (byte)(color2 >>  8),
  b2 = (byte)(color2 >>  0);

  r = (int16_t)r1 - (int16_t)r2;
  g = (int16_t)g1 - (int16_t)g2;
  b = (int16_t)b1 - (int16_t)b2;

  if(r < 0) r = 0;
  if(g < 0) g = 0;
  if(b < 0) b = 0;
  return strip.Color(r, g, b);
}

//------------------------------------------ class ALARM -------------------------------------------------------
class ALARM {
  public:
    ALARM()                                         { }
    void      init(bool act, uint16_t minutes = 0, byte wmask = 0);
    void      activate(bool a)                      { if ((active = a)) calculateAlarmTime(); }
    bool      isActive(void)                        { return active; }
    byte      wdayMask(void)                        { return wday_mask; }
    void      calculateAlarmTime(bool next = false);// Calculate the next alarm time; in next, calculate the next alarm time in one minute from the current time
    bool      isAlarmNow(void);                     // Whether the alarm should be started
    void      stopAlarm(void)                       { firing = false; }
    time_t    alarmTime(void)                       { return next_alarm; }
  private:
    time_t    next_alarm;                           // The UNIX time of the next alarm
    time_t    stop_time;                            // The time when to stop active alarm
    uint16_t  alarm_time;                           // The minutes from midnight to fire the alarm
    byte      wday_mask;                            // The week day bitmask to fire the alarm 0 - sunday, 7 saturday
    bool      active;                               // Whether the alarm is active
    bool      firing;                               // Whether tha alarm is firing right now
};

void ALARM::init(bool act, uint16_t minutes, byte wmask) {
  active = act;
  if (minutes >= 24*60) minutes = 24*60-1;
  alarm_time = minutes;
  wday_mask  = wmask & 0b01111111;
  if (active) calculateAlarmTime();
}

bool ALARM::isAlarmNow(void) {
  time_t n = now();
  if (firing) {
    if (n >= stop_time) {                           // The time to stop alarm
      firing = false;
      stop_time = 0;
    }
    return firing;
  }
  if (!active) return firing;                       // There is no active alarm

  byte S = n % 60;
  if (!firing && (S <= 10) && (n - next_alarm) <= 10) {
    firing = true;                                // It is Time to fire the alarm
    stop_time = next_alarm + 300;                 // Set the time to stop alarm firing (5 minutes)
    calculateAlarmTime(true);
  }
    
  return firing;
}

void ALARM::calculateAlarmTime(bool next) {
  tmElements_t tm;
  next_alarm = 0;
  if (!active) return;

  time_t n = RTC.get();                             // RTC.read load weekday incorectly
  if (next) n += 60;                                // Calculate next alarm in one minute of the current time
  breakTime(n, tm);
  tm.Second = 0;
  long dm = long(tm.Hour) * 60 + tm.Minute;         // The time in minutes since midnight

  long delta_minutes = long(alarm_time) - dm;
  byte delta_days = 0;
  if (wday_mask) {                                  // This alarm should be repeated some week days
    byte m = 1;
    delta_days = 8;
    for (byte j = 0; j < 7; ++j) {
      if (wday_mask & m) {                          // Fire the alarm in j - day 
        char d = j + 1 - tm.Wday;                   // sunday is 1
        if (d < 0) d += 7;
        if ((d == 0) && (delta_minutes <= 0)) d = 7;  // Today is late for this alarm, we should fire it in a week
        if (d < delta_days) delta_days = d;         // Take the minimum value
      }
      m <<= 1;
    }
  }
  if ((delta_minutes <= 0) && (wday_mask == 0)) delta_minutes += 24 * 60;
  uint32_t a_delta = 86400 * delta_days + delta_minutes * 60;
  next_alarm = makeTime(tm) + a_delta;
  return;
}

//------------------------------------------ class DAYTIME -----------------------------------------------------
class DAYTIME {
  public:
    DAYTIME()                                       {}
    void        init(byte morning, byte evening);
    bool        isDark(void);
    void        updateDayTime(void);
  private:
    void        rebuild(void);
    time_t      r_start_time;                       // When the nextion ring is switched-on
    time_t      r_stop_time;                        // When the nextion ring is switched-off
    time_t      next_midnight;                      // The time to recalculate start and stop times
    byte        t_morning, t_evening;               // Morning and evening times in 10-minutes intervals
};

void DAYTIME::init(byte morning, byte evening) {
  t_morning = morning;
  t_evening = evening;
  rebuild(); 
  return;
}

void DAYTIME::rebuild(void) {
  time_t last_midnight = RTC.get();
  last_midnight -= last_midnight % 86400;           // time of the last midnight
  next_midnight  = last_midnight + 86400;
  r_start_time = last_midnight + uint32_t(t_morning) * 600;
  r_stop_time  = last_midnight + uint32_t(t_evening) * 600;
  return;
}

bool DAYTIME::isDark(void) {
   #ifdef DISABLE_DARK
   return false;
   #endif
   time_t nowRTC = now();
   if (nowRTC > next_midnight) {                    // New day, recalculate start and stop times
     rebuild();
   }
   return ((nowRTC < r_start_time) || (nowRTC > r_stop_time));
}

void DAYTIME::updateDayTime(void) {
  time_t at = now();
  if (at < r_start_time) {                          // Alarm is before nextion ring start time
    r_start_time = at;
  } else if ((at < next_midnight) && (at > r_stop_time)) {
      r_stop_time = at + 600;                       // 10 minutes after alarm time
  }
}

//------------------------------------------ class SCREEN ------------------------------------------------------
class SCREEN {
  public:
    SCREEN* next;
    SCREEN* nextL;

    SCREEN() {
      next = nextL = 0;
    }
    virtual void init(void) { }
    virtual void show(void) { }
    virtual SCREEN* menu(void) {if (this->next != 0) return this->next; else return this; }
    virtual SCREEN* menu_long(void) { if (this->nextL != 0) return this->nextL; else return this; }
    virtual void inc(void) { }
    virtual void inc_long(void) { }
};


class clockSCREEN: public SCREEN, public ALARM, public DAYTIME {
  public:
    clockSCREEN(TMDSPL* D, CONFIG* CFG, ANIMATION* a[], byte asize) : SCREEN(), ALARM(), DAYTIME() {
      pD        = D;
      pCfg      = CFG;
      anim      = a;
      a_size    = asize;
      mS        = HZ;
      S         = 0;
      next_sec  = disp_mode_change = ring_mode_change = synchronize = 0;
      night_light_brightness = 3;
      update_config = 0;
      ring_on   = true;
    }
    virtual SCREEN* menu(void);
    virtual SCREEN* menu_long(void);
    virtual void init(void);
    virtual void show(void);
    virtual void inc(void)                          { night_light = !night_light; }
    virtual void inc_long(void);
  private:
    void        changeDisplayMode(byte mode = 10);
    time_t      nextAlarm(time_t start_time, byte week_mask);
    ANIMATION*  newAnimation(void);                 // Change neopixel ring animation
    void        loadConfig(void);                   // load configuration, calculate start and stop times
    void        updateConfig(void);                 // Update alarm and night_light in the config
    TMDSPL*       pD;                                 // The pointer to the LED DISPLAY instance
    CONFIG*     pCfg;                               // The pointer to the configuration instance
    ANIMATION** anim;                               // Array of pointers to the animation
    ANIMATION*  curr_animation;                     // The current animation
    byte        a_size;                             // The number of the animations in the array 
    uint32_t    synchronize;                        // The time in ms to perform time synchronization with RTC
    uint32_t    next_sec = 0;                       // The time in ms to perform next animation step
    uint32_t    disp_mode_change;                   // The time in ms to change the displayed information
    uint32_t    ring_mode_change;                   // The time in ms to change new animation
    uint32_t    update_config;                      // The time in ms when to rewrite the config in EEPROM
    byte        mS, S, M, H;                        // tenth-of the seconds, Seconds, Minutes, Hours
    byte        disp_mode;                          // Display info: 0 - clock, 1 - day&month, 2 - alarm time, 3 - alarm week day mask
    bool        ring_on;                            // Whether the neopixel ring is enabled
    bool        night_light;                        // Whether the neopixel ring is used as night light
    bool        show_ring;                          // Whether the neopixel ring is on
    byte        dot_mask;                           // Where the dot should be displayed
    byte        night_light_brightness;             // The brightness of night light [1 - 6], 1 - the highest
    const byte     deltaMS = (1000 / HZ);
    const uint32_t sync_period = 60000;             // Synchronize the system clock every minute with RTC
};

void clockSCREEN::init(void) {
  loadConfig();
  curr_animation  = newAnimation();
  curr_animation->switchOffRing();
  time_t nowRTC = RTC.get();
  setTime(nowRTC);
  S = second();
  M = minute();
  H = hour();
  pD->showTime(H, M);
  changeDisplayMode(0);
  update_config = 0;
  synchronize = 0;
}

SCREEN* clockSCREEN::menu(void) {
  if (ALARM::isAlarmNow()) {
    ALARM::stopAlarm();
    return this;
  }
  
  if (night_light && isDark()) {
    while (RingSize % ++night_light_brightness) {
      if (night_light_brightness > RingSize/3)
        night_light_brightness = 0;
    }
    anim[1]->handle(night_light_brightness);
  } else {
    ring_on = !ring_on;                             // Toggle the neopixel ring
    if (ring_on) curr_animation = newAnimation();
  }
  return this;
}

SCREEN* clockSCREEN::menu_long(void) {
  if (this->nextL != 0) {
    if (update_config) updateConfig();
    return this->nextL;
  } else 
    return this;
}

void clockSCREEN::inc_long(void) {                  // Toggle the alarm
  bool a = ALARM::isActive();
  ALARM::activate(!a);
  update_config = millis() + 30000;                 // Rewrite the config in 30 seconds  
}

void clockSCREEN::show(void) {
  bool is_dark = isDark();                          // Going to often use this value in this function
 
  // First, show the animation
  uint32_t nowMS = millis();
  if (nowMS >= next_sec) {                          // It is time to update the ring
    next_sec = nowMS + deltaMS;                     // Several times per second
    ++mS;
    if (mS >= HZ) {
      if (disp_mode == 0) dot_mask ^= 0b100;
      mS = 0;
      if (++S >= 60) {
        S = second();
        M = minute();
        H = hour();
        changeDisplayMode(0);
      }
    }
    if ((S == 0) && nowMS >= ring_mode_change)
      curr_animation = newAnimation();

    if (show_ring) {
      byte loop = H * 60 + M;
      if (M % 2) loop += 86;
      uint16_t tick = S * HZ + mS;
      uint32_t color = curr_animation->Wheel(loop);
      if ((nowMS + 5000) > ring_mode_change)
        curr_animation->clean(tick, color);
      else
        curr_animation->show(tick, color);
      strip.show();
    }

  } else {                                          // No need to animate the ring, perform all another tasks
    // Sometimes show another information: date and alarm time
    nowMS = millis();
    #ifndef NO_SHOW_DATE_AND_ALARM
    if (nowMS >= disp_mode_change)
      changeDisplayMode();
    #endif

    bool alrm = ALARM::isAlarmNow();                // check for the alarm time
    if (alrm && (curr_animation != anim[0])) {
      curr_animation = anim[0];                     // start the alarm animation
      curr_animation->handle(0);                    // Initialize the sequence
      curr_animation->switchOffRing();
      ring_mode_change = nowMS + 300000;            // Alarm during next 5 minutes
      DAYTIME::updateDayTime();                     // update DAYTIME interval to include the current alarm
      ring_on = show_ring = true;
      if (ALARM::wdayMask() == 0) {                 // Do not alarm next day if the week day mask not setup
        ALARM::activate(false);
        updateConfig();
      }
    }
    if (!ring_on || is_dark) {                      // Night time
      if (!night_light && show_ring && !alrm) {
        curr_animation->switchOffRing();
        show_ring = false;
      } else if (night_light && !alrm) {
        curr_animation = anim[1];                   // Night light
        ring_mode_change = nowMS + 60000;           // Animation for one minute
        show_ring = true;
      }
    } else {
      show_ring = true;
    }

    if (nowMS > synchronize) {
      time_t nowRTC = RTC.get();
      setTime(nowRTC);
      synchronize = millis() + sync_period;
    }
    
    if (update_config && (nowMS > update_config)) updateConfig();
  }

  // Last, show the information on the LED display
  pD->show(dot_mask);
  if (is_dark) {                                    // Dim the led display at night period
    if (!show_ring)
      delay(10);
    else if (night_light)
      delay(6);
  }
}

ANIMATION* clockSCREEN::newAnimation(void) {
  byte ring_mode   = random(2, a_size);             // Do not select sunrise and fireplace for animation
  ring_mode_change = millis() + (uint32_t(random(120, 600)) * 1000);
  ANIMATION* an    = anim[ring_mode];
  an->handle(random(1));
  return an;
}

void clockSCREEN::updateConfig(void) {
   struct cfg conf;
   pCfg->getConfig(conf);
   conf.active = ALARM::isActive();
   if (night_light)
     conf.nl_br  = night_light_brightness;
   else
     conf.nl_br  = 0;
   pCfg->saveConfig(conf);
   update_config = 0;
}

void clockSCREEN::loadConfig(void) {
  struct cfg conf;
  pCfg->getConfig(conf);
  disp_mode       = 2;
  dot_mask        = 0b100;
  show_ring       = false;
  night_light     = conf.nl_br > 0;
  night_light_brightness = constrain(conf.nl_br, 1, RingSize/3);
  if (night_light) anim[1]->handle(night_light_brightness);
  ALARM::init(conf.active, conf.alarm_time, conf.wday_mask);
  DAYTIME::init(conf.morning, conf.evening);
  anim[1]->handle(night_light_brightness);          // Setup the brightness of night light
}

void clockSCREEN::changeDisplayMode(byte mode) {
  uint32_t nowMS = millis();
  if (mode > 3) {                                   // Caled without an argument, automatically switch through modes
    if (S >= 50) {                                  // Do not switch mode late
      disp_mode = 0;
    } else {
      ++disp_mode;
      if (disp_mode > 3) disp_mode = 0;
      if ((disp_mode == 3) && !ALARM::isActive())
        disp_mode = 0;
    }
  } else {
    disp_mode = mode;
  }

  time_t next_alarm = ALARM::alarmTime();
  switch (disp_mode) {
    case 1:                                         // Show the date (day, month)
      pD->showTime(day(), month());
      dot_mask = 0b100;
      disp_mode_change = nowMS + 3000;
      break;
    case 2:                                         // Alarm time
      if (ALARM::isActive() && (next_alarm - now()) < 86400) {
        pD->showTime(hour(next_alarm), minute(next_alarm));
        dot_mask = 0b101;
      } else {
        pD->showDash();
        dot_mask = 0;
      }
      disp_mode_change = nowMS + 3000;
      break;
    case 3:                                         // Alarm week day mask
      pD->showWdayGeneral(ALARM::wdayMask());
      dot_mask = 0;
      disp_mode_change = nowMS + 3000;
      break;
    default:                                        // Hour and Minute
      pD->showTime(H, M);
      dot_mask = 0b100;
      disp_mode_change = nowMS + (uint32_t(random(20, 40)) * 1000);
      break;
  }
}

#ifdef NO_SHOW_DATE_AND_ALARM
#define EDIT_ENTRY_MAX 2
#else
#define EDIT_ENTRY_MAX 11
#endif
//------------------------------------------ class clock setup -------------------------------------------------
class clockSetupSCREEN: public SCREEN {
  public:
    clockSetupSCREEN(TMDSPL* D, CONFIG* CFG) : SCREEN() {
      pD          = D;
      pCfg        = CFG;
      next_sec    = 0;
      edit_entity = 0;
      show_digit  = true;
    }
    virtual SCREEN* menu(void) {
      ++edit_entity;
      if (edit_entity >= EDIT_ENTRY_MAX) edit_entity = 0;
      refresh = true;
      return this;
    }
    virtual SCREEN* menu_long(void);
    virtual void    inc(void);
    virtual void    init(void);
    virtual void    show(void);
  private:
    TMDSPL          *pD;                              // Pointer to the LED display instance
    CONFIG*       pCfg;                             // The pointer to the configuration instance
    uint32_t      next_sec;
    uint32_t      edit_timeout;
    bool          refresh;                          // Whether refresh the screen
    bool          show_digit;
    byte          edit_entity;                      // 0 - Hour, 1 - Minute, 2 - Day, 3 - Month, 4 - Year,
                                                    // 5 - morning time, 6 - evening time,
                                                    // 7- Alarm hour, 8 - Alarm minute,
                                                    // 9 - Weekday mask general, 10 - Weekday mask HEX
    tmElements_t  tm;                               // time to be set-up
    byte          days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    struct cfg conf;
    bool          change_time;
    bool          rewrite_config;                   // Whether the config was modified
    const byte wday_general[4] = {0b0000000, 0b00111110, 0b0011110, 0b1111111};
    const uint16_t timeout = 10000;
};

void clockSetupSCREEN::init(void) {
  pCfg->getConfig(conf);
  rewrite_config  = false;
  change_time     = false;
  RTC.read(tm);
  edit_entity = 0;
  next_sec = millis();
  refresh = true;
}

SCREEN* clockSetupSCREEN::menu_long(void) {
  if (nextL != 0) {
    if (rewrite_config) {
      pCfg->saveConfig(conf);
    } else if (change_time) {
      tm.Second = 0;
      RTC.write(tm);
    }
    return nextL;
  } else return this;
}


void clockSetupSCREEN::show(void) {
  uint32_t nowMS = millis();
  
  if (nowMS >= next_sec) {
    next_sec = nowMS + 200;                         // Blink with digit
    show_digit = !show_digit;
  }
  
  byte DisplayMask = 0b1111;
  byte Cent = (tm.Year + 1970) / 100;
  byte Year = (tm.Year + 70)   % 100;
  strip.setPixelColor(RingSize/4*3, 0);
  strip.setPixelColor(RingSize/4, 0);
  switch (edit_entity) {
   case 0:                                          // Hour
     if (refresh) pD->showTime(tm.Hour, tm.Minute);
     if (!show_digit) DisplayMask &= 0b0011;
     pD->show(0b100, DisplayMask);
     break;
   case 1:                                          // Minute
     if (refresh) pD->showTime(tm.Hour, tm.Minute);
     if (!show_digit) DisplayMask &= 0b1100;
     pD->show(0b100, DisplayMask);
     break;
   case 2:                                          // Day
     if (refresh) pD->showTime(tm.Day, tm.Month);
     if (!show_digit) DisplayMask &= 0b0011;
     pD->show(0b100, DisplayMask);
     break;
   case 3:                                          // Month
     if (refresh) pD->showTime(tm.Day, tm.Month);
     if (!show_digit) DisplayMask &= 0b1100;
     pD->show(0b100, DisplayMask);
     break;
   case 4:                                          // Year
     if (refresh) pD->showTime(Cent, Year);
     if (!show_digit) DisplayMask &= 0b1100;
     pD->show(0, DisplayMask);
     break;
   case 5:                                          // Morning time
     if (refresh) pD->showTime(conf.morning/6, (conf.morning % 6) * 10); 
     if (!show_digit) DisplayMask &= 0b1100;
     pD->show(0b100, DisplayMask);
     strip.setPixelColor(RingSize/4*3, 0x0F0000);
     break;
   case 6:                                          // Evening time
     if (refresh) pD->showTime(conf.evening/6, (conf.evening % 6) * 10); 
     if (!show_digit) DisplayMask &= 0b1100;
     pD->show(0b100, DisplayMask);
     strip.setPixelColor(RingSize/4, 0x0F0000);
     break;
   case 7:                                          // Alarm hour
     if (refresh) pD->showTime(conf.alarm_time / 60, conf.alarm_time % 60);
     if (!show_digit) DisplayMask &= 0b0011;
     pD->show(0b101, DisplayMask);
     break;
   case 8:                                          // Alarm minute
     if (refresh) pD->showTime(conf.alarm_time / 60, conf.alarm_time % 60);
     if (!show_digit) DisplayMask &= 0b1100;
     pD->show(0b101, DisplayMask);
     break;
   case 9:                                          // Alarm weekday mask general: none, work-day, full-week
     pD->showWdayGeneral(conf.wday_mask);
     pD->show(0, 0b1111);
     break;
   case 10:                                         // Alarm weekday mask full mode
     pD->showWdayFull(conf.wday_mask);
     pD->show(0, 0b1111);
     break;
  }
  refresh = false;
  strip.show();
}

void clockSetupSCREEN::inc(void) {
  byte minut;
  switch(edit_entity) {
    case 0:                                         // Hour
      ++tm.Hour; if (tm.Hour > 23) tm.Hour = 0;
      change_time = true;
      break;
    case 1:                                         // Minute
      ++tm.Minute; if (tm.Minute > 59) tm.Minute = 0;
      change_time = true;
      break;
    case 2:                                         // Day
      days[1] = 28;
      if (((tm.Year+1970) / 4) == 0) days[1] = 29;  // Leap Year 
      ++tm.Day;
      if (tm.Day > days[tm.Month-1]) tm.Day = 1;
      change_time = true;
      break;
    case 3:                                         // Month
      ++tm.Month;
      if (tm.Month > 12) tm.Month = 1;
      pD->showTime(tm.Day, tm.Month);
      change_time = true;
      break;
    case 4:                                         // Year
      ++tm.Year;
      if (tm.Year > 60) tm.Year = 30;
      change_time = true; 
      break;
    case 5:                                         // Morning Time
      if (++conf.morning >= 72) conf.morning = 0;   // 0:00 - 11:50
      rewrite_config = true;
      break;
    case 6:                                         // Evening time
      if (++conf.evening >= 144) conf.evening = 72; // 12:00 - 23:50
      rewrite_config = true;
      break;
    case 7:                                         // Alarm hour
      conf.alarm_time += 60;
      conf.alarm_time %= 1440;
      rewrite_config = true;
      conf.active = true;
      break;
    case 8:                                         // Alarm minute
      minut = conf.alarm_time % 60;
      conf.alarm_time -= minut;
      if (++minut >= 60) minut = 0;
      conf.alarm_time += minut;
      conf.active = true;
      rewrite_config = true;
      break;
    case 9:
      for (minut = 0; minut < 4; ++minut)           // Weekday mask, general. minut here is just an index
        if (conf.wday_mask == wday_general[minut]) break;
      if(++minut >= 4) minut = 0;
      conf.wday_mask = wday_general[minut];
      break;
    case 10:                                        // Weekday mask bit-by-bit
      if(++conf.wday_mask >= 0b1111111) conf.wday_mask = 0;
      break;
  }
  tm.Second = 0;
  time_t nt = makeTime(tm);
  breakTime(nt, tm);
  refresh = true;
}

// ================================ End of all class definitions ====================================== 
TMDSPL            dspl(tmDataPin, tmClockPin);      // 7segment display with TM1637

sunRise           aSunRise;                         // Alarm sequence
firePlace         fPlace;                           // night light

// Neopixel ring animations
FillRing          fill;
cClockRing        cClock;
lpClockRing       lpClock;
rsRing            riseSlowly;
rSectRing         rSect;
swingRing         swing;
fRainRing         fillRain;
aRainRing         anotherRain;
ANIMATION*        anim[] = { &aSunRise, &fPlace, &fill, &cClock, &lpClock, &riseSlowly, &rSect, &swing, &fillRain, &anotherRain };
const byte a_size = sizeof(anim) / sizeof(ANIMATION*);

CONFIG            clockCfg;
clockSCREEN       mainClock(&dspl, &clockCfg, anim, a_size);
clockSetupSCREEN  setupClock(&dspl, &clockCfg);

BUTTON menuButton(BTN_MENU_PIN);
BUTTON incrButton(BTN_INCR_PIN);

SCREEN* pScreen = &mainClock;


void setup() {
  //Serial.begin(9600);            
  dspl.init();
  
  time_t nowRTC = RTC.get();
  randomSeed(nowRTC);
  setTime(nowRTC);
  
  menuButton.init();
  incrButton.init();
  incrButton.setTimeout(10000);
  attachInterrupt(digitalPinToInterrupt(BTN_MENU_PIN), menuButtonChange, CHANGE);
  
  mainClock.nextL  = &setupClock;
  setupClock.nextL = &mainClock;

  strip.begin();
  strip.setBrightness(0xff);
  delay(500);
  strip.show();                                     // Initialize all pixels to 'off'

  // Load configuration data
  clockCfg.init();
  clockCfg.load();
  pScreen->init();
}

// Interrupts functions just call the corresponding method
void menuButtonChange(void) {
  menuButton.buttonCnangeINTR();
}

//================================================================================
// The Main LOOP                                                                  
//================================================================================
void loop() {
 static unsigned long return_to_main = 0;
 static bool RingOff = false;

  bool buttonPressed = false;

   if ((pScreen != &mainClock) && millis() > return_to_main) {
     return_to_main = 0;
     pScreen = &mainClock;
   }

  // Check status of the main button, that supports interrupts
  if (byte Mode = menuButton.intButtonStatus()) {
    buttonPressed = true;
    SCREEN* nextScreen = pScreen;
    if (Mode == 1) nextScreen = pScreen->menu();
    if (Mode == 2) nextScreen = pScreen->menu_long();
    if (pScreen != nextScreen) {
      pScreen = nextScreen;
      pScreen->init();
    }
  }
  // Check status of the increment button
  if (byte Mode = incrButton.buttonCheck()) {
    buttonPressed = true;
    if (Mode == 1) pScreen->inc();
    if (Mode == 2) pScreen->inc_long();
  }  

  if (pScreen == &setupClock)
    if (incrButton.buttonTick()) pScreen->inc();
    
  if (pScreen != &mainClock) {
      if (!RingOff) {
        fill.switchOffRing();
        RingOff = true;
      }
      if (buttonPressed == true) return_to_main = millis() + 30000;
  } else
    RingOff = false;

   pScreen->show();
}

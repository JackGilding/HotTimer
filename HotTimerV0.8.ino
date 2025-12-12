/*
Jack Gilding maker@backroad.com.au
2025-12-12 HotTimer v0.800

MIT License

Copyright (c) 2025 Jack Gilding

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <EEPROM.h>
#include <TimeLib.h>
#include <DS3231.h>
#include <Wire.h>
#include <TFT.h>
#include <SPI.h>
///////////////
// CONSTANTS //
///////////////

// pin definitions
const byte CS_PIN = 10;
const byte DC_PIN = 9;
const byte RST_PIN = 8;
const byte RELAY0_PIN = 7;  // control pin for 5V relay
const byte MENU_PIN = 2;
const byte PREV_PIN = 3;
const byte NEXT_PIN = 4;
const byte LESS_PIN = 5;
const byte MORE_PIN = 6;

// constant names for menu navigation options
const byte ZERO = 0;  // no button press
const byte MENU = 1;
const byte PREV = 2;
const byte NEXT = 3;
const byte LESS = 4;
const byte MORE = 5;

// constant names for set_pos
const byte SET_YY = 1;
const byte SET_MNTH = 2;
const byte SET_DD = 3;
const byte SET_HH = 4;
const byte SET_MM = 5;
const byte SET_SS = 6;


const bool RESET_RTC = false;  // set false to leave RTC as it is, true to change time

// Assign human-readable names to some common 16-bit color values: (note these values have been edited for Temu TFT misinterpretation)
#define BLACK 0x0000
#define BLUE 0xF800
#define RED 0x001F
#define GREEN 0x07E0
#define YELLOW 0x07FF
#define WHITE 0xFFFF
#define ORANGE 0x037F

// hard coded screen positions - currently 128 x 160 screen (0-127, 0-159)
const byte X0 = 0;
const byte X1 = 4;  // changed from 5
const byte X2 = 8;  // changed from 10
const byte X3 = 12;
const byte X9 = 120;
const byte TFTWIDTH = 128;
const byte TFTHEIGHT = 160;

const byte Y0 = 0;
const byte Y1 = 5;
const byte Y2 = 25;
const byte Y3 = 50;
const byte Y4 = 55;
const byte Y5 = 75;
const byte Y6 = 98;
const byte Y7 = 102;
const byte Y8 = 115;
const byte Y9 = 129;  // start of area below existing 128 x 128 messages
byte tariffheight = Y6 - Y3 + 1;

// character size constants
const byte SIZE1X = 6;   // width including space
const byte SIZE1Y = 10;  // height including space
const byte SIZE2X = 12;  // width including space
const byte SIZE2Y = 20;  // height including space

// const byte boost_seconds = 5; // much shorter for testing
const int boost_seconds = 600;  // 10 minutes
int boost_togo = 0;

//////////////////////
// GLOBAL VARIABLES //
//////////////////////

bool itis_solar = false;  // current time is within solar on and off times
bool itis_dst;            // flags if dayalight saving in operation

bool centuryBit;  // needed for myRTC.getMonth()
bool h12;         // needed for myRTC.getHour()
bool hPM;         // needed for myRTC.getHour()

// defaults will be overwritten if valid values stored in EEPROM
byte use_solar = 1;  // set zero to turn on relay at all off-peak times

// set these in standard time NOT daylight saving time
byte solar_on1hh = 11;  // hours in 24 hour format
byte solar_on1mm = 02;
byte solar_off1hh = 15;
byte solar_off1mm = 59;

byte solar_on2hh = 21;
byte solar_on2mm = 3;
byte solar_off2hh = 21;  // set off time same as on time to not use second boost period
byte solar_off2mm = 58;


int yyyy;  // full four digit year (but DS3231 set as single byte representing years since 2000)
int yy;    // year as two digits (20 to 99)
byte mnth = 99;
byte dd;       // date 1-31
byte dow = 8;  // day of the week 0=Sun, 1-5=weekdays, 6=Sat
byte hh;       // hour in 24 hour clock 0-23
byte hh_dst;   //only the hour changes with DST
byte mm;       // minutes 0-59
byte ss;       // seconds 0-59

byte mm_before;  // see function reset_befores()
byte ss_before;
byte tariff_before;

// variables for RTC menu
byte value;  //value of currently being set register
byte pos;    // position being displayed – compare to set_pos to see if highlight
byte min_value;
byte max_value;
byte nav;      // most recent selection on navigation button
byte set_pos;  // default for checking is solar_on1hh

/* set_pos values (use same for EEPROM positions): 
use_solar    = 7 

solar_on1hh  = 8
solar_on1mm  = 9
solar_off1hh = 10
solar_off1mm = 11
solar_on2hh  = 12
solar_on2mm  = 13
solar_off2hh = 14
solar_off2mm = 15
*/

byte ee_value;  // value returned from EEPRP.read()

String tariff_ends = "(not set)";  // how long does current tariff last?
int tariff = 0;                    //0=offpeak, 1=shoulder, 2=peak

byte max_days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };  // max days in the month
char dow_full[][9 + 1] = { " Sunday  ", " Monday  ", " Tuesday ", "Wednesday", " Thursday", " Friday  ", "Saturday " };
char mnth_short[][3 + 1] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
char tariff_name[][10 + 1] = { " off-peak ", " shoulder ", "   peak   " };
char time_label[][8 + 1] = { " year:20", "  month:", "   date:", "   hour:", " minute:", " second:" };


DS3231 myRTC;
TFT TFTscreen = TFT(CS_PIN, DC_PIN, RST_PIN);


///////////
// Setup //
///////////

void setup() {
  // Start the I2C interface
  Wire.begin();
  Serial.begin(9600);
  TFTscreen.begin();

  // clear the screen with a black background
  TFTscreen.setRotation(0);  // ( I believe 0=portrait, 1=landscape, 2&3 are p and l inverted)
  TFTscreen.background(BLACK);

  pinMode(RELAY0_PIN, OUTPUT);
  pinMode(MENU_PIN, INPUT_PULLUP);
  pinMode(PREV_PIN, INPUT_PULLUP);
  pinMode(NEXT_PIN, INPUT_PULLUP);
  pinMode(LESS_PIN, INPUT_PULLUP);
  pinMode(MORE_PIN, INPUT_PULLUP);


  // see separate document for other settings to test operation
  if (RESET_RTC) {
    myRTC.setYear(25);  // single byte representing year since 2000
    myRTC.setMonth(6);
    myRTC.setDate(25);
    myRTC.setHour(16);
    myRTC.setMinute(28);
    myRTC.setSecond(55);
  }
  reset_befores();
  get_eeproms();   //read EEPROM values, use if valid, otherwise set to defaults above
  show_startup();  // display start up message - continues after delay
}

///////////////
// main loop //
///////////////
void loop() {
  update_times();
  nav = get_nav();
  if (nav == MENU) {
    menurtc_do();
    update_times();
    TFTscreen.background(BLACK);

    nav = get_nav();  // may not need this - not sure what value of nav is after menu
    reset_befores();
  }

  // was subtle error here, have to set itis_dst before calling get_tariff()
  // error only shows when coming out of set RTC.
  itis_dst = isit_dst();
  tariff = get_tariff();


  // Set relay
  relay_check();  // will set relay after checking

  // adjust boost
  if (nav == MORE) {
    boost_togo = (boost_togo + boost_seconds);
  }
  if (nav == LESS) {
    boost_togo = 0;
  }


  // things to do if minute has changed
  // ==================================
  if (mm != mm_before) {
    date_show();
    time_show();
    tariff_show();
    TFTscreen.fillRect(X1, Y7, X9 - X1 + 1, SIZE1Y, BLACK);  // clear the AEST area every minute in case DST just ended
    // should this just be clear whole line?
    aest_show();  // display AEST time at bottom if itis_dst
    mm_before = mm;
  }

  // things to do if second has changed
  // ==================================
  if (ss != ss_before) {
    seconds_show();
    // clear boost line every time
    TFTscreen.fillRect(X1, Y8, X9 - X1 + 1, SIZE1Y, BLACK);
    // decrement and display boost to go
    boost_show();
    // display AEST seconds if is DST
    aestsec_show();
  }
  ss_before = ss;
}

// =====================
// ===== FUNCTIONS =====
// =====================

// TIME AND TARIFF FUNCTIONS
////////////////////////////

// update date and time variables from RTC
void update_times() {
  DateTime rtcnow = RTClib::now();
  time_t t = rtcnow.unixtime();
  yyyy = year(t);
  yy = yyyy - 2000;
  mnth = month(t);
  dd = day(t);
  hh = hour(t);
  mm = minute(t);
  ss = second(t);
  dow = weekday(t) - 1;
  return;
}

void date_show() {
  TFTscreen.fillRect(X0, Y1, TFTWIDTH, SIZE2Y, BLACK);
  if (hh == 23 && itis_dst) {
    // do nothing
  } else {
    TFTscreen.setTextSize(2);
    TFTscreen.setTextColor(WHITE);
    TFTscreen.setCursor(X1, Y1);
    TFTscreen.print(get_date());
  }
  dow_show();
}

void dow_show() {
  // put dow in full at bottom of main screen
  TFTscreen.fillRect(X0, TFTHEIGHT - SIZE2Y, TFTWIDTH, SIZE2Y, BLUE);
  TFTscreen.setTextSize(2);
  TFTscreen.setTextColor(WHITE);
  TFTscreen.setCursor(X2, TFTHEIGHT - SIZE2Y + 2);
  // display day (or tomorrow if DST and after 11pm)
  if (hh == 23 && itis_dst) {
    byte f_nextdow = dow + 1;
    if (f_nextdow == 7) { f_nextdow = 0; }
    TFTscreen.print(dow_full[f_nextdow]);
  } else {
    TFTscreen.print(dow_full[dow]);
  }
}
void tariff_show() {  // draws tariff box, display tariff name and end time
  // draw the tariff box
  if (tariff == 0) {
    TFTscreen.fill(GREEN);
  } else if (tariff == 1) {
    TFTscreen.fill(ORANGE);
  } else {
    TFTscreen.fill(RED);
  }
  TFTscreen.noStroke();                            // don't draw a line around the next rectangle
  TFTscreen.rect(X0, Y3, TFTWIDTH, tariffheight);  //draw a rectangle across the screen

  // draw the tariff name
  TFTscreen.setTextSize(2);
  TFTscreen.stroke(BLACK);
  TFTscreen.text(tariff_name[tariff], X1, Y4);
  TFTscreen.setCursor(X1, Y5);
  TFTscreen.print(tariff_ends);
}

void time_show() {
  // erase and re-display time (without seconds)
  // -------------------------------------------
  TFTscreen.fillRect(X1, Y2, X9 - X1 + 1, SIZE2Y, BLACK);
  TFTscreen.setTextSize(2);
  TFTscreen.setCursor(X1, Y2);
  if (itis_dst) {
    TFTscreen.setTextColor(YELLOW);
    if (hh == 23) {
      TFTscreen.print(get_display((hh - 23), mm));
    } else {
      TFTscreen.print(get_display((hh + 1), mm));
    }
  } else {
    TFTscreen.setTextColor(WHITE);
    TFTscreen.print(get_display(hh, mm));
  }
}

void seconds_show() {
  // erase and re-display seconds only
  // -------------------------
  TFTscreen.fillRect(X1 + (SIZE2X * 6), Y2, 24, 20, BLACK);  // blank seconds area. seconds come after 6 characters at size 2
  TFTscreen.setTextSize(2);
  TFTscreen.setCursor(X1 + (SIZE2X * 6), Y2);
  if (itis_dst) {
    TFTscreen.setTextColor(YELLOW);
    TFTscreen.print(pad(ss));
  } else {
    TFTscreen.setTextColor(WHITE);
    TFTscreen.print(pad(ss));
  }
}

void aest_show() {
  // display AEST hours and minutes if is DST
  if (itis_dst) {
    TFTscreen.fillRect(X1, Y7, X9 - X1 + 1, SIZE1Y, BLACK);
    // should this just be clear whole line?
    TFTscreen.setTextSize(1);
    TFTscreen.setTextColor(WHITE);
    TFTscreen.setCursor(X1, Y7);
    TFTscreen.print("AEST: ");
    TFTscreen.print(get_display(hh, mm));
  }
}

void aestsec_show() {
  if (itis_dst) {
    // "AEST: hh:mm:" = 12 chars * 6w = 72
    TFTscreen.setTextSize(1);
    TFTscreen.setTextColor(WHITE);
    TFTscreen.fillRect(X2 + (12 * SIZE1X), Y7, (2 * SIZE1X), SIZE1Y, BLACK);  //blank seconds area: 1 char high * 2 chars wide
    TFTscreen.setCursor(X2 + (12 * SIZE1X), Y7);
    TFTscreen.print(pad(ss));
  }
}

void boost_show() {
  if (boost_togo > 0) {
    boost_togo = boost_togo - 1;
    int boost_hh = (boost_togo / 3600);
    int boost_mm = ((boost_togo - (boost_hh * 3600)) / 60);
    int boost_ss = (boost_togo % 60);
    TFTscreen.setTextSize(1);
    TFTscreen.setTextColor(WHITE);
    TFTscreen.setCursor(X1, Y8);
    TFTscreen.print("Boost: ");
    TFTscreen.print(get_display(boost_hh, boost_mm));
    TFTscreen.print(pad(boost_ss));
  }
}

// function to find what tariff period we are in
// hard coded for Aurora tariff 93 in standard time
// only ever returns 0 or 2
// need extra code for shoulder tariff returns 1 or different time periods
int get_tariff() {
  byte f_returntariff;
  // is it weekend?
  if ((dow == 0) || (dow == 6) || ((dow == 5) && (hh >= 21))) {  //offpeak from 9pm Fri and all weekend
    if (itis_dst) {
      tariff_ends = "to 8am Mon";
    } else {
      tariff_ends = "to 7am Mon";
    }
    f_returntariff = 0;
  } else {
    if (((hh >= 7) && (hh < 10)) || ((hh >= 16) && (hh < 21))) {  //peak 7-10am and 4pm to 9pm
      if ((hh >= 7) && (hh < 10)) {
        if (itis_dst) {
          tariff_ends = "until 11am";
        } else {
          tariff_ends = "until 10am";
        }
      }
      if ((hh >= 16) && (hh < 21)) {
        if (itis_dst) {
          tariff_ends = "until 10pm";
        } else {
          tariff_ends = " until 9pm";
        }
      }
      f_returntariff = 2;
    }
    // it is off-peak time
    else {
      if ((hh < 7) || (hh >= 21)) {
        if (itis_dst) {
          tariff_ends = " until 8am";
        } else {
          tariff_ends = " until 7am";
        }
      }
      if ((hh >= 10) && (hh < 16)) {
        if (itis_dst) {
          tariff_ends = " until 5pm";
        } else {
          tariff_ends = " until 4pm";
        }
      }
      f_returntariff = 0;
    }
  }
  return f_returntariff;
}

// function to accept hours, minutes and return an six character String with trailing colon but not seconds
String get_display(byte f_hours, byte f_minutes) {
  return pad(f_hours) + ":" + pad(f_minutes) + ":";
}

// function to return ("dd Mmm 'yy" = 10 characters) as String
// note single quote mark instead of "20" because only room for 10 characters
String get_date() {
  return pad(dd) + " " + mnth_short[mnth - 1] + " '" + pad(yy);
}


// function to pad a two digit integer with a leading zero and return a two character String
String pad(byte f_number) {
  String f_returnpad = "";
  if (f_number < 10) {
    f_returnpad = "0" + String(f_number);
  } else {
    f_returnpad = String(f_number);
  }
  return f_returnpad;
}

// function to check if (Tasmanian) Daylight Saving is in operation
bool isit_dst() {
  // Tasmania reference: https://www.dpac.tas.gov.au/divisions/policy/daylightsaving
  if ((mnth > 10) || (mnth < 4)) {
    return true;  // definintely is DST
  } else if ((mnth > 4) && (mnth < 10)) {
    return false;  // definintely is not DST

  } else {  // month is April or October
    //// FIRST SUNDAY ////
    if ((dow == 0) && (dd <= 7)) {
      if (hh < 2) {
        if (mnth == 4) {
          return true;
        }  // the first sunday in April but before 2am so still DST
        else {
          return false;
        }       // the first sunday in October but before 2am so not yet DST
      } else {  // 2 pm or later on first Sunday
        if (mnth == 4) {
          return false;  // the first sunday in April and after 2am so not DST
        } else {
          return true;  // the first sunday in October and after 2am so DST
        }
      }
    } else  // NOT THE FIRST SUNDAY ////
      /// before the fist sunday ///
      if ((dd - dow) <= 0) {  // Before first Sunday because date minus day of week (0=Sun) is <= 0
        if (mnth == 4) {
          return true;  // before the first sunday in April so still DST
        } else {
          return false;  // before the first sunday in October so not yet DST
        }

      } else {
        /// after the first Sunday
        if (mnth == 4) {
          return false;  // after the first sunday in April so no longer DST
        } else {
          return true;  // after the first sunday in October so now DST
        }
      }
  }
}

// function to check and set relay
void relay_check() {
  itis_solar = ((isit_solar(solar_on1hh, solar_on1mm, solar_off1hh, solar_off1mm)) || (isit_solar(solar_on2hh, solar_on2mm, solar_off2hh, solar_off2mm)));
  if (use_solar) {
    if ((itis_solar) || (boost_togo > 0)) {
      relay_do(true);
    } else {
      relay_do(false);
    }
  } else {
    if ((tariff == 0) || (boost_togo > 0)) {
      relay_do(true);
    } else {
      relay_do(false);
    }
  }
}

// function to set relay, LED is on the same pin
void relay_do(bool relay_status) {
  if (relay_status == true) {
    digitalWrite(RELAY0_PIN, HIGH);
  } else {
    digitalWrite(RELAY0_PIN, LOW);
  }
}

// function to check if in current solar boost period
bool isit_solar(byte f_start_hh, byte f_start_mm, byte f_end_hh, byte f_end_mm) {
  bool f_after_start = ((hh > f_start_hh) || ((hh == f_start_hh) && (mm >= f_start_mm)));
  bool f_before_end = ((hh < f_end_hh) || ((hh == f_end_hh) && (mm < f_end_mm)));
  bool f_return_solar = (f_after_start && f_before_end);
  return f_return_solar;
}

// function to display a two digit value in the RTC or boost menu
void show_value(byte f_xpos, byte f_ypos, byte f_value, byte f_pos) {

  if (f_pos == set_pos) {  //this is the highlighted value
    TFTscreen.fillRect(f_xpos, f_ypos - 1, 2 * SIZE2X, SIZE2Y, YELLOW);
  } else {
    TFTscreen.fillRect(f_xpos, f_ypos - 1, 2 * SIZE2X, SIZE2Y, WHITE);
  }
  TFTscreen.setTextColor(BLACK);
  TFTscreen.setCursor(f_xpos, f_ypos);
  TFTscreen.print(pad(f_value));
  return;
}

// MENU FUNCTIONS
/////////////////

// setup and update the RTC menu
void menurtc_do() {
  set_pos = SET_MM;  // start on minutes when adjusting RTC values
  menurtc_setup();
  nav = get_nav();
  menurtc_update();
  // does nothing if nav = 0
  while (nav != MENU) {
    nav = get_nav();
    if ((nav == MORE) || (nav == LESS)) {
      change_value();
    }
    if (nav == PREV) {
      set_pos = set_pos - 1;
      if (set_pos < 1) { set_pos = 6; }
    }
    if (nav == NEXT) {
      set_pos = set_pos + 1;
      if (set_pos > 6) { set_pos = 1; }
    }

    if (nav != ZERO) {

      menurtc_update();
    }

  }  // end while (nav != MENU)
  menumode_do();
  return;
}

// set up the menu for adjusting RTC date and time
void menurtc_setup() {
  TFTscreen.background(BLACK);
  // Display messages
  TFTscreen.fillRect(X0, Y0, TFTWIDTH, (2 * SIZE1Y), WHITE);
  TFTscreen.setTextSize(1);
  TFTscreen.setTextColor(BLACK);
  TFTscreen.setCursor(X1, Y0);
  TFTscreen.print("set in standard time");
  TFTscreen.setTextColor(RED);
  TFTscreen.setCursor(X1, Y0 + SIZE1Y);
  TFTscreen.print(" NOT daylight time");

  // TFTscreen.fillRect(X0, Y4 - 3, TFTWIDTH, ((2 * SIZE2Y) + 2), BLUE);

  TFTscreen.setTextSize(2);
  TFTscreen.setTextColor(WHITE);

  for (int time_pos = 0; time_pos < 6; time_pos++) {
    TFTscreen.setCursor(X1, Y2 + (time_pos * SIZE2Y));
    TFTscreen.print(time_label[time_pos]);
  }
  return;
}

void menurtc_update() {
  // Display current values
  show_value(X1 + (8 * SIZE2X), Y2 + (0 * SIZE2Y), yy, 1);
  show_value(X1 + (8 * SIZE2X), Y2 + (1 * SIZE2Y), mnth, 2);
  show_value(X1 + (8 * SIZE2X), Y2 + (2 * SIZE2Y), dd, 3);
  show_value(X1 + (8 * SIZE2X), Y2 + (3 * SIZE2Y), hh, 4);
  show_value(X1 + (8 * SIZE2X), Y2 + (4 * SIZE2Y), mm, 5);
  show_value(X1 + (8 * SIZE2X), Y2 + (5 * SIZE2Y), ss, 6);
  dow_show();
  return;
}

// menu for setting mode
void menumode_do() {
  nav = get_nav();
  while (nav != MENU) {
    TFTscreen.setCursor(X0, Y0);
    TFTscreen.background(BLACK);
    TFTscreen.setTextSize(2);
    TFTscreen.setTextColor(WHITE);
    TFTscreen.println("Set mode");
    TFTscreen.setTextSize(1);
    TFTscreen.println("0 = use off-peak ");
    TFTscreen.println("1 = use boost times");
    TFTscreen.setTextSize(2);
    set_pos = 7;
    show_value(X1, Y3, use_solar, 7);
    TFTscreen.setCursor(X0, Y5);
    TFTscreen.setTextSize(1);
    TFTscreen.setTextColor(WHITE);
    if (use_solar == 0) {
      TFTscreen.println("press Menu to exit");
    } else {
      TFTscreen.println("press Menu");
      TFTscreen.println("to set boost times");
    }
    nav = get_nav();
    // stays in following loop until nav=MORE or nav=LESS or nav=MENU
    // does nothing if ((nav == PREV) || (nav == NEXT)) because only one option to set

    while ((nav != MORE) && (nav != LESS) && (nav != MENU)) {
      nav = get_nav();
      change_value();
      use_solar = value;
    }
  }
  EEPROM.update(7, use_solar);
  if (use_solar) {
    menuboost_do();
  }
  return;
}

// setup and call menu for adjusting boost times
void menuboost_do() {
  menuboost_setup();
  menuboost_update();

  nav = get_nav();

  // does nothing if nav = 0
  while (nav != MENU) {
    nav = get_nav();
    if ((nav == MORE) || (nav == LESS)) {
      change_value();
    }
    if (nav == PREV) {
      set_pos = set_pos - 1;
      if (set_pos < 8) { set_pos = 15; }
    }
    if (nav == NEXT) {
      set_pos = set_pos + 1;
      if (set_pos > 15) { set_pos = 8; }
    }

    if (nav != ZERO) {
      menuboost_update();
    }
  }
  save_eeproms();
  return;
}

// menuboost_setup
void menuboost_setup() {
  TFTscreen.background(BLACK);
  // Display messages
  TFTscreen.fillRect(X0, Y0, TFTWIDTH, (2 * SIZE1Y), WHITE);
  TFTscreen.setTextSize(1);
  TFTscreen.setTextColor(BLACK);
  TFTscreen.setCursor(X0, Y0);
  TFTscreen.println("set boost mode+time");
  TFTscreen.setTextColor(RED);
  TFTscreen.println("off must be after on");

  TFTscreen.setTextSize(2);
  TFTscreen.setTextColor(WHITE);
  set_pos = 8;
  return;
}

//setup and update boost start and finish times
void menuboost_update() {
  // Display current values
  byte f_xhh = X0 + (5 * SIZE2X);  // column for hour
  byte f_xco = X0 + (7 * SIZE2X);  // column for colon
  byte f_xmm = X0 + (8 * SIZE2X);  // column for minute

  byte f_yrow1 = Y2;
  byte f_yrow2 = Y2 + SIZE2Y;
  byte f_yrow3 = Y2 + (2 * SIZE2Y);
  byte f_yrow4 = Y2 + (3 * SIZE2Y);
  byte f_yrow5 = Y2 + (4 * SIZE2Y);
  byte f_yrow6 = Y2 + (5 * SIZE2Y);


  TFTscreen.setTextColor(WHITE);
  tft_text(X0, f_yrow1, "BOOST 1");
  tft_text(X0, f_yrow3, "  to:");
  tft_text(f_xco, f_yrow2, ":");
  tft_text(f_xco, f_yrow3, ":");

  tft_text(X0, f_yrow4, "BOOST 2");
  tft_text(X0, f_yrow6, "  to:");
  tft_text(f_xco, f_yrow5, ":");
  tft_text(f_xco, f_yrow6, ":");

  show_value(f_xhh, f_yrow2, solar_on1hh, 8);
  show_value(f_xmm, f_yrow2, solar_on1mm, 9);
  show_value(f_xhh, f_yrow3, solar_off1hh, 10);
  show_value(f_xmm, f_yrow3, solar_off1mm, 11);

  show_value(f_xhh, f_yrow5, solar_on2hh, 12);
  show_value(f_xmm, f_yrow5, solar_on2mm, 13);
  show_value(f_xhh, f_yrow6, solar_off2hh, 14);
  show_value(f_xmm, f_yrow6, solar_off2mm, 15);

  return;
}

// FUNCTIONS USED IN MENU FUNCTIONS
///////////////////////////////////

// change value based on min and max and navigation buttons
void change_value() {
  set_min_max();
  get_value();
  if (nav == LESS) {
    // value = value - 1 ; //THIS DOESNT WORK WITH BYTE IF MIN_VALUE == 0
    if (value == min_value) {
      value = max_value;
    } else {
      value = value - 1;
    }
  }
  if (nav == MORE) {
    if (value == max_value) {
      value = min_value;
    } else {
      value = value + 1;
    }
  }
  if (set_pos < 7) {
    setrtc_value();
  }
  // case where set_pos = 7 (use_solar) dealt with elsewhere
  if (set_pos > 7) {
    setboost_value();
  }
  return;
}

// get existing value to be adjusted in menu
void get_value() {
  switch (set_pos) {
    case 1:
      value = myRTC.getYear();
      break;
    case 2:
      value = myRTC.getMonth(centuryBit);
      break;
    case 3:
      value = myRTC.getDate();
      break;
    case 4:
      value = myRTC.getHour(h12, hPM);
      break;
    case 5:
      value = myRTC.getMinute();
      break;
    case 6:
      value = myRTC.getSecond();
      break;
    case 7:
      value = use_solar;
      break;
    case 8:
      value = solar_on1hh;
      break;
    case 9:
      value = solar_on1mm;
      break;
    case 10:
      value = solar_off1hh;
      break;
    case 11:
      value = solar_off1mm;
      break;
    case 12:
      value = solar_on2hh;
      break;
    case 13:
      value = solar_on2mm;
      break;
    case 14:
      value = solar_off2hh;
      break;
    case 15:
      value = solar_off2mm;
      break;
    default:
      // dont have any error trapping for now
      break;
  }
  return;
}

// update Boost values in memory while adjusting. Saved to EEPROM on exit from menuboost_do()
void setboost_value() {
  if (set_pos == 8) {
    solar_on1hh = value;
  }
  if (set_pos == 9) {
    solar_on1mm = value;
  }
  if (set_pos == 10) {
    solar_off1hh = value;
  }
  if (set_pos == 11) {
    solar_off1mm = value;
  }
  if (set_pos == 12) {
    solar_on2hh = value;
  }
  if (set_pos == 13) {
    solar_on2mm = value;
  }
  if (set_pos == 14) {
    solar_off2hh = value;
  }
  if (set_pos == 15) {
    solar_off2mm = value;
  }
  return;
}

// set RTC value
void setrtc_value() {
  if (set_pos == SET_YY) {
    myRTC.setYear(value);
  }
  if (set_pos == SET_MNTH) {
    myRTC.setMonth(value);
  }
  if (set_pos == SET_DD) {
    myRTC.setDate(value);
  }
  if (set_pos == SET_HH) {
    myRTC.setHour(value);
  }
  if (set_pos == SET_MM) {
    myRTC.setMinute(value);
  }
  if (set_pos == SET_SS) {
    myRTC.setSecond(value);
  }
  update_times();  // reset all variables from RTC
  return;
}

// function to get min and max values
void set_min_max() {
  if (set_pos == SET_YY) {  // (two digits only)
    min_value = 20;
    max_value = 99;
  }
  if (set_pos == SET_MNTH) {
    min_value = 1;
    max_value = 12;
  }
  if (set_pos == SET_DD) {
    min_value = 1;
    max_value = max_days[mnth - 1];
  }  // minus 1 because array is zero indexed, ie Jan = 0, Dec = 11
  if (set_pos == SET_HH) {
    min_value = 0;
    max_value = 23;
  }
  if (set_pos == SET_MM) {
    min_value = 0;
    max_value = 59;
  }
  if (set_pos == SET_SS) {
    min_value = 0;
    max_value = 59;
  }

  if (set_pos == 7) {
    min_value = 0;
    max_value = 1;
  }

  if ((set_pos == 8) || (set_pos == 10) || (set_pos == 12) || (set_pos == 14)) {
    min_value = 0;
    max_value = 23;
  }

  if ((set_pos == 9) || (set_pos == 11) || (set_pos == 13) || (set_pos == 15)) {
    min_value = 0;
    max_value = 59;
  }

  return;
}

// reset _before values so main screen updated first time around main loop and when coming out of RTC menu
void reset_befores() {
  mm_before = 60;
  ss_before = 60;
  tariff_before = 4;
  return;
}

// function to get most recent button press
byte get_nav() {
  byte f_lastpin = 0;  // zero if no buttons pressed
  // invert because press goes low
  if (!digitalRead(MENU_PIN)) {
    while (!digitalRead(MENU_PIN)) {}  // do nothing so wait until button released
    f_lastpin = 1;
  }
  if (!digitalRead(PREV_PIN)) {
    while (!digitalRead(PREV_PIN)) {}
    f_lastpin = 2;
  }
  if (!digitalRead(NEXT_PIN)) {
    while (!digitalRead(NEXT_PIN)) {}
    f_lastpin = 3;
  }
  if (!digitalRead(LESS_PIN)) {
    while (!digitalRead(LESS_PIN)) {}
    f_lastpin = 4;
  }
  if (!digitalRead(MORE_PIN)) {
    while (!digitalRead(MORE_PIN)) {}
    f_lastpin = 5;
  }
  return f_lastpin;
}

// OTHER FUNCTIONS
//////////////////

// tft_text() function to display text at a defined position.
// Assumes text size, colour and background already set [NEW]
void tft_text(byte f_xpos, byte f_ypos, String f_text) {
  TFTscreen.setCursor(f_xpos, f_ypos);
  TFTscreen.print(f_text);
  return;
}

void get_eeproms() {

  // test and update use_solar
  ee_value = EEPROM.read(7);
  if (ee_value != 0 && ee_value != 1) {
    EEPROM.write(7, 1);  // defaults to 1 (true) if no existing valid value
  } else {
    if (ee_value == 0) {
      use_solar = false;
    } else {
      use_solar = true;
    }
  }

  // test and update solar1 start hour
  ee_value = EEPROM.read(8);
  if (ee_value < 1 || ee_value > 23) {
    EEPROM.write(8, solar_on1hh);
  } else {
    solar_on1hh = ee_value;
  }
  // test and update solar1 start minute
  ee_value = EEPROM.read(9);
  if (ee_value > 59) {
    EEPROM.write(9, solar_on1mm);
  } else {
    solar_on1mm = ee_value;
  }
  // test and update solar1 end hour
  ee_value = EEPROM.read(10);
  if (ee_value < 1 || ee_value > 23) {
    EEPROM.write(10, solar_off1hh);
  } else {
    solar_off1hh = ee_value;
  }
  // test and update solar1 end minute
  ee_value = EEPROM.read(11);
  if (ee_value > 59) {
    EEPROM.write(11, solar_off1mm);
  } else {
    solar_off1mm = ee_value;
  }

  // test and update solar2 start hour
  ee_value = EEPROM.read(12);
  if (ee_value < 1 || ee_value > 23) {
    EEPROM.write(12, solar_on2hh);
  } else {
    solar_on2hh = ee_value;
  }
  // test and update solar2 start minute
  ee_value = EEPROM.read(13);
  if (ee_value > 59) {
    EEPROM.write(13, solar_on2mm);
  } else {
    solar_on2mm = ee_value;
  }
  // test and update solar2 end hour
  ee_value = EEPROM.read(14);
  if (ee_value < 1 || ee_value > 23) {
    EEPROM.write(14, solar_off2hh);
  } else {
    solar_off2hh = ee_value;
  }
  // test and update solar2 end minute
  ee_value = EEPROM.read(15);
  if (ee_value > 59) {
    EEPROM.write(15, solar_off2mm);
  } else {
    solar_off2mm = ee_value;
  }
}

void save_eeproms() {
  EEPROM.update(8, solar_on1hh);
  EEPROM.update(9, solar_on1mm);
  EEPROM.update(10, solar_off1hh);
  EEPROM.update(11, solar_off1mm);
  EEPROM.update(12, solar_on2hh);
  EEPROM.update(13, solar_on2mm);
  EEPROM.update(14, solar_off2hh);
  EEPROM.update(15, solar_off2mm);
  return;
}

void show_startup() {
  TFTscreen.setTextSize(2);
  TFTscreen.setTextColor(WHITE);
  TFTscreen.setCursor(X1, Y0);
  TFTscreen.println("HotTimer");
  TFTscreen.setTextSize(1);
  TFTscreen.println("(c) 2025");
  TFTscreen.println("Backroad Connections");
  TFTscreen.println("see github.com/");
  TFTscreen.println("JackGilding/HotTimer");
  TFTscreen.println();

  if (use_solar == 1) {
    TFTscreen.println("use boost times");
  } else {
    TFTscreen.println("use off peak times");
  }
  TFTscreen.println();

  TFTscreen.println("BOOST 1");
  TFTscreen.setTextSize(2);
  TFTscreen.println("   " + pad(solar_on1hh) + ":" + pad(solar_on1mm));
  TFTscreen.println("to:" + pad(solar_off1hh) + ":" + pad(solar_off1mm));


  TFTscreen.setTextSize(1);
  TFTscreen.println();
  TFTscreen.println("BOOST 2");
  TFTscreen.setTextSize(2);
  TFTscreen.println("   " + pad(solar_on2hh) + ":" + pad(solar_on2mm));
  TFTscreen.println("to:" + pad(solar_off2hh) + ":" + pad(solar_off2mm));
  TFTscreen.println();
  delay(5000);
  TFTscreen.background(BLACK);
}


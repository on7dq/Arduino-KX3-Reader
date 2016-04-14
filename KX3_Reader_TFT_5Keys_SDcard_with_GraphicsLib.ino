// Started   20160212
// Last edit 20160310

// KX3 decode buffer readout and macro buttons
// by Luc Decroos, ON7DQ-KF0CR
// with help from Tony Collen - N0RUA
//
// Using 1.8" TFT display,
// infro and sample code from https://www.youtube.com/watch?v=boagCpb6DgY
//
// Using 5 buttons on pin A0 (like Keypad shield, info and sample code from :
// http://www.dfrobot.com/wiki/index.php?title=Arduino_LCD_KeyPad_Shield_(SKU:_DFR0009))
// thanks Mark Bramwell
//
// Using RTC module DS3232 set in UTC for clock and logging
//

#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library
#include <SPI.h>
#include <Wire.h>
#include <SoftwareSerial.h>  // for comms to KX3
#include <Time.h>            // for time conversion functions
#include "DS3232RTC.h"       // RTC + temperature sensor
#include <SD.h>              // for SD card logging


#define BAUD_RATE 9600       // KX3 serial speed
#define LOOP_DELAY 100       // determines rate of polling the KX3

// pin definitions for the TFT on Arduino Uno
#define TFT_CS     10
#define SD_CS      4
#define TFT_RST    8  // you can also connect this to the Arduino reset
                      // in which case, set this #define pin to 0!
#define TFT_DC     9

// define button names
#define btn0      0
#define btn1      1
#define btn2      2
#define btn3      3
#define btn4      4
#define btnNONE   5

// for RTC module
#define BUFF_MAX 128
uint8_t time[8];
time_t myTime;
char recv[BUFF_MAX];
unsigned int recv_size = 0;
unsigned long prev, interval = 60000;

File file; // for SD card logging

// primitive variables
int spkPin    = 12; // speaker 8 ohm + R 100 Ohm to ground (not used for now)
long r = 0, g = 255, b = 0;        // colors, start with GREEN
int key     = 0;
int adc_key_in  = 0;
int menu = 0;
int maxMenu = 5;
int x;
int y;
int counter;
boolean mode = true;

char ch;
int che = 9;  // char height = 8 + 1 for spacing between lines
int cw = 6;   // char width = 5 + 1 for spacing between chars

// Strings and arrays
String str, old;

// Object variables
// create an instance of the GFX TFT library
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS,  TFT_DC, TFT_RST);

// serial connection to the KX3 :
// RX = KX3 to PC  : via voltage divider (3k9 in series/10k to ground) to pin 10
// TX = PC to KX3  : direct to pin 7
SoftwareSerial mySerial(6, 7, true); // (RX, TX, invert) --> invert the bits because no MAX232 is used

void setup() {
  Serial.begin (BAUD_RATE);    // init Serial monitor (in IDE on PC)
  Serial.println ();
  Serial.println ("KX3 Terminal project");

  initializeSD();
  //myTime = RTC.get();
  createFile("logbook.txt");
  writeToFile("This is sample text!");
  closeFile();

  
  // Put this line at the beginning of every sketch that uses the GLCD:
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.setTextWrap(true);
  Serial.println("Display Initialized");

  // clear the screen with a black background
  tft.fillScreen(ST7735_BLACK);

  // display intro text
  tft.setTextSize(2);
  tft.setTextColor(ST7735_YELLOW);
  tft.println("");
  tft.println("KX3 Decoder");
  tft.println("");
  tft.setTextColor(ST7735_WHITE);
  tft.println("ON7DQ/KF0CR");

  delay(1000);


  // connect to KX3
  mySerial.begin(BAUD_RATE);
  mySerial.println("AI0;"); // disable auto info on the KX3
  // optional : do several settings in KX3
  //  mySerial.println("FA00014070000;"); // set VFO A to some frequency
  //  mySerial.println("MD6;"); // first set DATA mode ...
  //  mySerial.println("DT3;"); // then set submode for PSK-D
  //  mySerial.println("KY VVV DE ON7DQ;"); // send a test msg

  // wait and clear screen
  delay (3000);
  tft.fillScreen(ST7735_BLACK);
  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(0, 0);
  tft.setTextSize(1);
  x = 0; y = 0;
}

// main loop
void loop() {

  // check keys
  key = read_LCD_buttons();

  switch (key)   // depending on which button was pushed, we perform an action
  {
    case btn0:
      {
        tft.print(">>Screen:S/M/L? ");
        x = 0; y += che;
        mode = false; // stops receiving text
        menu++; if (menu>maxMenu) menu = 0;
        break;
      }
    case btn1:
      {
        tft.print(" CLEAR ");
        delay(150);
        restart();
        x = 0; y = 0;
        break;
      }
    case btn2:
      {
        
        x = 0; y += che;
        mode=false;
        myTime = RTC.get(); // epoch time in seconds since 1 jan 1070
        tft.fillScreen(ST7735_BLACK);
        tft.setTextSize(2);
        tft.print(" UTC TIME ");
        tft.print(hour(myTime));
        tft.print(":");
        tft.println(minute(myTime));

        tft.println(RTC.temperature()/4);
        
        break;
      }
    case btn3:
      {
        tft.print(" Menu 3 ");
        x = 0; y += che;
        break;
      }
    case btn4:
      {
        tft.print(" Menu 4 ");
        x = 0; y += che; mode = true;
        break;
      }
    case btnNONE:
      {
        // do nothing (for now)
        break;
      }
  }

  // read KX3 receive buffer
  if (mode) {
    mySerial.print("TB;");
    /* get return from TB command, format is TBtrss
          t = number of buffered CW chars to be sent
          rr = number of RX charactes (00-40)
          s = the corresponding string (variable length)
    */
    while (mySerial.available() > 0) {
      ch = mySerial.read();

      if (ch != ';') {
        str += ch;
      } else {
        displayDecodeBuffer(str);
        Serial.print(".");
        str = "";
      }
    }
  }
  delay(LOOP_DELAY);

} // end of loop()


//************************ functions ********************************************



// read the buttons
int read_LCD_buttons()
{
  adc_key_in = 0;
  for (int i = 0; i < 3; i++) {
    adc_key_in += analogRead(0);      // read the value from the sensor
    delay(2);
  }
  adc_key_in /= 3; // average from 3 reads
  
  // for checking actual key values :
  //Serial.print ("Key value : ");
  //Serial.println (adc_key_in); 
  // my buttons when read are centered at these valies: 0, 144, 329, 504, 741
  // we add approx 50 to those values and check to see if we are close
  if (adc_key_in > 1000) return btnNONE; // We make this the 1st option for speed reasons since it will be the most likely result
  if (adc_key_in < 64)   return btn4; // reversed the order after boxing my project ;-)
  if (adc_key_in < 220)  return btn3;
  if (adc_key_in < 395)  return btn2;
  if (adc_key_in < 600)  return btn1;
  if (adc_key_in < 870)  return btn0;
  return btnNONE;  // when all others fail, return this...
}



void displayDecodeBuffer(String msg) {  // this version for a 1.8" TFT Display
  String fullMessage = "";
  int messageLength = 0;

  // TB000s;
  messageLength = msg.substring(3, 5).toInt();
  if (messageLength > 0) {
    fullMessage = msg.substring(5, 5 + messageLength);
    tft.print(fullMessage);
    counter += messageLength;
    if (counter >= 400) {

      restart();
    }

  }
}


void initializeSD()
{
  Serial.println("Initializing SD card...");
  pinMode(SD_CS, OUTPUT);

  if (SD.begin())
  {
    Serial.println("SD card is ready to use.");
  } else
  {
    Serial.println("SD card initialization failed");
    return;
  }
}

int createFile(char filename[])
{
  file = SD.open(filename, FILE_WRITE);

  if (file)
  {
    Serial.println("File created successfully.");
    return 1;
  } else
  {
    Serial.println("Error while creating file.");
    return 0;
  }
}

int writeToFile(char text[])
{
  if (file)
  {
    file.println(text);
    Serial.println("Writing to file: ");
    Serial.println(text);
    return 1;
  } else
  {
    Serial.println("Couldn't write to file");
    return 0;
  }
}

void closeFile()
{
  if (file)
  {
    file.close();
    Serial.println("File closed");
  }
}

int openFile(char filename[])
{
  file = SD.open(filename);
  if (file)
  {
    Serial.println("File opened with success!");
    return 1;
  } else
  {
    Serial.println("Error opening file...");
    return 0;
  }
}

String readLine()
{
  String received = "";
  char ch;
  while (file.available())
  {
    ch = file.read();
    if (ch == '\n')
    {
      return String(received);
    }
    else
    {
      received += ch;
    }
  }
  return "";
}


void restart() {
  counter = 0;
  tft.fillScreen(ST7735_BLACK);
  tft.setCursor(0, 0);
}

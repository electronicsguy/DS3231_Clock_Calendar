// Modified by Sujay Phadke, 2025
// Setup: Waveshare ESP32S3 connected to
// SH1106 128x64 OLED display &
// ZS-042 DS3231 RTC Module
// (both over I2C)

// Original codes from:
// https://www.hackster.io/lucasio99/clock-with-sh1106-oled-display-ds1302-rtc-module-518a4c
//
// https://github.com/kriswiner/DS3231RTC/blob/master/DS3231RTCBasicExample.ino

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// Have suffcient pullup resistors on the I2C pins
// Both OLED display and RTC module share the
// same I2C pins
#define I2C_A_SDA 17
#define I2C_A_SCL 16

// I2C operation frequency
unsigned long freq1 = 400 * 1000;
unsigned long freq2 = 400 * 1000;

#define OLED_Address 0x3c
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET, freq1, freq2);

// RTC register addresses as per the data sheet
// https://www.analog.com/media/en/technical-documentation/data-sheets/ds3231.pdf
//
#define REG_SECONDS 0x00
#define REG_MINUTES 0x01
#define REG_HOUR 0x02
#define REG_DAY 0x03
#define REG_DATE 0x04
#define REG_MONTH 0x05
#define REG_YEAR 0x06

#define REG_CONTROL 0x0E
#define REG_STATUS 0x0F
#define REG_AGING_OFFSET 0x10
#define REG_MSB_TEMP 0x11
#define REG_LSB_TEMP 0x12

// Define arrays for converting numbers into text
uint16_t century_array[] = { 1900, 2000 };
const char* AM_PM_array[] = { "AM", "PM" };
const char* short_days_array[] = { "None", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
const char* days_array[] = { "None", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" };

const char* short_months_array[] = { "None", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
const char* months_array[] = { "None", "January", "February", "March", "April", "May", "June",
                               "July", "August", "September", "October", "November", "December" };

#define AT24C32_ADDRESS 0x57  // On-board EEPROM
#define DS3231_ADDRESS 0x68   // RTC Address

// Variables to store calendar values
uint8_t seconds, minutes, hours, date, day, month, year, century;
bool PM;
uint8_t AM_PM_or_24HR = 1;
float temperature;
byte age_off_val;

// Control display update refresh time
uint32_t delta_t = 0;
uint32_t curr_t = 0;

void drawUI() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(2, 2);
  display.drawLine(0, 11, 128, 11, SH110X_WHITE);
  display.drawLine(0, 44, 94, 44, SH110X_WHITE);
}

// Values in the DS3231 registers are stored
// using the BCD format for each nibble
// (10's values | 1's values)
uint8_t readSeconds() {
  uint8_t data;
  data = readByte(DS3231_ADDRESS, REG_SECONDS);
  return ((data >> 4) * 10) + (data & 0x0F);
}

uint8_t readMinutes() {
  uint8_t data;
  data = readByte(DS3231_ADDRESS, REG_MINUTES);
  return ((data >> 4) * 10) + (data & 0x0F);
}

uint8_t readHours() {
  uint8_t data;
  data = readByte(DS3231_ADDRESS, REG_HOUR);
  AM_PM_or_24HR = (data & 0x40) >> 6;
  return (((data & 0x10) >> 4) * 10) + (data & 0x0F);
}

uint8_t readHours24() {
  uint8_t data;
  data = readByte(DS3231_ADDRESS, REG_HOUR);
  AM_PM_or_24HR = (data & 0x40) >> 6;
  return (((data & 0x20) >> 5) * 20) + (((data & 0x10) >> 4) * 10) +(data & 0x0F);
}

boolean readPM() {
  uint8_t data;
  data = readByte(DS3231_ADDRESS, REG_HOUR);
  return (data & 0x20);
}

uint8_t readDay() {
  uint8_t data;
  data = readByte(DS3231_ADDRESS, REG_DAY);
  return data;
}

uint8_t readDate() {
  uint8_t data;
  data = readByte(DS3231_ADDRESS, REG_DATE);
  return ((data >> 4) * 10) + (data & 0x0F);
}

uint8_t readMonth() {
  uint8_t data;
  data = readByte(DS3231_ADDRESS, REG_MONTH);
  return (((data & 0x10) >> 4) * 10) + (data & 0x0F);
}

// century bit:
// 0: 1900 century
// 1: 2000 century
// It will toggle when the years register overflows from 99 to 00
uint8_t readCentury() {
  uint8_t data;
  data = readByte(DS3231_ADDRESS, REG_MONTH);
  return data >> 7;
}

uint8_t readYear() {
  uint8_t data;
  data = readByte(DS3231_ADDRESS, REG_YEAR);
  return ((data >> 4) * 10) + (data & 0x0F);
}

// Temperature is represented as a 10-bit code with a
// resolution of 0.25°C
// The temperature is encoded in 2’s complement format
// The upper 8 bits are the integer portion
// The lower 2 bits are the fractional portion
float readTempData() {
  uint8_t rawData[2];
  readBytes(DS3231_ADDRESS, REG_MSB_TEMP, 2, &rawData[0]);
  return ((float)((int8_t)rawData[0])) + ((float)((int8_t)rawData[1] >> 6) * 0.25);
}

// The aging offset is encoded in 2’s complement, 
// with bit 7 representing the sign bit
byte readAgingOffset() {
  uint8_t rawData;
  rawData = readByte(DS3231_ADDRESS, REG_AGING_OFFSET);
  return ((byte)rawData);
}

void writeByte(uint8_t address, uint8_t subAddress, uint8_t data) {
  Wire.beginTransmission(address);  // Initialize the Tx buffer
  Wire.write(subAddress);           // Put slave register address in Tx buffer
  Wire.write(data);                 // Put data in Tx buffer
  Wire.endTransmission();           // Send the Tx buffer
}

uint8_t readByte(uint8_t address, uint8_t subAddress) {
  uint8_t data;                           // `data` will store the register data
  Wire.beginTransmission(address);        // Initialize the Tx buffer
  Wire.write(subAddress);                 // Put slave register address in Tx buffer
  Wire.endTransmission(false);            // Send the Tx buffer, but send a restart to keep connection alive
  Wire.requestFrom(address, (uint8_t)1);  // Read one byte from slave register address
  data = Wire.read();                     // Fill Rx buffer with result
  return data;                            // Return data read from slave register
}

void readBytes(uint8_t address, uint8_t subAddress, uint8_t count, uint8_t* dest) {
  Wire.beginTransmission(address);  // Initialize the Tx buffer
  Wire.write(subAddress);           // Put slave register address in Tx buffer
  Wire.endTransmission(false);      // Send the Tx buffer, but send a restart to keep connection alive
  uint8_t i = 0;
  Wire.requestFrom(address, count);  // Read bytes from slave register address
  while (Wire.available()) {
    dest[i++] = Wire.read();
  }  // Put read results in the Rx buffer
}

void UpdateTimeDisplay() {
  drawUI();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(3);
  display.setCursor(1, 19);
  if (hours < 10) {
    display.print("0");
  }
  display.print(hours);
  display.print(":");
  if (minutes < 10) {
    display.print("0");
  }
  display.print(minutes);
  display.setTextSize(2);
  display.print(":");
  if (seconds < 10) {
    display.print("0");
  }
  display.print(seconds);

  display.setTextSize(1);
  display.setCursor(95, 0);
  display.print(short_days_array[day]);

  display.setCursor(0, 0);
  display.setTextSize(1);
  if (date < 10) {
    display.print("0");
  }
  display.print(date);
  display.print(" ");
  // if (month < 10) {
  //   display.print("0");
  // }
  display.print(months_array[month]);
  display.print(" ");
  display.print(century_array[century] + year);

  display.setCursor(1, 46);
  display.setTextSize(1);
  display.print("T: ");
  display.print(temperature);

  display.setCursor(1, 56);
  display.setTextSize(1);
  display.print("AO: ");
  display.print(int(age_off_val));

  if (AM_PM_or_24HR) {
    display.setTextSize(2);
    display.setTextColor(SH110X_BLACK);
    // Black text on white background
    display.fillRect(98, 46, 128 - 98, 20, SH110X_WHITE);
    display.setCursor(100, 48);
    display.print(AM_PM_array[PM]);
  }
  else{
    display.setTextSize(1);
    display.setTextColor(SH110X_BLACK);
    // Black text on white background
    display.fillRect(98, 54, 128 - 98, 10, SH110X_WHITE);
    display.setCursor(100, 56);
    display.print("24Hr");
  }

  display.display();
}

void SetTime(void) {
  // Since the value byte to be written is
  // BCD encoded, HEX values work easily here
  AM_PM_or_24HR = 1;  // set 0 for 24-hour time format
  // Set seconds
  writeByte(DS3231_ADDRESS, REG_SECONDS, 0x00);
  // Set minute
  writeByte(DS3231_ADDRESS, REG_MINUTES, 0x55);
  // Set hour
  // OR with AM/PM (bit_5 = 0/1) (hex values 0x0/0x20)
  // OR with 12/24-hour display (bit_6 = 1/0) (hex values 0x40/0x00)
  // Also, set the 10s-hour digit (bit_4) and 20s-hour digit (bit_5)
  if (AM_PM_or_24HR == 1)
    writeByte(DS3231_ADDRESS, REG_HOUR, 0x02 | 0x20 | 0x40);
  else
    writeByte(DS3231_ADDRESS, REG_HOUR, 0x01 | 0x20 | 0x00);

  // Set date
  writeByte(DS3231_ADDRESS, REG_DATE, 0x01);
  // Set day
  writeByte(DS3231_ADDRESS, REG_DAY, 0x07);
  // Set month &
  // century bit (19/20th century: OR with 0x00/0x80)
  writeByte(DS3231_ADDRESS, REG_MONTH, 0x06 | 0x80);
  // Set Year
  writeByte(DS3231_ADDRESS, REG_YEAR, 0x25);
}

void setup() {
  Serial.begin(9600);

  Wire.begin(I2C_A_SDA, I2C_A_SCL);
  Wire.setClock(freq2);

  display.begin(OLED_Address, true);
  drawUI();
  display.display();

// Once the time is set, comment out the line below
// Time will be maintained by the RTC battery

//#define SET_TIME
#ifdef SET_TIME
  SetTime();
#endif
}

void PrintCalendar(void) {
  Serial.println();
  Serial.print("year: ");
  Serial.println(century_array[century] + year);
  Serial.print("month: ");
  Serial.println(month);
  Serial.print("date: ");
  Serial.println(date);
  Serial.print("day: ");
  Serial.println(days_array[day]);
  Serial.print("hours: ");
  Serial.println(hours);
  Serial.print("minutes: ");
  Serial.println(minutes);
  Serial.print("century: ");
  Serial.println(century);
  Serial.print("PM: ");
  Serial.println(AM_PM_array[PM]);
  Serial.print("Temp: ");
  Serial.println(temperature);
}

void loop() {
  // Check if the RTC module is busy
  bool is_busy = (readByte(DS3231_ADDRESS, REG_STATUS) & 0x04);

  if (!is_busy) {
    // Read Calendar
    seconds = readSeconds();
    minutes = readMinutes();

    hours = readHours();
    if (AM_PM_or_24HR == 1){
      PM = readPM();
    }
    else{
      hours = readHours24();
    }

    day = readDay();
    date = readDate();
    month = readMonth();
    year = readYear();
    century = readCentury();

    temperature = readTempData();
    age_off_val = readAgingOffset();
  }

  //PrintCalendar();

  //Update LCD every 300 ms
  delta_t = millis() - curr_t;
  if (delta_t > 300) {
    UpdateTimeDisplay();
    curr_t = millis();
  }
  //delay(999);
}

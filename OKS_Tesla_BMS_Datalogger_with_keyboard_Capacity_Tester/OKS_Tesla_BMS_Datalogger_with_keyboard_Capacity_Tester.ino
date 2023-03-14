/*Datalogger and capacity tester for Overkill Solar BMSs
 *
 *This arduino datalogger will communicate with a BMS and record the test data on the SD card
 *
 *We used an arduino UNO clone and an Adafruit SD card shield.
 *
 *In some combinations, the Arduino's RX activity light will draw the signal too low from the BMS UART.
 *For this setup, we had to remove the RX LED from the Arduino board.
 *Other boards have been known to communicate without modification. The UNO rev3 has buffered status leds and should work fine, for example.
 *The arduino Mega works well when you use Serial 1,2,or 3
 *3.3v Arduino boards like the promicro always seem to communicate properly with the BMS. An ESP32 is also a good choice.
 *
 *Test procedure:
 *
 * The arduino will enable the BMS output for ten seconds then record the current and cell voltages in the data file.
 * It will then disable the BMS MOSFET array for 1 second, wait for a stable reading of zero amps, and record another data set in the data file.
 * 
 * The test can be run with a load or charger connected to the battery.
 * 
 * The test is complete when the BMS goes into high or low voltage cutoff.
 * 
 * The SD card can then be used to transfer the data to a computer. We used Google sheets to process the data and generate charts & graphs.
 * 
 * The data collected can be used to graph the charge/discharge curves both loaded and unloaded, and calculate capacity and internal resistance of each cell.
 * 
 */

#include <bms2.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <SPI.h>
#include <EEPROM.h>
#include <hidboot.h> //note- the USB host library has been modified by st. changes SS from pin 10 to 8.
#include "RTClib.h"
#include "SdFat.h"

//RTC library
RTC_DS1307 rtc;
//to set the RTC time after battery failure, run the ds1307 example code from adafruit
DateTime now;

//------------------------------------------------------------------------------
// call back for file timestamps
void dateTime(uint16_t* date, uint16_t* time) {
 DateTime now = rtc.now();
 //sprintf(timestamp, "%02d:%02d:%02d %2d/%2d/%2d \n", now.hour(),now.minute(),now.second(),now.month(),now.day(),now.year()-2000);
 //Serial.println("yy");
 //Serial.println(timestamp);
 // return date using FAT_DATE macro to format fields
 *date = FAT_DATE(now.year(), now.month(), now.day());

 // return time using FAT_TIME macro to format fields
 *time = FAT_TIME(now.hour(), now.minute(), now.second());
}
//------------------------------------------------------------------------------

//SDfat SD card library
#define SDcardChipSelect 10
File myFile;
SdFs SD;
//const uint8_t SD_CS_PIN = 10;

//BMS library object
OverkillSolarBms2 bms;

//LCD library object
LiquidCrystal_I2C lcd(0x27,20,4);
//LiquidCrystal_I2C lcd(0x3F,20,4); //Library object for the LCD
//set the LCD address to 0x3F or 0x27 for a 20 chars and 4 line display

//*********usb host setup******************************************
/* shield pins. First parameter - SS pin, second parameter - INT pin */
//the SS pin on my USb host shield had to be changed from 10 to 8 because of a conflict with the SD card shield.
//find line 58 in usbcore.h, and edit the SS pin number:
//typedef MAX3421e<P8, P9> MAX3421E; // Official Arduinos. chip select and int pin declarations.
char keyasc;
int keycode;
boolean iskeypressed;

class KeyboardInput : public KeyboardReportParser
{
 protected:
    void OnKeyDown  (uint8_t mod, uint8_t key);
    void OnKeyPressed(uint8_t key);
};

void KeyboardInput::OnKeyDown(uint8_t mod, uint8_t key){
  uint8_t c = OemToAscii(mod, key);
  if (key == 0x2a){ //backspace key hex value
    OnKeyPressed(0x2a);
  }else if (key == 0x28){ //enter key hex value
    OnKeyPressed(0x28);
  }
  else if (c){ //normal keys, returned from the ascii conversion
    OnKeyPressed(c);
  }
}
void KeyboardInput::OnKeyPressed(uint8_t key){
keyasc = (char) key;
keycode = (int)key;
iskeypressed = true; 
}
USB     Usb;
HIDBoot<USB_HID_PROTOCOL_KEYBOARD>    HidKeyboard(&Usb);
KeyboardInput Prs;
//**********usb host setup****************************************

//global variables
long lastmillis;
long lastmillis2;
long lastmillis3;
int cyclecount;
#define eeAddress 0   //Location we want the SN data to be put.
String filename;
float AverageCellVoltage;
float AverageCellVoltageUnloaded;
int NumberofOnPeriods = 58; //used to controll the on time between resing periods. number of cycles of the 10s timer
int TestPhase; //an indicator of the test phase/mode/step


void setup() {
  //only execute this once to initialize the eeprom location. 
  //This will set the serial number to an initial value.
  //only needed to initialize the serial number on a new board. otherwise it starts at zero.
  //EEPROM.put(eeAddress, serialnumber); //comment out this line after running once

  //i2c setup
  Wire.begin(); 

  //lcd setup
  lcd.begin(20, 4); // set up the LCD's number of columns and rows
  //print a Splash Screen on the LCD
  lcd.noCursor();
  lcd.noBlink();
  lcd.backlight();
  lcd.print(F("   Overkill Solar"));
  lcd.setCursor(0, 2);
  lcd.print(F("   6s Tesla Module")); 
  lcd.setCursor(0, 3);
  lcd.print(F("Capacity Tester V1.2")); 

  //initialize the serial ports. 
  Serial1.begin(9600);
  while (!Serial1) {  // Wait for the BMS serial port to initialize
    }
  bms.begin(&Serial1); //Initialize the BMS library object

  //usb host setup
  Usb.Init();
  delay(200);
  HidKeyboard.SetReportParser(0, &Prs);

  delay(3000);// let the splash screen sit for a bit

  //initialize the SD card
  lcd.clear();
  lcd.print(F("Initializing SD card"));
  lcd.setCursor(0, 1);
  if (!SD.begin(SDcardChipSelect)) {
    lcd.print(F("initialization fail"));
    lcd.setCursor(0, 3);
    lcd.print(F("is the card in?"));
    while (1);
  }
  lcd.print(F("initialization OK."));
  
  lcd.setCursor(0, 3);

  //start the RTC
  if (! rtc.begin()) {
    lcd.println(F("Couldn't find RTC"));
    while(1);
  }
  if (! rtc.isrunning()) {
    lcd.println(F("RTC NOT running!"));
    while(1);
    // When time needs to be set on a new device, or after a power loss, the
    // following line sets the RTC to the date & time this sketch was compiled
    //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
  }else{
    // set date time callback function
    SdFile::dateTimeCallback(dateTime);
    DateTime now = rtc.now();
    lcd.print(F("unix time:"));
    lcd.print(now.unixtime());
  }


  delay(2000);
  lcd.setCursor(0, 2);

  //create a new file for this test
  //get filename from keyboard entry
  NewFile();
  
  // Check to see if the file exists:
  if (SD.exists(filename)) {
    lcd.setCursor(0, 0);
    lcd.print("file open: ");
    lcd.setCursor(0, 1);
    lcd.print(filename.substring(0, 20));
    if (filename.length() >= 20){
      lcd.setCursor(0, 2);
      lcd.print(filename.substring(20));
    }
  } else {
    lcd.setCursor(0, 0);
    lcd.print("filename NFG");
    lcd.setCursor(0, 1);
    lcd.print("file not open");
    while(1);
  }

  delay(2000);
  lcd.setCursor(0, 3);

  
    //now wait while running the BMS library to establish communication
  lastmillis = millis();
  while ((millis() - lastmillis) < 4000){ // timer
    if (bms.get_comm_error_state()){
      bms.main_task(true); //call the BMS library every loop.
    }else{break;}
  }//end timer
  
  //control FETS (charge, discharge)
  Fets_off();

      lastmillis = millis();
  while ((millis() - lastmillis) < 2500){ // timer
      bms.main_task(true); //call the BMS library every loop.
  }//end timer

  lcd.clear();
  //now display the model and cell count
  //if any
  lcd.setCursor(0, 0);
  lcd.print("Found "); 
  lcd.print(bms.get_num_cells()); 
  lcd.print(" cell BMS");
  lcd.setCursor(0, 2);
  String bmsname = bms.get_bms_name();
  bmsname.remove(19, 5);
  lcd.print(bmsname);
  //Serial.println(bmsname);
  //print BMS model number
  delay(3000); //delay is ok here

  GetAverageVolts();
  AverageCellVoltageUnloaded = AverageCellVoltage; //save the unloaded voltage reading

  //ready to start testing
  Fets_on();
  TestPhase = 1;


}//end setup



void loop() {
  now = rtc.now();
  bms.main_task(true); //call the BMS library every loop.

  //1 second timer, non-blocking
  //update the lcd every second
  if ((millis() - lastmillis) > 998){ 
    lastmillis = millis();
    GetAverageVolts();
    _display();
  }//end 1 second timer

  //10s timer, non-blocking
  //save a loaded reading
  if ((millis() - lastmillis2) > 9976){ 
    cyclecount++;
    Save_a_reading();
    lastmillis2 = millis();
  }//end 10s timer


  switch (TestPhase) {
    case 1://charge up to 100%, 10 minute cycle
      //charging phase.
      if (GetAverageVolts() > 4.200){//done charging, switch to case 2
        //set up for 30s on, 10s off
        NumberofOnPeriods = 3;
        TestPhase = 2;
      }
      break;//break case 1

    //save unloaded readings more often during the first 10% and last 10%
    //cutoff voltages: 90% == 3.837v , 10% == 3.324v

    case 2://top 10%, discharge and save readings every 30s
      if (AverageCellVoltageUnloaded < 3.837){//when unloaded readings dip below 90% SOC, switch to case 3
        //set up for a 10 minute cycle
        NumberofOnPeriods = 58;
        TestPhase = 3;
      }
      break;//break case 2

    case 3://bulk phase, 10% to 90%, save unloaded readings every 10 minutes
      if (AverageCellVoltageUnloaded < 3.324){//when unloaded readings dip below 10% SOC, switch to case 4
        NumberofOnPeriods = 3;
        TestPhase = 4;
      }
      break;//break case 3

    case 4://bottom 10%, discharge and save readings every 30s
      if (GetAverageVolts() < 2.800){//when loaded readings dip below 0% SOC, switch to case 5
        NumberofOnPeriods = 58;
        TestPhase = 5;
      }
      break;//break case 4

    case 5://recharge phase, charge up to 30%
      //for now do nothing, the operator will manually run the charge cycle.
      break;//break case 5

    
  }


  //save an unloaded reading every ? minutes ([NumberofOnPeriods+1] cycles)
  //turn off the fets after cycle [NumberofOnPeriods]
  //turning off the fets allows the battery top rest until the next 10s cycle, then it will be switched on again.
  if (cyclecount == NumberofOnPeriods){ 
    Fets_off();
  }

  //turn on the fets after cycle [NumberofOnPeriods+1]
  if (cyclecount >= (NumberofOnPeriods+1)){ 
    AverageCellVoltageUnloaded = AverageCellVoltage; //save the unloaded voltage reading
    Fets_on();
    cyclecount = 0; //reset cycle count
  }

}//end loop()


void GetAverageVolts(){
  //calculate average cell voltage
  for (int i=0; i<5; i++){
    AverageCellVoltage += bms.get_cell_voltage(i);
  }
  AverageCellVoltage = AverageCellVoltage/6;
  return AverageCellVoltage;
}//end GetAverageVolts()


void Fets_off(){
  //control FETS (charge, discharge)
  //bms.set_0xE1_mosfet_control(false, false); //FETS off
  while (bms.get_discharge_mosfet_status() || bms.get_charge_mosfet_status()){
    //half second refresh timer
    if ((millis() - lastmillis) > 500){ 
      lastmillis = millis();
      lcd.clear();
      lcd.print(F("Switching Off..."));

      lcd.setCursor(0, 2);
      lcd.print(F("Discharge FET: "));
      lcd.print(bms.get_discharge_mosfet_status());

      lcd.setCursor(0, 3);
      lcd.print(F("   Charge FET: "));
      lcd.print(bms.get_charge_mosfet_status());

      bms.set_0xE1_mosfet_control(false, false); //FETS off
    }//end 1 second timer
    bms.main_task(true); //call the BMS library every loop.
  }
}

void Fets_on(){
  //control FETS (charge, discharge)
  //bms.set_0xE1_mosfet_control(true, true); //FETs on
  int debugcyclecheck = 0;
  while (!bms.get_discharge_mosfet_status() || !bms.get_charge_mosfet_status()){
    //half second refresh timer
    if ((millis() - lastmillis) > 500){ 
      lastmillis = millis();
      lcd.clear();
      lcd.print(F("Switching On..."));

      lcd.setCursor(0, 2);
      lcd.print(F("Discharge FET: "));
      lcd.print(bms.get_discharge_mosfet_status());

      lcd.setCursor(0, 3);
      lcd.print(F("   Charge FET: "));
      lcd.print(bms.get_charge_mosfet_status());
      lcd.print(F("   "));
      lcd.print(debugcyclecheck);
      debugcyclecheck++;

      bms.set_0xE1_mosfet_control(true, true); //FETs on
    }//end 1 second timer
    bms.main_task(true); //call the BMS library every loop.
  }
}

void Save_a_reading(){
  myFile = SD.open(filename, FILE_WRITE);
  // if the file opened okay, write to it:
  if (myFile) {
    //cell labels
    //unix time,milliseconds,current,NTC 1,NTC 2,cell 1,cell 2,cell 3,cell 4,cell 5,cell 6
    now = rtc.now();
    long _unixtime = now.unixtime();
    myFile.print(_unixtime);
    myFile.print(",");
    myFile.print(millis());
    myFile.print(",");
    myFile.print(bms.get_current());
    myFile.print(",");
    myFile.print(bms.get_ntc_temperature(0), 0);
    myFile.print(",");
    myFile.print(bms.get_ntc_temperature(1), 0);
    myFile.print(",");
    //loop prints each cell voltage. This version is only for 6 cell BMSs
    myFile.print((bms.get_cell_voltage(0) *1000), 0);
    myFile.print(",");
    myFile.print((bms.get_cell_voltage(1) *1000), 0);
    myFile.print(",");
    myFile.print((bms.get_cell_voltage(2) *1000), 0);
    myFile.print(",");
    myFile.print((bms.get_cell_voltage(3) *1000), 0);
    myFile.print(",");
    myFile.print((bms.get_cell_voltage(4) *1000), 0);
    myFile.print(",");
    myFile.print((bms.get_cell_voltage(5) *1000), 0);

    myFile.println();
    // close the file:
    myFile.close();


  } else {
    // if the file didn't open, print an error:
    lcd.clear();
    lcd.print(F("error opening file"));
    lcd.setCursor(0, 2);
    lcd.print(F("aborting test"));
    Fets_off();
    delay(1000);
    Fets_off();
    while(1);
   }
}


void NewFile(){
  int serialnumber;
  EEPROM.get(eeAddress, serialnumber);
  serialnumber++;
  //filename = "test";
  //filename += serialnumber;
  //filename += ".csv";

  //get filename entry from the keyboard 
  filename_keyboard_entry();

  myFile = SD.open(filename, FILE_WRITE);
  // if the file opened okay, write tthe first line:
  delay(1);
  if (myFile) {
    //print the filename(VIN) on the first line
    myFile.print(filename);
    myFile.println(F(",,,,,,,,,,"));
    //print the spreadsheet cell labels
    myFile.println(F("unix time,milliseconds,current,NTC 1,NTC 2,cell 1,cell 2,cell 3,cell 4,cell 5,cell 6"));
    //cell_labels.print(myFile);
    //myFile.println(" ");
    // close the file:
    myFile.close();
  } else {
    // if the file didn't open, print an error:
    lcd.setCursor(0, 2);
    lcd.print(F("error opening file"));
    delay(1000);
  }
  EEPROM.put(eeAddress, serialnumber);
}

void _display(){
    //Print to the LCD, avoiding the use of delay()

    //bms.debug();
    //ProtectionStatus foo = bms.get_protection_status();
    //check communication status
    if (bms.get_comm_error_state()){ //returns false if comm is ok, true if it fails.
      //print error message
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(F("   Communication")); 
      lcd.setCursor(0, 1);
      lcd.print(F("      Failure")); 
      lcd.setCursor(0, 3);
      lcd.print(F(" No reply from BMS")); 
    }else{    
      //normal display
      lcd.clear();
      lcd.setCursor(0, 0);
      //lcd.print("SOC ");
      lcd.print(bms.get_state_of_charge());
      lcd.print("%");
      //calculate watts
      float watts = ((bms.get_voltage()) * (bms.get_current()));
      lcd.setCursor(5, 0);
      if(watts > 0){ lcd.print("+"); }
      lcd.print(watts, 0);
      lcd.print("w ");
      lcd.setCursor(14, 0);
      lcd.print(bms.get_voltage());
      lcd.print("V");
      lcd.setCursor(0, 1);
      if(bms.get_current() > 0){ lcd.print("+"); } 
      lcd.print(bms.get_current());
      lcd.print("A ");
      if (! rtc.isrunning()) {
        lcd.println(F("RTC NOT running!"));
      }else{
        lcd.print(F("  T:"));
        lcd.print(now.unixtime());
      }


      lcd.setCursor(0, 2);
      lcd.print("Average: ");
      lcd.print(AverageCellVoltage);
      lcd.print(" V");

      lcd.setCursor(0, 3);
      lcd.print("Phase: ");
      lcd.print(TestPhase);

      /* print all 6 cell voltage and both temp probes
      lcd.setCursor(0, 2);
      lcd.print((bms.get_cell_voltage(0) *1000), 0);
      lcd.print(" ");
      lcd.print((bms.get_cell_voltage(1) *1000), 0);
      lcd.print(" ");
      lcd.print((bms.get_cell_voltage(2) *1000), 0);
      lcd.print(" ");
      lcd.setCursor(17, 2);
      lcd.print(bms.get_ntc_temperature(0), 0);
      lcd.print("c");
      lcd.setCursor(0, 3);
      lcd.print((bms.get_cell_voltage(3) *1000), 0);
      lcd.print(" ");
      lcd.print((bms.get_cell_voltage(4) *1000), 0);
      lcd.print(" ");
      lcd.print((bms.get_cell_voltage(5) *1000), 0);
      lcd.print(" ");
      lcd.setCursor(17, 3);
      lcd.print(bms.get_ntc_temperature(1), 0);
      lcd.print("c");
      */
            
    }

}//end _display()


void keyboard_debug_display(){
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(filename);
}//end keyboard_debug_display()

void keyboard_filename_display(){
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Enter the filename:"));
  lcd.setCursor(0, 1);
  lcd.print(F("Format: VIN-1of16"));
  //word wrap fix- print the first 20 characters on line 2 and the rest on line 3
  lcd.setCursor(0, 2);
  lcd.print(filename.substring(0, 20));
  if (filename.length() >= 20){
    lcd.setCursor(0, 3);
    lcd.print(filename.substring(20));
  }
}//end keyboard_filename_display()()


void filename_keyboard_entry(){
  //handle keyboard input
  keyboard_filename_display();
  lcd.cursor();
  lcd.blink();
  while(1){
    Usb.Task();
    if(iskeypressed){
      //lcd.print(keyasc);
      //lcd.print(keycode);
      if (keycode == 0x2a){ //handle backspace. remove the last char in the string
        filename.remove((filename.length()-1));
      }else if(keycode == 0x28){ //handle enter key
        filename += ".csv";
        lcd.clear();
        lcd.noCursor();
        lcd.noBlink();
        break; //exit the loop
      }else{
        filename += keyasc; //append the string
      }
      keyboard_filename_display();
      iskeypressed = false;  
    }
  }
}//end filename_keyboard_entry()

void debug(){

}//end debug()
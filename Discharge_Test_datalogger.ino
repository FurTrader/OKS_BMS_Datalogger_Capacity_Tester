
#include <bms2.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <EEPROM.h>

#define SDcardChipSelect 10

#include "RTClib.h"
RTC_DS1307 rtc;
//to set the RTC time, run the ds1307 example code from adafruit
DateTime now;

//global variables
long lastmillis;
int eeAddress = 0;   //Location we want the SN data to be put.
int serialnumber = 1000; //starting serial number
String filename = "test";

File myFile;

OverkillSolarBms2 bms;

LiquidCrystal_I2C lcd(0x27,20,4);
//LiquidCrystal_I2C lcd(0x3F,20,4); //Library object for the LCD
//set the LCD address to 0x3F or 0x27 for a 20 chars and 4 line display

void setup() {
  //only execute this once to initialize the eeprom location. This will set the serial number to the initial value.
  //EEPROM.put(eeAddress, serialnumber); //comment out this line after running once
  
  //initialize the serial ports. 
  Serial.begin(9600);

  while (!Serial) {  // Wait for the BMS serial port to initialize
    }
  bms.begin(&Serial); //Initialize the BMS library object

  lcd.begin(20, 4); // set up the LCD's number of columns and rows:
  
  //print a Splash Screen on the LCD
  lcd.backlight();
  lcd.home();
  lcd.noBlink();
  lcd.noCursor();
  lcd.clear();
  lcd.print(F("Overkill Solar"));
  lcd.setCursor(0, 2);
  lcd.print(F("Data Recorder V1")); 

  delay(3000);

  //set up the SD card
  lcd.clear();
  lcd.print(F("Initializing SD card"));
  lcd.setCursor(0, 1);
  if (!SD.begin(SDcardChipSelect)) {
    lcd.print(F("initialization fail"));
    while (1);
  }
  lcd.print(F("initialization OK."));
  lcd.setCursor(0, 2);
  
  NewFile();//create a new file for this test
  
  // Check to see if the file exists:
  if (SD.exists(filename)) {
   lcd.println("file: " + filename);
  } else {
    lcd.println(filename + " NFG");
  }
  lcd.setCursor(0, 3);

  //set up the RTC
  if (! rtc.begin()) {
    lcd.println(F("Couldn't find RTC"));
  }
  if (! rtc.isrunning()) {
    lcd.println(F("RTC NOT running!"));
    // When time needs to be set on a new device, or after a power loss, the
    // following line sets the RTC to the date & time this sketch was compiled
    //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
  }else{
    now = rtc.now();
    lcd.print(F("unix time:"));
    lcd.print(now.unixtime());
  }
  

    //now wait while running the BMS library to establish communication
  lastmillis = millis();
  while ((millis() - lastmillis) < 4000){ // timer
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
}//end setup



void loop() {
  now = rtc.now();
  bms.main_task(true); //call the BMS library every loop.
  
  //1 second timer, non-blocking
  if ((millis() - lastmillis) > 1000){ 
    lastmillis = millis();
    
    _display();

    //Save_a_loaded_reading();
    Save_an_unloaded_reading();
  }//end timer
}//end loop()

void Save_a_loaded_reading(){
  myFile = SD.open(filename, FILE_WRITE);
  // if the file opened okay, write to it:
  if (myFile) {
    //cell labels
    //loaded reading unix time, current, cell 1 mv, cell 2 mv, cell 3 mv, cell 4 mv, unloaded reading unix time, current, cell 1 mv, cell 2 mv, cell 3 mv, cell 4 mv");
    myFile.print(now.unixtime() + ",");
    myFile.print(bms.get_current());
    myFile.print(",");
    //loop prints each cell voltage. This version is only for 4 cell BMSs
    myFile.print((bms.get_cell_voltage(0) *1000), 0);
    myFile.print(",");
    myFile.print((bms.get_cell_voltage(1) *1000), 0);
    myFile.print(",");
    myFile.print((bms.get_cell_voltage(2) *1000), 0);
    myFile.print(",");
    myFile.print((bms.get_cell_voltage(3) *1000), 0);
    myFile.print(",");
    // close the file:
   myFile.close();
  } else {
    // if the file didn't open, print an error:
    lcd.clear();
    lcd.print("error loaded");
    delay(4000);
   }
}

void Save_an_unloaded_reading(){
  myFile = SD.open(filename, FILE_WRITE);
  // if the file opened okay, write to it:
  if (myFile) {
    //cell labels
    //loaded reading unix time, current, cell 1 mv, cell 2 mv, cell 3 mv, cell 4 mv, unloaded reading unix time, current, cell 1 mv, cell 2 mv, cell 3 mv, cell 4 mv");
    myFile.print(now.unixtime(), ",");
    myFile.print(",");
    myFile.print(bms.get_current());
    myFile.print(",");
    myFile.print((bms.get_cell_voltage(0) *1000), 0);
    myFile.print(",");
    myFile.print((bms.get_cell_voltage(1) *1000), 0);
    myFile.print(",");
    myFile.print((bms.get_cell_voltage(2) *1000), 0);
    myFile.print(",");
    myFile.print((bms.get_cell_voltage(3) *1000), 0);
    myFile.println();
    // close the file:
    myFile.close();
  } else {
    // if the file didn't open, print an error:
    lcd.clear();
    lcd.print(F("error unloaded"));
    delay(4000);
   }
}

void NewFile(){
    EEPROM.get(eeAddress, serialnumber);
    serialnumber++;
    filename = "test";
    filename += serialnumber;
    filename += ".txt";
    File myFile = SD.open(filename, FILE_WRITE);
    // if the file opened okay, write to it:
    delay(1);
    if (myFile) {
      //print the spreadsheet cell labels
      myFile.println(F("loaded reading unix time,current,cell 1 mv,cell 2 mv,cell 3 mv,cell 4 mv,unloaded reading unix time,current,cell 1 mv,cell 2 mv,cell 3 mv,cell 4 mv"));
      //cell_labels.print(myFile);
      //myFile.println(" ");
      // close the file:
      myFile.close();
    } else {
      // if the file didn't open, print an error:
      lcd.setCursor(0, 2);
      lcd.print(F("error newfile"));
      delay(4000);
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
      lcd.print("SOC ");
      lcd.print(bms.get_state_of_charge());
      lcd.print("%");
      //calculate watts
      float watts = ((bms.get_voltage()) * (bms.get_current()));
      lcd.setCursor(10, 0);
      if(watts > 0){ lcd.print("+"); }
      lcd.print(watts, 0);
      lcd.print(" w");
      lcd.setCursor(0, 2);
      lcd.print(bms.get_voltage());
      lcd.print(" V");
      lcd.setCursor(10, 2);
      if(bms.get_current() > 0){ lcd.print("+"); } 
      lcd.print(bms.get_current());
      lcd.print(" A");
      lcd.setCursor(0, 3);
      //loop prints each cell voltage. This version is only for 4 cell BMSs
      for (int i = 0; i <= 3; i++) {
        lcd.print((bms.get_cell_voltage(i) *1000), 0);
        lcd.print(" ");
      }
      lcd.setCursor(0, 1);
      if (! rtc.isrunning()) {
        lcd.println(F("RTC NOT running!"));
      }else{
        lcd.print(F("unix time:"));
        lcd.print(now.unixtime());
      }
    }

}//end _display()

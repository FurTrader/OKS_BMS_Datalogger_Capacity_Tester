/*Datalogger and capacity tester for Overkill Solar BMSs
 *
 *This arduino datalogger will communicate with a BMS and record the test data on the SD card

 ----duty cycle: 10s on, 1s off----
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
#include <SD.h>
#include <EEPROM.h>

#define SDcardChipSelect 10

#include "RTClib.h"
RTC_DS1307 rtc;
//to set the RTC time, run the ds1307 example code from adafruit
DateTime now;

//global variables
long lastmillis;
long lastmillis2;
long lastmillis3;
int cyclecount;
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
  lcd.init();
  delay(100);
  lcd.backlight();
  lcd.home();
  lcd.noBlink();
  lcd.noCursor();
  lcd.clear();
  lcd.print(F("Overkill Solar"));
  lcd.setCursor(0, 2);
  lcd.print(F("  6s Tesla Module")); 
  lcd.setCursor(0, 3);
  lcd.print(F("Capacity Tester V1.1")); 

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

  //control FETS (charge, discharge)
  Fets_on();
}//end setup



void loop() {
  now = rtc.now();
  bms.main_task(true); //call the BMS library every loop.
  
  //1 second timer, non-blocking
  //update the lcd every second
  if ((millis() - lastmillis) > 1000){ 
    lastmillis = millis();
    _display();
  }//end 1 second timer


  //10 second timer, non-blocking
  //save a loaded reading every 10 seconds
  if ((millis() - lastmillis2) > 10000){ 
    cyclecount++;
    Save_a_reading();
    lastmillis2 = millis();
  }//end 10s timer


  //save an unloaded reading every 10 minutes (60 cycles)
  //turn off the fets after cycle 58
  if (cyclecount == 58){ 
    Fets_off();
  }//end (cyclecount == 58)

  //turn on the fets after cycle 59
  if (cyclecount == 59){ 
    Fets_on();
    cyclecount = 0; //reset cycle count
  }//end (cyclecount == 59)
  
}//end loop()



void Fets_off(){
  //control FETS (charge, discharge)
  bms.set_0xE1_mosfet_control(false, false); //FETS off
}

void Fets_on(){
  //control FETS (charge, discharge)
  bms.set_0xE1_mosfet_control(true, true); //FETs on
}

void Save_a_reading(){
  myFile = SD.open(filename, FILE_WRITE);
  // if the file opened okay, write to it:
  if (myFile) {
    //cell labels
    //loaded reading unix time, current, cell 1 mv, cell 2 mv, cell 3 mv, cell 4 mv, unloaded reading unix time, current, cell 1 mv, cell 2 mv, cell 3 mv, cell 4 mv");
    now = rtc.now();
    long _unixtime = now.unixtime();
    myFile.print(_unixtime);
    myFile.print(",");
    myFile.print(millis());
    myFile.print(",");
    myFile.print(bms.get_current());
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
    myFile.print(",");
    myFile.print(bms.get_ntc_temperature(0), 0);
    myFile.print(",");
    myFile.print(bms.get_ntc_temperature(1), 0);
    myFile.println();
    // close the file:
   myFile.close();
  } else {
    // if the file didn't open, print an error:
    lcd.clear();
    lcd.print(F("error loaded"));
    delay(4000);
   }
}


void NewFile(){
    EEPROM.get(eeAddress, serialnumber);
    serialnumber++;
    filename = "test";
    filename += serialnumber;
    filename += ".txt";
    myFile = SD.open(filename, FILE_WRITE);
    // if the file opened okay, write to it:
    delay(1);
    if (myFile) {
      //print the spreadsheet cell labels
      myFile.println(F("unix time,milliseconds,current,cell 1,cell 2,cell 3,cell 4,cell 5,cell 6,NTC 1,NTC 2"));
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
        lcd.print(F("T:"));
        lcd.print(now.unixtime());
      }

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
            
    }

}//end _display()

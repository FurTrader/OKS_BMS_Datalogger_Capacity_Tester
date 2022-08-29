# Datalogger and capacity tester for Overkill Solar BMSs
 
 This arduino datalogger will communicate with a BMS and record the test data on the SD card
 
 We used an arduino UNO clone and an Adafruit SD card shield.
 
 In some combinations, the Arduino's RX activity light will draw the signal too low from the BMS UART.
 For this setup, we had to remove the RX LED from the Arduino board.
 Other boards have been known to communicate without modification. The UNO rev3 has buffered status leds and should work fine, for example.
 The arduino Mega works well when you use Serial 1,2,or 3
 3.3v Arduino boards like the promicro always seem to communicate properly with the BMS. An ESP32 is also a good choice.
 
## Test procedure:
 
  The arduino will enable the BMS output for ten seconds then record the current and cell voltages in the data file.
  It will then disable the BMS MOSFET array for 1 second, wait for a stable reading of zero amps, and record another data set in the data file.
  
  The test can be run with a load or charger connected to the battery.
  
  The test is complete when the BMS goes into high or low voltage cutoff.
  
  The SD card can then be used to transfer the data to a computer. We used Google sheets to process the data and generate charts & graphs.
  
  The data collected can be used to graph the charge/discharge curves both loaded and unloaded, and calculate capacity and internal resistance of each cell.
  
 

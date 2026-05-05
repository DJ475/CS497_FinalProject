#include <Arduino.h>
#include <LiquidCrystal.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <Wire.h>
#include <SparkFunBME280.h>
#include <SparkFunCCS811.h>

#include <WebServer.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <LittleFS.h>
#include "FS.h"


// Source: https://www.circuitschools.com/interfacing-16x2-lcd-module-with-esp32-with-and-without-i2c/?utm_source=copilot.com
// This source was used to access functions to write to the LCD screen as well as the pinouts needed to turn the lcd screen on
LiquidCrystal LCD(19, 23, 18, 5, 15, 4);


// Sources: https://www.digikey.com/en/maker/projects/ccs811bme280-qwiic-environmental-combo-breakout-hookup-guide/93694176db544b0ea441ed491893a2ce?msockid=2a0fd793fb4160dd1bf1c387fab961b0
// This source was used to access functions to read data from the BME280 and CCS811 sensors as well as the pinouts needed to power the sensors.
#define CCS811_ADDR 0x5B // Default I2C Address
#define BME280_ADDR 0x77 // Default I2C Address

////////////////////////////////////////////////// Sensor Object Setup
// Datasheet Find:
// CCS811 Burn-in Time: Please be aware that the CCS811 datasheet recommends a burn-in of 48 hours and a run-in of 20 minutes
//  (i.e. you must allow 20 minutes for the sensor to warm up and output valid data).
CCS811 CCS811OBJ(CCS811_ADDR);
BME280 BME280OBJ;

////////////////////////////////////////////////////////// DEFINE PINS /////////////////////////////////////////////////////////
#define ButtonPin 0 // Built-In ESP32 Button Pin: D0
#define BUZZER_PIN 32  // Pin for buzzer
#define DUST_LED_PIN  33 // Pin for ILED Pin
#define DUST_AOUT_PIN 34 // Pin for Analog Out Pin


//////////////////////////////////////////////// Wireless Connection Variables /////////////////////////////////////////////////
//// Wifi Credentials to Connect to Hotspot Webserver
const char* wifiNetworkName = "SSID";
const char* wifiPassword = "PASSWORD";
const char* computerAddress = "http://MY_PC_IP:5000/data"; 

// Webserver Object Initialization, Communication Through Port 80
WebServer webServer(80);

//Bluetooth definitions
#define SERVICE_UUID        "181A"  // Environmental Sensing Service
#define CHARACTERISTIC_UUID "2A6E"  // Temperature
#define CO2_CHAR_UUID       "2A6D"  // CO2 Level
#define HUMIDITY_CHAR_UUID  "2A6F"  // Humidity
#define TVOC_CHAR_UUID      "12345678-1234-1234-1234-123456789ABC"  // TVOC (custom)

// Bluetooth Advertising Name
#define DEVICE_NAME "AirQualityMonitor"

//BLE Characteristics
BLECharacteristic* pTempChar = NULL;
BLECharacteristic* pCO2Char = NULL;
BLECharacteristic* pHumidChar = NULL;
BLECharacteristic* pTVOCChar = NULL;
bool bleConnected = false;

class ServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    bleConnected = true;
  }
  void onDisconnect(BLEServer* pServer) {
    bleConnected = false;
    pServer->startAdvertising();
  }
};

// LittleFS Variables
// Little FS Format If Failed Flag
#define FORMAT_LITTLEFS_IF_FAILED true

// Source: https://esp32tutorials.com/esp32-freertos-mutex-esp-idf/
// Used to prevent race condition between TaskController and TaskStorage during writting
xSemaphoreHandle DataWriteMutex; // Mutex used for file writing(Shared by TaskController and TaskStorage)


/////////////////////////////////////////// Enum Message Flags /////////////////////////////////////////////////////////////////////////////////////
enum MessageType {
  MSG_LCD = 0,
  MSG_BLE = 1,
  MSG_WIFI = 2,
  MSG_ENV = 3,
  MSG_ERROR = 4,
};

/////////////////////////////////////////////// Data Struct ///////////////////////////////////////////////////////////////////////////////////////
struct Data {
  MessageType MSG_TYPE; // Buffer to hold the message to be displayed on LCD
  uint16_t CO2; // Variable to hold CO2 value from CCS811 sensor
  uint16_t TVOC; // Variable to hold TVOC value from CCS811 sensor
  float tempF; // Variable to hold temperature value from BME280 sensor
  float pressure; // Variable to hold pressure value from BME280 sensor
  float humidity; // Variable to hold humidity value from BME280 sensor
  float dust; // Variable to hold dust particle values from GP2Y1010AU0F sensor
};

Data newestDataWrite; // this variable is used by the File System to encasulate the current reading values from TaskController

// Source: https://controllerstech.com/freertos-queues-arduino-task-communication/
// FreeRTOS syntax for intertask communication
// Create a queue handle for inter-task communication
QueueHandle_t ControllerQueue = xQueueCreate(5, sizeof(Data)); // This queue will use to send from sensors to send data to the Controller Task
QueueHandle_t DisplayQueue = xQueueCreate(5, sizeof(Data)); // This queue is for data being sent to the LCD Task
QueueHandle_t BLEQueue = xQueueCreate(5, sizeof(Data)); // This queue is for data being sent to the BLE Task


//// This is the signal/flag telling other tasks whether WIFI/BLE is ON(True) OR OFF(False)
bool WirelessToggleState = false; 

// Helper Function for Buzzer Alert
void makeBuzzerBeep(int freq, int duration) {
  tone(BUZZER_PIN, freq);
  delay(duration);
  noTone(BUZZER_PIN);
}
void turnBuzzerOn(int freq) { tone(BUZZER_PIN, freq); }
void turnBuzzerOff()        { noTone(BUZZER_PIN); }

void TaskEnv (void *pvParameters) 
{
  while (true) {
    // read in sensor data here and store in struct member variables
    Data dataSend;
    if (CCS811OBJ.dataAvailable())
    { 
      CCS811OBJ.readAlgorithmResults();
      dataSend.CO2 = CCS811OBJ.getCO2(); // Get CO2 value from CCS811 sensor
      dataSend.TVOC = CCS811OBJ.getTVOC(); // Get TVOC value from CCS811 sensor
      dataSend.tempF = BME280OBJ.readTempF(); // Get temperature value from BME280 sensor in farenheit
      dataSend.pressure = BME280OBJ.readFloatPressure(); // Get pressure value from BME280 sensor
      dataSend.humidity = BME280OBJ.readFloatHumidity(); // Get humidity value from BME280 sensor
      dataSend.MSG_TYPE = MSG_ENV; // Set message type to env data

      Serial.println(dataSend.CO2);
      Serial.println(dataSend.TVOC);
      Serial.println(dataSend.tempF);
      Serial.println(dataSend.pressure);
      Serial.println(dataSend.humidity);
    } 
    else if (CCS811OBJ.checkForStatusError())
    { 
      Serial.println("ERROR CS811");
      while(1);
    } 
    xQueueSend(ControllerQueue, &dataSend, portMAX_DELAY); // Send the message to the queue
    vTaskDelay(2000 / portTICK_PERIOD_MS); // Delay by 2000ms
  }
}

// Helper Class for Dust Sensor
float ReadDustSensor() {
  digitalWrite(DUST_LED_PIN, LOW);
  delayMicroseconds(280);
  int rawValue = analogRead(DUST_AOUT_PIN);
  delayMicroseconds(40);
  digitalWrite(DUST_LED_PIN, HIGH);

  float voltage = rawValue * (3.3 / 4095.0);
  float dustDensity = (voltage - 1.30) * 170.0;
  return max(dustDensity, 0.0f);
}

// Source: https://www.makerguides.com/dust-sensor-gp2y1010au0f-with-arduino/#Code_for_measuring_dust_density_with_GP2Y1010AU0F
// This source gave us the pinout and code smaples needed to read from the dust/particle sensor
void TaskDust(void *pvParameters)
{
  Data sendData;
  while(true)
  { 
    // Collect Dust Sensor Readings Here
    float dustDensity = ReadDustSensor();

    // set queue values
    sendData.dust = dustDensity;
    sendData.MSG_TYPE = MSG_ENV;
    xQueueSend(ControllerQueue, &sendData, portMAX_DELAY);

    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void TaskController (void *pvParameters) 
{
  while (true) {
    Data recievedData;
    // wait to receive data from different sensors in Controller Task
    if(xQueueReceive(ControllerQueue, &recievedData, portMAX_DELAY)) {
      
      // Check thresholds and use buzzer based on if above thresholds
      if(recievedData.CO2 > 1500 || recievedData.TVOC > 75) 
      {
        turnBuzzerOn(2000);  // direct function call
      } 
      else
      {
        turnBuzzerOff();
      }

      // if wifi/ble button is toggled on than send data to BLE Task
      if(WirelessToggleState == true) 
      {
        recievedData.MSG_TYPE = MSG_BLE;
        xQueueSend(BLEQueue, &recievedData, portMAX_DELAY);
      }

      // Route values to BLE and LCD
      recievedData.MSG_TYPE = MSG_LCD;

      // Use Semaphore to access shared resource with TaskStorage
      if(xSemaphoreTake(DataWriteMutex, portMAX_DELAY))
      {
        newestDataWrite = recievedData; // copy struct contents into global readings variable
        xSemaphoreGive(DataWriteMutex);
      }
      xQueueSend(DisplayQueue, &recievedData, portMAX_DELAY);
      /// Then send to appropriate Queue for Viewing and Displaying the data(TaskLCD and TaskBLE)
    }    
  }
}

// Task to write output to LCD screen.
void TaskLCD (void *pvParameters) 
{
  Data recievedData; // Create a data structure to hold the message to be displayed on LCD
  while (true) {
    if(xQueueReceive(DisplayQueue, &recievedData, portMAX_DELAY)) { // Wait for data to be received from the queue
      LCD.clear(); // Clear image on screen
      LCD.setCursor(0, 0); // Set lcd index to first cursor row
      switch (recievedData.MSG_TYPE)
      {
        case MSG_LCD:
          // Print message values on LCD Screen
          LCD.print("CO2:"); 
          LCD.print(recievedData.CO2);
          LCD.print(" TV:"); 
          LCD.print(recievedData.TVOC);
          LCD.setCursor(0, 1); // set LCD cursor for next line
          LCD.print("T:"); 
          LCD.print((int)recievedData.tempF);
          LCD.print("F H:"); 
          LCD.print((int)recievedData.humidity);
          LCD.print("%");
          break;
        case MSG_BLE:
          Serial.print("Data-BLE"); // Print message values to BLE
      } 
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay by 1000ms
  }
}

void TaskBLE(void *pvParameters)
{
  while(true)
  {
    // if bluetooth and  wireless toggle are enabled than start bluetooth advertising 
    if(bleConnected == true && WirelessToggleState == true)
    {
      Data recievedData;
      if(xQueueReceive(BLEQueue, &recievedData, pdMS_TO_TICKS(100)))
      {
        float tempF = recievedData.tempF;
        int co2 = recievedData.CO2;
        float humidity = recievedData.humidity;
        int tvoc = recievedData.TVOC;
  
        char dataStr[50];
        char tempStr[10];
        char humidStr[10];
  
        dtostrf(tempF, 4, 1, tempStr);
        dtostrf(humidity, 4, 1, humidStr);
  
        sprintf(dataStr, "CO2:%d TVOC:%d %sF %s%%", co2, tvoc, tempStr, humidStr);
  
        // Send the same formatted string to all characteristics
        pTempChar->setValue(dataStr);
        pTempChar->notify();
  
        pCO2Char->setValue(dataStr);
        pCO2Char->notify();
  
        pHumidChar->setValue(dataStr);
        pHumidChar->notify();
  
        pTVOCChar->setValue(dataStr);
        pTVOCChar->notify();
      }

    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void TaskWireless(void *pvParameters)
{
  while(true)
  {
    int buttonState = digitalRead(ButtonPin);
    // If button pressed than toggle semaphore flag/signal to turn on wifi
    if(buttonState == LOW) 
    {
      // toggle logic here
      if(WirelessToggleState == true)
      {
        WirelessToggleState = false;
        Serial.println("WIFI/BLE OFF");
      }
      else
      {
        Serial.println("WIFI/BLE ON");
        WirelessToggleState = true;
      }
      vTaskDelay(300 / portTICK_PERIOD_MS); // Debounce Button Pressing
    }
    vTaskDelay(200 / portTICK_PERIOD_MS); // Poll every 100 ms in order to not miss a button press event
  }
}

////////////////////////////////////// Helper Functions For LittleFS File Operations /////////////////////////////////////////////
// Source: https://randomnerdtutorials.com/esp32-write-data-littlefs-arduino/#esp32-little-fs-handle-files
// We used this source for syntax in using LittleFS for longterm storage of environmental data

// Create Data Directory if not Exists 
void createDir(fs::FS &fs, const char * path){
    Serial.printf("Creating Dir: %s\n", path);
    if(fs.mkdir(path)){
        Serial.println("Directory Created");
    } else {
        Serial.println("Directory Creation Failed");
    }
}

// Write message to file
void writeFile(fs::FS &fs, const char * path, const char * message){
    Serial.printf("Writing file: %s\r\n", path);

    File file = fs.open(path, FILE_WRITE);
    if(!file){
        Serial.println("- failed to open file for writing");
        return;
    }
    if(file.print(message)){
        Serial.println("- file written");
    } else {
        Serial.println("- write failed");
    }
    file.close();
}

// Append to existing log file
void appendLog(Data &capture) {
  File logFile = LittleFS.open("/env.csv", FILE_APPEND);
  if(logFile) {
    logFile.print(esp_timer_get_time() / 1000000ULL); logFile.print(",");
    logFile.print(capture.CO2);                      logFile.print(",");
    logFile.print(capture.TVOC);                     logFile.print(",");
    logFile.print(capture.tempF);                    logFile.print(",");
    logFile.print(capture.humidity);                 logFile.print(",");
    logFile.print(capture.pressure);                 logFile.print(",");
    logFile.println(capture.dust); // println adds \n
    logFile.close();
    Serial.println("Log entry written to LittleFS");
  } else {
    Serial.println("Failed to open log file");
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void TaskStorage(void *pvParameters)
{
  uint64_t logTimerInterval = 60ULL * 60ULL * 1000000ULL; // this caluclates into waiting about 1 hour
  uint64_t lastTimeLogged = esp_timer_get_time(); // get current time storing last time logged
  while(true)
  {
    Data captureReadings;

    // Source: https://forum.arduino.cc/t/esp32-freertos-scheduling-task-with-delays-in-days/1092813/6
    // Using this source we were able to schedule the task to run every hour writing to the csv stored in the file system in a less power consuming way

    // check if time has exceeded the hour timer
    if((esp_timer_get_time() - lastTimeLogged) >= logTimerInterval)
    { 
      // check if reasource is available before write operation
      if(xSemaphoreTake(DataWriteMutex, portMAX_DELAY))
      {
        captureReadings = newestDataWrite; // copy values from current times sensor readings into local captureReadings variable
        xSemaphoreGive(DataWriteMutex); // give back mutex
      }
      
      appendLog(captureReadings); // write captured data to file system
      lastTimeLogged = esp_timer_get_time(); // update last time logged after log operation
    }
    vTaskDelay(10000 / portTICK_PERIOD_MS); // check timerinterval every 10 seconds
  }
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200); // Serial Communication Intialization
  Wire.begin(); // I2C Communication Intialization

  pinMode(ButtonPin, INPUT_PULLUP); // setup button pin as input
  pinMode(BUZZER_PIN, OUTPUT); // setup buzzer as output
  pinMode(DUST_LED_PIN, OUTPUT); // setup for dust led pin

  DataWriteMutex = xSemaphoreCreateMutex();

  /* ################################################### BLE SETUP ##################################################################### */ 
  BLEDevice::init(DEVICE_NAME);
  BLEServer* pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());
  
  BLEService* pService = pServer->createService(SERVICE_UUID);
  
  pTempChar = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pTempChar->addDescriptor(new BLE2902());
  
  pCO2Char = pService->createCharacteristic(
    CO2_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pCO2Char->addDescriptor(new BLE2902());
  
  pHumidChar = pService->createCharacteristic(
    HUMIDITY_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pHumidChar->addDescriptor(new BLE2902());
  
  pTVOCChar = pService->createCharacteristic(
    TVOC_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pTVOCChar->addDescriptor(new BLE2902());
  
  pService->start();
  pServer->getAdvertising()->start();

  /* ##################################################### COMPONENT SETUP ############################################################# */
  // LCD SETUP
  LCD.begin(16, 2); // LCD Column and Row Initialization
  LCD.clear(); // Clear image on screen
  LCD.print("System Started.."); // Print System Started on LCD
  delay(2500); // Delay by 2000ms to show the message on LCD
  LCD.clear(); // Clear the Start Message

  // ENVIRONMENTAL SENSOR SETUP
  BME280OBJ.settings.commInterface = I2C_MODE;
  BME280OBJ.settings.I2CAddress = 0x77;
  BME280OBJ.settings.runMode = 3; // Normal mode
  BME280OBJ.settings.tStandby = 0;
  BME280OBJ.settings.filter = 4;
  BME280OBJ.settings.tempOverSample = 5;
  BME280OBJ.settings.pressOverSample = 5;
  BME280OBJ.settings.humidOverSample = 5;
  delay(10); // BME280 needs a delay to give time to start up and apply settings

  if(BME280OBJ.begin() != 0x60)
  {
    LCD.print("INIT ERR BME280");
  }
  else
  {
    LCD.print("BME280:Started");
  }

  //// CCS811 SETUP
  CCS811OBJ.begin();
  delay(2500); // Give CCS811 Time to Startup/Boot
  LCD.clear(); // Clear message after Initialization succeeds
  LCD.print("CCS811:Started");
  delay(2500);
  LCD.clear(); // Clear Initalize Messages

  //// Dust Sensor Setup
  digitalWrite(DUST_LED_PIN, HIGH); // Turn off led light that detects particles initially


  //////////////////////////////////////////////////// LITTLEFS SETUP ////////////////////////////////////////////////////////////

  // Check if LITTLEFS Can Be Mounted

  // If error mounting littlefs show error message then return
  if(!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)){
      Serial.println("LittleFS Mount Failed");
      return;
  }
  
  if(!LittleFS.exists("/env.csv")) 
  {
    File logFile = LittleFS.open("/env.csv", FILE_WRITE);
    if(logFile) 
    {
      logFile.println("co2,tvoc,temp_f,humidity,pressure,dust");
      logFile.close();
    }
    else
    {
      Serial.print("Cannot Create Log File");
    }
  }

  /*  ################################################ FREERTOS TASK CREATION ######################################################### */
  xTaskCreate(
    TaskEnv,
    "Environment Sensor Task",
    2048,
    NULL,
    3,
    NULL
  );
  
  xTaskCreate(
    TaskDust,
    "Dust Sensor Task",
    2048,
    NULL,
    3,
    NULL
  );

  xTaskCreate(
    TaskController,
    "Controller Task",
    2048,
    NULL,
    3,
    NULL
  );
  
  xTaskCreate(
    TaskLCD,
    "LCD Task",
    2048,
    NULL,
    1,
    NULL
  );

  xTaskCreate(
    TaskBLE,
    "BLE Task",
    4096,
    NULL,
    2,
    NULL
  );  
  
  // This task enables and disables wireless protocols/polling for WIFI and BLE
  xTaskCreate(
    TaskWireless,
    "WIRELESS TOGGLE Task",
    2048,
    NULL,
    2,
    NULL
  ); 

  xTaskCreate(
    TaskStorage,
    "Storage Task",
    4096,
    NULL,
    2,
    NULL
  );

  // // Add Last
  // xTaskCreate(
  //   TaskWIFI,
  //   "WIFI Task",
  //   4096,
  //   NULL,
  //   1,
  //   NULL
  // ); 
}

void loop() {}


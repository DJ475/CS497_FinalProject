#include <Arduino.h>
#include <LiquidCrystal.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include <Wire.h>
#include <SparkFunBME280.h>
#include <SparkFunCCS811.h>

// Source: https://www.circuitschools.com/interfacing-16x2-lcd-module-with-esp32-with-and-without-i2c/?utm_source=copilot.com
// This source was used to access functions to write to the LCD screen as well as the pinouts needed to turn the lcd screen on
LiquidCrystal LCD(19, 23, 18, 5, 15, 4);


// Sources: https://www.digikey.com/en/maker/projects/ccs811bme280-qwiic-environmental-combo-breakout-hookup-guide/93694176db544b0ea441ed491893a2ce?msockid=2a0fd793fb4160dd1bf1c387fab961b0
// This source was used to access functions to read data from the BME280 and CCS811 sensors as well as the pinouts needed to power the sensors.
#define CCS811_ADDR 0x5B // Default I2C Address
#define BME280_ADDR 0x77 // Default I2C Address

CCS811 CCS811OBJ(CCS811_ADDR);
// Datasheet Find:
// CCS811 Burn-in Time: Please be aware that the CCS811 datasheet recommends a burn-in of 48 hours and a run-in of 20 minutes
//  (i.e. you must allow 20 minutes for the sensor to warm up and output valid data).

BME280 BME280OBJ;

enum MessageType {
  MSG_LCD = 0,
  MSG_BLE = 1,
  MSG_WIFI = 2,
  MSG_ENV = 3,
  MSG_ERROR = 4,
};

enum SystemMain{
  SYS_SENSOR_POLLING = 0,
  SYS_BLE = 1,
  SYS_WIFI = 2,
  SYS_ALERT = 3,
  SYS_LOGGING = 4, 
  SYS_ERROR = 5,
};

struct Data {
  MessageType MSG_TYPE; // Buffer to hold the message to be displayed on LCD
  uint16_t CO2; // Variable to hold CO2 value from CCS811 sensor
  uint16_t TVOC; // Variable to hold TVOC value from CCS811 sensor
  float tempF; // Variable to hold temperature value from BME280 sensor
  float pressure; // Variable to hold pressure value from BME280 sensor
  float humidity; // Variable to hold humidity value from BME280 sensor
  float dust; // Variable to hold dust particle values from GP2Y1010AU0F sensor
};


// Source: https://controllerstech.com/freertos-queues-arduino-task-communication/
// FreeRTOS syntax for intertask communication
// Create a queue handle for inter-task communication
QueueHandle_t ControllerQueue = xQueueCreate(5, sizeof(Data)); // This queue will use to send from sensors to send data to the Controller Task
QueueHandle_t DisplayQueue = xQueueCreate(5, sizeof(Data)); // This queue is for data being sent to the LCD Task
QueueHandle_t BLEQueue = xQueueCreate(5, sizeof(Data)); // This queue is for data being sent to the BLE Task

void TaskBuzzer(void *pvParameters)
{
  while(true)
  {
    // Add Alarm/Buzzer Calls Here When Dust/Env is in dangerous levels
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

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

void TaskDust(void *pvParameters)
{
  while(true)
  { 
    // Collect Dust Sensor Readings Here
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void TaskController (void *pvParameters) 
{
  while (true) {
    Data recievedData;
    // wait to receive data from different sensors in Controller Task
    if(xQueueReceive(ControllerQueue, &recievedData, portMAX_DELAY)) {
      // After received sensor data change states based on data
      
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
          LCD.print("Data-LCD:"); // Print message values on LCD Screen
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
    // add ble notification/communication here
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200); // Serial Communication Intialization
  Wire.begin(); // I2C Communication Intialization

  /* ##################################################### COMPONENT SETUP ############################################################# */
  // LCD SETUP
  LCD.begin(16, 2); // LCD Column and Row Initialization
  LCD.clear(); // Clear image on screen
  LCD.print("System Started.."); // Print System Started on LCD
  delay(2000); // Delay by 2000ms to show the message on LCD

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
    Serial.print("INIT ERR BME280");
  }

  
  // CCS811 SETUP
  if(CCS811OBJ.begin() != 0)
  {
    Serial.print("INIT ERR CCS811");
  }

  /*  ################################################ FREERTOS TASK CREATION ######################################################### */
  xTaskCreate(
    TaskBuzzer,
    "Buzzer Task",
    2048,
    NULL,
    1,
    NULL
  );

  xTaskCreate(
    TaskEnv,
    "Environment Sensor Task",
    2048,
    NULL,
    1,
    NULL
  );
  
  xTaskCreate(
    TaskDust,
    "Dust Sensor Task",
    2048,
    NULL,
    1,
    NULL
  );

  xTaskCreate(
    TaskController,
    "Controller Task",
    2048,
    NULL,
    1,
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
    2048,
    NULL,
    1,
    NULL
  );  
}

void loop() {}


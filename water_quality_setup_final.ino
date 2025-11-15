#include <OneWire.h>
#include <DallasTemperature.h>
#include <RtcDS1302.h>
#include <DFRobot_PH.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>
#include <SD.h>
#include <SPI.h>

// Pin Definitions
#define TEMP_SENSOR_PIN 9
#define ORP_PIN A1
#define PH_PIN A3
#define GSM_RX 7
#define GSM_TX 6
#define SD_CS_PIN 10

// ORP Constants
#define VOLTAGE 5.00
#define OFFSET 0
#define ArrayLenth 40

// Global Objects
OneWire oneWire(TEMP_SENSOR_PIN);
DallasTemperature tempSensor(&oneWire);
ThreeWire myWire(3, 2, 4); // IO, SCLK, CE
RtcDS1302<ThreeWire> Rtc(myWire);
DFRobot_PH ph;
SoftwareSerial SIM900A(GSM_TX,GSM_RX);

// Global Variables
float tempCelsius;
float tempFahrenheit;
int orpArray[ArrayLenth];
int orpArrayIndex = 0;
float voltage, phValue, temperature = 25;
const char phoneNumber[] = "+265880452766";

// Timing variables
unsigned long lastReadingTime = 0;
unsigned long lastSDLogTime = 0;
unsigned long lastSMSTime = 0;
const unsigned long readInterval = 20;      // Read sensors every 20ms
const unsigned long logInterval = 60000;    // Log every minute
const unsigned long smsInterval = 300000;   // Send SMS every 5 minutes

void setup() {
    Serial.begin(9600);
    Serial.println("Starting system initialization...");
    
    // Initialize SD card first
    Serial.print("Initializing SD card...");
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("SD card initialization failed!");
        //while(1);  // Stop if SD fails
    }
    Serial.println("SD card initialization successful!");
    
    // Create or check data file
    File dataFile = SD.open("SENSOR.CSV", FILE_WRITE);
    if (dataFile) {
        if (dataFile.size() == 0) {
            dataFile.println("DateTime,Temperature(C),pH,ORP(mV)");
        }
        dataFile.close();
        Serial.println("Data file ready");
    }
    
    // Initialize other components
    SIM900A.begin(9600);
    Serial.println("GSM initialized");
    
    tempSensor.begin();
    tempSensor.setWaitForConversion(true);
    tempSensor.setResolution(12);
    Serial.println("Temperature sensor initialized");
    
    ph.begin();
    Serial.println("pH sensor initialized");
    
    Rtc.Begin();
    if (!Rtc.GetIsRunning()) {
        Serial.println("RTC was not running, starting now");
        Rtc.SetIsRunning(true);
    }
    Serial.println("RTC initialized");
    
    // Initialize ORP array
    for (int i = 0; i < ArrayLenth; i++) {
        orpArray[i] = 0;
    }
    
    delay(1000);
    Serial.println("Setup complete!\n");
}

void getDateTime(char* datestring) {
    RtcDateTime now = Rtc.GetDateTime();
    snprintf_P(datestring, 
            26,
            PSTR("%02u/%02u/%04u %02u:%02u:%02u"),
            now.Month(),
            now.Day(),
            now.Year(),
            now.Hour(),
            now.Minute(),
            now.Second());
}

void logData(const char* datetime, float temp, float ph, double orp) {
    File dataFile = SD.open("SENSOR.CSV", FILE_WRITE);
    if (dataFile) {
        String dataString = String(datetime) + "," + 
                           String(temp, 1) + "," + 
                           String(ph, 2) + "," + 
                           String((int)orp);
        dataFile.println(dataString);
        dataFile.close();
        Serial.println("Data logged: " + dataString);
    } else {
        Serial.println("Error opening data file!");
    }
}

void sendSMS(const char* datetime, float temp, float ph, double orp) {
    String message = String(datetime) + ";" + 
                    String(temp, 1) + "C;" + 
                    String(ph, 2) + "pH;" + 
                    String((int)orp) + "mV";
                    
    Serial.println("Sending SMS: " + message);
    
    SIM900A.println("AT+CMGF=1");
    delay(1000);
    SIM900A.print("AT+CMGS=\"");
    SIM900A.print(phoneNumber);
    SIM900A.println("\"");
    delay(1000);
    SIM900A.println(message);
    delay(100);
    SIM900A.println((char)26);
    delay(1000);
    
    Serial.println("SMS sent!");
}

// Function to calculate average for ORP readings
double avergearray(int* arr, int number) {
  int i;
  int max, min;
  double avg;
  long amount = 0;
  
  if(number <= 0) {
    Serial.println("Error: Invalid array length");
    return 0;
  }
  
  if(number < 5) {   //less than 5, calculated directly statistics
    for(i = 0; i < number; i++) {
      amount += arr[i];
    }
    avg = amount/number;
    return avg;
  } else {
    if(arr[0] < arr[1]) {
      min = arr[0]; max = arr[1];
    } else {
      min = arr[1]; max = arr[0];
    }
    for(i = 2; i < number; i++) {
      if(arr[i] < min) {
        amount += min;
        min = arr[i];
      } else {
        if(arr[i] > max) {
          amount += max;
          max = arr[i];
        } else {
          amount += arr[i];
        }
      }
    }
    avg = (double)amount/(number-2);
  }
  return avg;
}

void loop() {
    unsigned long currentTime = millis();
    static unsigned long statusTimer = 0;
    
    // Read ORP sensor
    if (currentTime - lastReadingTime >= readInterval) {
        orpArray[orpArrayIndex++] = analogRead(ORP_PIN);
        if (orpArrayIndex == ArrayLenth) orpArrayIndex = 0;
        lastReadingTime = currentTime;
    }
    
    // Log data and send SMS
    if (currentTime - lastSDLogTime >= logInterval) {
        char datestring[26];
        getDateTime(datestring);
        
        // Read sensors
        tempSensor.requestTemperatures();
        delay(750);  // Give time for conversion
        tempCelsius = tempSensor.getTempCByIndex(0);
        
        voltage = analogRead(PH_PIN)/1024.0*5000;
        phValue = ph.readPH(voltage, temperature);
        
        double orpValue = ((30*(double)VOLTAGE*1000)-(75*avergearray(orpArray, ArrayLenth)*VOLTAGE*1000/1024))/75-OFFSET;
        
        // Log to SD
        logData(datestring, tempCelsius, phValue, orpValue);
        lastSDLogTime = currentTime;
        
        // Send SMS if interval has elapsed
        if (currentTime - lastSMSTime >= smsInterval) {
            sendSMS(datestring, tempCelsius, phValue, orpValue);
            lastSMSTime = currentTime;
        }
    }
    
    // Print status every 10 seconds
    if (currentTime - statusTimer >= 10000) {
        Serial.println("System running...");
        statusTimer = currentTime;
    }
}
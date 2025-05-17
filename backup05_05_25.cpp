#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <PZEM004Tv30.h>
#include <SD.h>

#include <time.h>

// #include <Ethernet.h>
// #include <EthernetClientSecure.h>

#include <WiFi.h>

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include <WebSocketsClient.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


//bagian flowmeter
// Pin connected to the flow meter
const int flowMeterPin = 25; 
volatile int pulseCount = 0;
float currentTime;
float previousTime;
float flowRate;
double volume = 0;

// Ditentukan oleh datasheet flow meter
float carFactorFlow = 7.5;

//Prosedur interrupt counter pulsa flowmeter
void IRAM_ATTR pulseCounter() 
{
  pulseCount++;
}

void collectFlowMeterData()
{
  //Matikan interrupt
  detachInterrupt(digitalPinToInterrupt(flowMeterPin));

  //Menghitung flow rate
  flowRate = (float)pulseCount /carFactorFlow;

  //Print flow rate
  // Serial.print("Pulse count: ");
  // Serial.print(pulseCount);

  // Serial.print(", Flow rate: ");
  // Serial.print(flowRate);
  // Serial.print(" L/min");

  //Hitung volume
  volume = volume + flowRate/60; //flow rate dalam L/min, maka volume dalam L
  // Serial.print(", Volume: ");
  // Serial.print(volume);
  // Serial.print(" L, ");

  // Serial.print(volume/1000);
  // Serial.println(" m3");

  //Reset pulseCount dan currentTime
  pulseCount = 0;
  previousTime = currentTime;

  //Setelah delay 1 detik, aktifkan interrupt kembali
  attachInterrupt(digitalPinToInterrupt(flowMeterPin), pulseCounter, FALLING);
}

//bagian PZEM kwh meter
//Pin RX dan TX Serial2
int rxPin = 16;
int txPin = 17;

PZEM004Tv30 pzem(Serial2, rxPin, txPin);

float voltage = 0;
float current = 0;
float frequency = 0;
float pf = 0;

double energy = 0;
float power = 0;

float carFactorVoltage = 1.0;
float carFactorCurrent = 1.0;
float carFactorPf = 1.0;
float carFactorFrequency = 1.0;

void collectKwhMeterData()
{
  //Bagian PZEM kwh meter
  //if one of the value NaN it still zero and not change
  if (pzem.voltage() != NULL && pzem.current() != NULL && pzem.frequency() != NULL && pzem.pf() != NULL)
  {
    voltage = carFactorVoltage * pzem.voltage();
    current = carFactorCurrent * pzem.current();
    frequency = carFactorFrequency * pzem.frequency();
    pf = carFactorPf * pzem.pf();

    power = pf * voltage * current;

    //dihitung setiap detik energi dalam kWh
    if (power > 0)
      energy = energy + power/(3600); //power dalam W, maka energy dalam Wh
    else
      energy = 0;

    // Serial.print("Voltage: ");
    // Serial.print(voltage);
    // Serial.print("V, Current: ");
    // Serial.print(current);
    // Serial.print("A, pf ");
    // Serial.print(pf);
    // Serial.print(", frequency: ");
    // Serial.print(frequency);
    // Serial.print("Hz, Power: ");
    // Serial.print(power);
    // Serial.print("W, Energy: ");
    // Serial.print(energy/1000);
    // Serial.println("kWh");
  }
}

//Bagian RTC
const char* ntpServer = "id.pool.ntp.org";
const long  gmtOffset_sec = 7*3600; //Set GMT+7
const int   daylightOffset_sec = 0; //No daylight saving time
struct tm timeinfo;

int currYear = 0;
int currMonth = 0;
int currDay = 0;
int currHour = 0;
int currMinute = 0;
int currSecond = 0;

void collectRTCData()
{
  //Bagian waktu RTC
  if (!getLocalTime(&timeinfo)) 
  {
    Serial.println("Failed to obtain time");
    return;
  }
  else
  {
    currYear = timeinfo.tm_year + 1900;
    currMonth = timeinfo.tm_mon + 1;
    currDay = timeinfo.tm_mday;
    currHour = timeinfo.tm_hour;
    currMinute = timeinfo.tm_min;
    currSecond = timeinfo.tm_sec;

    // Serial.print("Current time: ");
    // Serial.print(currYear);
    // Serial.print("-");
    // Serial.print(currMonth);
    // Serial.print("-");
    // Serial.print(currDay);
    // Serial.print(" ");
    // Serial.print(currHour);
    // Serial.print(":");
    // Serial.print(currMinute);
    // Serial.print(":");
    // Serial.println(currSecond);
  }
}


//bagian LCD
int SDApin = 21;
int SCLpin = 22;

int lcdAddress = 0x27;
int lcdColumns = 16;
int lcdRows = 2;

LiquidCrystal_I2C lcd(lcdAddress, lcdColumns, lcdRows);

int stage = 0;
float SwitchStageTime = 3000; //3 detik
float lcdTime1;
float lcdTime2;

void lcdDisplayData()
{
  if (stage == 0)
  {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Flow: ");
  lcd.print(flowRate);
  lcd.print(" L/min");
  lcd.setCursor(0, 1);
  lcd.print("Volume: ");
  lcd.print(volume);
  lcd.print(" L");
  stage = 1;
  }

  else if (stage == 1)
  {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Voltage: ");
  lcd.print(voltage);
  lcd.print(" V");
  lcd.setCursor(0, 1);
  lcd.print("Current: ");
  lcd.print(current);
  lcd.print(" A");
  stage = 2;
  }

  else if (stage == 2)
  {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Power: ");
  lcd.print(power);
  lcd.print(" W");
  lcd.setCursor(0, 1);
  lcd.print("Energy: ");
  lcd.print(energy);
  lcd.print(" Wh");
  stage = 3;
  }

  else if (stage == 3)
  {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Frequency: ");
  lcd.print(frequency);
  lcd.print(" Hz");
  lcd.setCursor(0, 1);
  lcd.print("PF: ");
  lcd.print(pf);
  stage = 4;
  }

  else if (stage == 4)
  {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Time: ");
  lcd.print(currHour);
  lcd.print(":");
  lcd.print(currMinute);
  lcd.print(":");
  lcd.print(currSecond);
  lcd.setCursor(0, 1);
  lcd.print("Date: ");
  lcd.print(currYear);
  lcd.print("-");
  lcd.print(currMonth);
  lcd.print("-");
  lcd.print(currDay);
  stage = 0;
  }
}

int csEthPin = 5;
int csSdPin = 14;

//Bagian SD Card
float saveDataPeriod = 10000; //Simpan data setiap 10 detik
float timecount1;
float timecount2;


//lastest data untuk di send ke server
float lastVolume = 0;
float lastFlowRate = 0;
float lastCurrent = 0;
float lastVoltage = 0;
float lastPf = 0;
float lastFrequency = 0;
float lastPower = 0;
float lastEnergy = 0;


// Function to check available space
bool isCardFull() 
{
    uint32_t totalBytes = SD.totalBytes();
    uint32_t usedBytes = SD.usedBytes();
    uint32_t freeBytes = totalBytes - usedBytes;
    return freeBytes < 838860800;; // threshold 800 MB
    //calculate in 1.5 GB manually
}

// Function to delete the oldest file
void deleteOldestFile() 
{
    File root = SD.open("/");
    File file;
    File oldestFile;
    uint32_t oldestTime = UINT32_MAX;

    while (file = root.openNextFile()) 
    {
        if (!file.isDirectory()) 
        {
            uint32_t fileTime = file.getLastWrite();
            if (fileTime < oldestTime) 
            {
                oldestTime = fileTime;
                if (oldestFile) 
                {
                    oldestFile.close();
                }
                oldestFile = file;
            } 
            else 
            {
                file.close();
            }
        }
    }

    if (oldestFile) 
    {
        Serial.print("Deleting file: ");
        Serial.println(oldestFile.name());
        SD.remove(oldestFile.name());
        oldestFile.close();
    }
}

void saveDatainSD()
{
  //Simpan data di SD card
  digitalWrite(csEthPin, HIGH);
  digitalWrite(csSdPin, LOW);
  SD.begin(csSdPin);

  if(SD.begin(csSdPin))
  { 
    if(isCardFull())
    {
      deleteOldestFile();
    }
    
    else
    {
      // Create the filename
      char filename[30];
      snprintf(filename, sizeof(filename), "/%04d%02d%02d;%02d%02d%02d-log.json", currYear, currMonth, currDay, currHour, currMinute, currSecond);

      File dataFile = SD.open(filename, FILE_WRITE);
      if (dataFile)
      {
        // Create a JSON document
        StaticJsonDocument<256> doc;
        doc["volume"] = volume;
        doc["flowRate"] = flowRate;
        doc["current"] = current;
        doc["voltage"] = voltage;
        doc["pf"] = pf;
        doc["frequency"] = frequency;
        doc["power"] = power;
        doc["energy"] = energy/1000; //dalam kWh
        doc["date"] = String(currYear) + "-" + String(currMonth) + "-" + String(currDay);
        doc["time"] = String(currHour) + ":" + String(currMinute) + ":" + String(currSecond);

        // Serialize JSON to string
        String output;
        serializeJson(doc, output);

        // Write JSON string to file
        dataFile.println(output);
        dataFile.close();
        Serial.println("Data written to file successfully.");
        
      }
      
      else
      {
        Serial.println("Error opening file.");
      }
    }
  }
  else
  {
    Serial.println("Error initializing SD card");
  }

}

File findLatestDataFile() 
{
  File root = SD.open("/");
  File file;
  File latestFile;
  uint32_t latestTime = 0;

  while (file = root.openNextFile()) 
  {
    if (!file.isDirectory()) 
    {
      uint32_t fileTime = file.getLastWrite();
      if (fileTime > latestTime) 
      {
        latestTime = fileTime;
        if (latestFile) 
        {
          latestFile.close();
        }
        latestFile = file;
      }

      else 
      {
        file.close();
      }
    }
  }
  return latestFile;
}

void saveLatestData() 
{
  File latestFile = findLatestDataFile();
  if (latestFile) 
  {
    Serial.print("Saving data from file: ");
    Serial.println(latestFile.name());

    // Read the file content and save it to variables
    while (latestFile.available()) 
    {
      String line = latestFile.readStringUntil('\n');
      Serial.println(line);
      // Parse the JSON data
      StaticJsonDocument<256> doc;
      deserializeJson(doc, line);

      // Save the data to variables
      lastVolume = doc["volume"];
      lastFlowRate = doc["flowRate"];
      lastCurrent = doc["current"];
      lastVoltage = doc["voltage"];
      lastPf = doc["pf"];
      lastFrequency = doc["frequency"];
      lastPower = doc["power"];
      lastEnergy = doc["energy"];
    }
    latestFile.close();
  } else 
  {
    Serial.println("No files found.");
  }
}

void energy_volume_intial()
{
  //Cari latestData
  //kemudian ganti variabel energy dan volume dengan lastestData
  File latestFile = findLatestDataFile();

  if (latestFile) 
  {
    Serial.print("Saving data from file: ");
    Serial.println(latestFile.name());

    // Read the file content and save it to variables
    while (latestFile.available()) 
    {
      String line = latestFile.readStringUntil('\n');
      Serial.println(line);
      // Parse the JSON data
      StaticJsonDocument<256> doc;
      deserializeJson(doc, line);

      // Save the data to variables
      volume = doc["volume"];
      energy = doc["energy"];
    }
    latestFile.close();
  } 
  else 
  {
    Serial.println("No files found.");
  }
}


// //Bagian Ethernet
String mac_address = "0xDE:0xAD:0xBE:0xEF:0xFE:0xED";

byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};

// bool connectToETH()
// {
//   digitalWrite(csSdPin, HIGH);
//   digitalWrite(csEthPin, LOW);
//   Ethernet.init(csEthPin);

//   if(!ETH.begin(ETH_PHY_W5500, 1, csEthPin, -1, -1, SPI2_HOST, 14, 12, 13))
//   {
//     lcd.clear();
//     Serial.println("Failed to Configure Ethernet.");
//     lcd.setCursor(0,0);
//     lcd.print("Ethernet Failed!");
//     return false;
//   }
//   lcd.clear();
//   Serial.println("Ethernet configured");
//   lcd.setCursor(0,0);
//   lcd.print("Ethernet Connected");
//   Serial.print("IP Address: ");
//   Serial.println(ETH.localIP());
//   Serial.print("Gateway: ");
//   Serial.println(ETH.gatewayIP());
//   Serial.println("DNS: ");
//   Serial.println(ETH.dnsIP());
//   return true;
// }

//Bagian Wifi

String ssid =  "";
String password =  "";

void connectToWiFi()
{ 
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Connecting WiFi...");
  WiFi.begin(ssid.c_str(), password.c_str());
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    Serial.print(".");
  }
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("WiFi Connected");
}

//Bagian aktuator
int SVPin = 26;
int SSRPin = 27;
bool aktuator_state = false;
bool SA_state = false;
bool SSR_state = false;

void toogleActuator()
{
  if (aktuator_state == false)
  {
    digitalWrite(SVPin, HIGH);
    digitalWrite(SSRPin, HIGH);
    aktuator_state = true;
    SA_state = true;
    SSR_state = true;
  }
  else
  {
    digitalWrite(SVPin, LOW);
    digitalWrite(SSRPin, LOW);
    aktuator_state = false;
    SA_state = false;
    SSR_state = false;
  }
}

//Bagian WebSockets
const char* websocketServerUrl = "172.20.10.2"; 
const uint16_t websocketPort = 4443;
WebSocketsClient webSocket;
WiFiClientSecure wifiClient;


//Generate Sertifikat Pra-komunikasi
const char* rootCACertificate = "-----BEGIN CERTIFICATE-----\\n\
MIIECTCCAvGgAwIBAgIUezRyaF9arcjWsbUVhOkQ/cxkPKUwDQYJKoZIhvcNAQEL\\n\
BQAwgZMxCzAJBgNVBAYTAklEMRMwEQYDVQQIDApKYXdhIEJhcmF0MRAwDgYDVQQH\\n\
DAdCYW5kdW5nMSUwIwYDVQQKDBxQVCBKZW1iYXRhbiBEaWdpdGFsIElub3ZhdGlm\\n\
MQ8wDQYDVQQLDAZKYXJkaW4xJTAjBgkqhkiG9w0BCQEWFmphc29uZWNlbi5jb0Bn\\n\
bWFpbC5jb20wHhcNMjQxMDA0MDg0NjU0WhcNMjUxMDA0MDg0NjU0WjCBkzELMAkG\\n\
A1UEBhMCSUQxEzARBgNVBAgMCkphd2EgQmFyYXQxEDAOBgNVBAcMB0JhbmR1bmcx\\n\
JTAjBgNVBAoMHFBUIEplbWJhdGFuIERpZ2l0YWwgSW5vdmF0aWYxDzANBgNVBAsM\\n\
BkphcmRpbjElMCMGCSqGSIb3DQEJARYWamFzb25lY2VuLmNvQGdtYWlsLmNvbTCC\\n\
ASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALuAXgArqBhUmsdYozdYO/yN\\n\
hmtPy4wAzWwaBSVKe901HmjnausWDy7GcnY1kClOcowr0xtJlQoijYsL2hTwHkN9\\n\
rMePLALhClJAkq1Ga1NZd9uo7YBB6eNFdPQIsgETzsObnbepURoDCHad3L+/RDoJ\\n\
kZiqert/8PvJfQJBK0JSLCX+5u+epebTrvb+8FLEBRj+YP2o8lcpymQvSczH6Hqp\\n\
7m0EpqYRNlzvtFcmKArhHTt7kwGleTK4K6Zkxsi0tSdOdQAZADlxYYRe3N4lXBUC\\n\
UJwzfdBhucviIAZcc1Zz6pz+dnNYIfdSWN6/PNoDa77CGVoUaKQTLb2PS+V1XnUC\\n\
AwEAAaNTMFEwHQYDVR0OBBYEFMv9fOOYKNghdAKKC+OJdoWfxz1cMB8GA1UdIwQY\\n\
MBaAFMv9fOOYKNghdAKKC+OJdoWfxz1cMA8GA1UdEwEB/wQFMAMBAf8wDQYJKoZI\\n\
hvcNAQELBQADggEBAAYymyYH4IWq3vVeLSnejf60mtMoZ32SAS9n13KgsoggSHJu\\n\
FKZT7Io1IY9BTukPaiquu6teGIR54GS4981UWWhVDePPh8HnLdTuI6Ci3ZGLhxn3\\n\
d/n8KoF+9ZX5BXIEKmboylkBkrZxevZBtXemcPy7R6TAUtm8vis/dEyx0JDrF4iK\\n\
XZ/WTMRX/lXdQ9TnUeSC74INfHAjt8/s5T0vvSlJ8Cb/slypJUzPLpjVQats6ul7\\n\
/Bxtj0zEfN+Q66KbDPQ8TK0wUDTwshGniQ/VQ0aq0Y2uw9+wKojSRlo/2DdEu/je\\n\
pqCwGg7XJcPUddNKReUB76ObhgzlBlBiq8wEOMw=\\n\
-----END CERTIFICATE-----\\n";

const char* clientCertificate = "-----BEGIN CERTIFICATE-----\\n\
DQYDVQQLDAZKYXJkaW4xJTAjBgkqhkiG9w0BCQEWFmphc29uZWNlbi5jb0Bn\\n\
bWFpbC5jb20wHhcNMjQxMDA1MTUzMjE0WhcNMjUxMDA1MTUzMjE0WjCBkzELMAkG\\n\
A1UEBhMCSUQxEzARBgNVBAgMCkphd2EgQmFyYXQxEDAOBgNVBAcMB0JhbmR1bmcx\\n\
JTAjBgNVBAoMHFBUIEplbWJhdGFuIERpZ2l0YWwgSW5vdmF0aWYxDzANBgNVBAsM\\n\
BkphcmRpbjElMCMGCSqGSIb3DQEJARYWamFzb25lY2VuLmNvQGdtYWlsLmNvbTCC\\n\
ASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAK+qqqshlAaGmxQ2UmxQZJxr\\n\
S8u6c1ZjCw8LEHxz50spPvgNGqcuVBTNE9BLM1mOorA/Y0bIFz2YjBnHAjK3sLxS\\n\
JZjvB/FbK2uZFRd8pJChDAqy/b9R5LuQW1J13Xt3CjdfPZkDrO2/aDj1Z9KPtgna\\n\
Zoe370SdTLf91XjHrbFTgtTmbXtM6HEt2nBmNQjo6o7jGzfuIgO5T90qonwuiNHW\\n\
lx4mYqQ5D50B5oQvyZw2yqiodJc+Zn1Fr+PNwfpd2tcnbkwMGMdc9vxiGgAJQ7Fe\\n\
nt1OTKwW5S8h03uOFG5BJlE5D/ACTrbzMqxkbIGj+2RooWeqles5/tn3ts/NKS0C\\n\
AwEAAaNCMEAwHQYDVR0OBBYEFGozt4wANAAZcmFLvG65psA5e6UcMB8GA1UdIwQY\\n\
MBaAFMv9fOOYKNghdAKKC+OJdoWfxz1cMA0GCSqGSIb3DQEBCwUAA4IBAQBjMzE3\\n\
JbYIp3zjPrfN2Oa882qAKrA41Tfrmv3/fYcUl0M5f9GsFUVWxrafGpwxIn6Azv/+\n\
yUrBlHmye4M1bSuEWx4nJYARGiGryckkA5/Mnc3fo3ctvqyTrZKuBrObK9oIIa9P\\n\
dsou8tmxl/nAfc/TsbmUReWvcmozVzYxWoO9wj3EYZQ6/343qX6x6eGBLAqzS1rE\\n\
okpmVicL/WJk1MeNGjpe0YGiPtUgPd1LWJ1jePGW9SB7eBzh12OejdnHDyeK5TAl\\n\
Bpw3aZIEfX9O8kwY1UpKXxNUZnzySgh2kzOoBQABcuCtwI5c2EkrtxSsDnPw+GO4\\n\
+6uZMO36VDtmyyCI\\n\
-----END CERTIFICATE-----\\n";

const char* clientPrivateKey = "-----BEGIN PRIVATE KEY-----\\n\
MIIEvwIBADANBgkqhkiG9w0BAQEFAASCBKkwggSlAgEAAoIBAQCvqqqrIZQGhpsU\\n\
NlJsUGSca0vLunNWYwsPCxB8c+dLKT74DRqnLlQUzRPQSzNZjqKwP2NGyBc9mIwZ\\n\
xwIyt7C8UiWY7wfxWytrmRUXfKSQoQwKsv2/UeS7kFtSdd17dwo3Xz2ZA6ztv2g4\\n\
9WfSj7YJ2maHt+9EnUy3/dV4x62xU4LU5m17TOhxLdpwZjUI6OqO4xs37iIDuU/d\\n\
KqJ8LojR1pceJmKkOQ+dAeaEL8mcNsqoqHSXPmZ9Ra/jzcH6XdrXJ25MDBjHXPb8\\n\
YhoACUOxXp7dTkysFuUvIdN7jhRuQSZROQ/wAk628zKsZGyBo/tkaKFnqpXrOf7Z\\n\
97bPzSktAgMBAAECggEAB2fDrZarCZJjl7jJhTjnYOOyDIrV4WKn/PCbt49gRP4e\\n\
YjEVOk5862qQxwNgjQozCAiIDBwTxLw5Tb3itRYizHGPFbEf7dgIzs6jgbu2qUUs\\n\
v4e3pDjU0mZdvy5qUZHdW6O3ckuiHtymjV4lDA6UXzp2f4qsusEB0rDP7rlGNHct\\n\
iOTMgUC8//mjFeAswofhjasElRN0bn7GURGl+NdEDF6iz2JR+L2htnKqAmFIMl7/\\n\
O4aD4l5VRMxnpGK3SbejubgFnFu7ktfrnBZxK5VJDpltNMj7L3rMJJL/U33hhjEF\\n\
AMhFXLe6UMFxhhpAi9OBdGoBdKL+S7XbkKyCER3gcQKBgQDuMyJivkfLDB9gBaYt\\n\
nyHkV+aWR0g60MFkPSYbfgVrrStRD99aKC0ukrRQ9fjUWqRZyhIcMvK+IduulCBT\\n\
j1sPR05DICA5LpVklW4TG9w4qn11w/tVQ9by6Fx8RC39e1/w7avPt3cVG54KCLnt\\n\
KvlAZu5Aq/kyb8Vvp85Ki7jpsQKBgQC8yz9Eh/LoGEA1YhXYC1HA7Dms+/MFEFcf\\n\
yOJyhALPBGfkaX2N9R859uzTnID8v11t1S6wVo/AQGMhaiU57nsZFhGFwlM+UBBu\\n\
YRxbZWVwCdKzQq4UEPzTOUCiwmroCEHcAyKtTrRo72Ao1/Ke1gxYozrHKDQ7jK0U\\n\
lDa+a/maPQKBgQCz7SwsKk9QsPeCMMI1895F/Z/QJEgLJWTvssD2Q1sU2tm9gZ/V\\n\
GkQGygoqnaI3xcAfkuPbKSDzPeATHxDMDZ1bDSGw0rOEfguazsU26fSPWTkrm75d\\n\
ycCz+5DgzR9wOaFe/Cir3om1CAd4zN6kp7FfVGDjuQOjdYpNrRPGe4RSAQKBgQCZ\\n\
kMl5uQuAdplj0tDp1us8/ek8KARZDh2QIRrxAyg1s+O+C4CxQ72OrKeeySKydAZP\\n\
HKTJkt+DBdychXaaVL2UfjrqjlKf0QhMgHlMboHATH0yiv6GG/tK5LsKqRHEg1Lu\\n\
3y4JwodFA9E358/WG04Xm05oSO+TsK3om1acNb/mEQKBgQCZrs3fjE/+I3oZZLfC\\n\
d0tIbNXImRGpJJ2F71vcrnk1ZCgL8IYghrbl/P3VQ5oXBSkYodCvXmeLg3r5f9Xg\\n\
HIp7nZ05TPqvG3MCBkdO5Y1XzNqe+l+98zXvGxmIcn8wJeurpO6oyn0lloI0+g0Y\\n\
1Ck+oSEQ5urClTUpG4amn4U+xg==\\n\
-----END PRIVATE KEY-----\\n";

int lastTime = 0;
int now = 0;
int sendTimePeriod = 5000; //Kirim data setiap 5 detik               

String received_id = "";
String received_type = "";
String received_command = "";
String received_digitalSignature = "";


void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) 
{
  switch(type) 
  {
    case WStype_DISCONNECTED:
      Serial.println("Disconnected from server");
      break;

    case WStype_CONNECTED:
      Serial.println("Connected to server");
      break;

    case WStype_TEXT:
      Serial.print("Received message: ");
      Serial.println((char *)payload);

      //if the send JSON is in this format
      //{"IdPerangkat": "PR001", "type" : "1", "command": "0001", "DigitalSignature":"",}
      //parse the JSON and input it to the received_data string
      

      break;
  }
}

void connectToWebSocket()
{
  // Mengatur sertifikat dan kunci untuk koneksi SSL
  wifiClient.setCACert(rootCACertificate);
  wifiClient.setCertificate(clientCertificate);
  wifiClient.setPrivateKey(clientPrivateKey);
  
  // Initialize the WebSocket connection with the server URL and port
  // webSocket.begin(websocketServerUrl, websocketPort, "/");
  webSocket.beginSSL(websocketServerUrl, websocketPort, "/");

  // Set the event handler for WebSocket events
  webSocket.onEvent(webSocketEvent);

  // Set the reconnect interval to 5000 milliseconds (5 seconds)
  webSocket.setReconnectInterval(5000);

}

void sendDataToServer()
{
  digitalWrite(csEthPin, HIGH);
  digitalWrite(csSdPin, LOW);

  //Baca recent data dari SD Card berdasarkan log dari SD Card

  // Create a JSON document
  DynamicJsonDocument doc(1024);
  doc["IdPerangkat"] = mac_address;
  doc["type"] = 1;
  doc["volume"] = lastVolume;
  doc["flowRate"] = lastFlowRate;
  doc["current"] = lastCurrent;
  doc["voltage"] = lastVoltage;
  doc["pf"] = lastPf;
  doc["frequency"] = lastFrequency;
  doc["power"] = lastPower;
  doc["SA_state"] = SA_state;
  doc["SSR_state"] = SSR_state;
  doc["energy"] = lastEnergy/1000; //dalam kWh

  // Serialize JSON to string
  String output;
  serializeJson(doc, output);

  // Send data to server
  webSocket.sendTXT(output);
  //Serial.println("Data sent to server: " + output);

  digitalWrite(csSdPin, HIGH);
  digitalWrite(csEthPin, LOW);

}

//Bagian Task RTOS

//Task1 untuk mengumpulkan data flowrate air dan listrik
void collectData(void * parameter)
{
  for (;;)
  {
    //Bagian flowmeter
    //Menghitung waktu sekarang
    currentTime = millis();
    if (currentTime - previousTime >= 1000) //Delay 1 detik dari previousTime (frekuensi = banyak pulse per detik)
    {
      collectFlowMeterData();

      collectKwhMeterData();
      
      collectRTCData();

      //Serial.println("");
    }
    
    //Bagian LCD
    lcdTime2 = millis();
    if (lcdTime2 - lcdTime1 >= SwitchStageTime)
    {
      lcdDisplayData();
      lcdTime1 = lcdTime2;
    }
  }
  vTaskDelay(10 / portTICK_PERIOD_MS); // delay for 10 ms
}

//Task2 untuk mengirim data ke server dan mengecek perintah dari server
void sendData(void * parameter)
{
  for (;;)
  {
    timecount2 = millis();
    if (timecount2 - timecount1 >= saveDataPeriod)
    {
      //Saat SD Card aktif cari data terbaru
      findLatestDataFile();
      saveLatestData();

      saveDatainSD();
      // toogleActuator();
      timecount1 = timecount2;

    }

    //Bagian Ethernet
    //Jika ETH mati segera nyalakan
    if (csEthPin == HIGH)
    {
      //connectToETH();
    }

    else
    {
      //Jika ETH sudah aktif, cek koneksi ke server
      webSocket.loop();
      //Kirim setiap 5 detik
      
      now = millis();
      if (now - lastTime >= sendTimePeriod)
      {
        sendDataToServer();
        lastTime = now;
      }
    }

  }
  vTaskDelay(10/ portTICK_PERIOD_MS); // delay for 10 ms
}

//Bagian Setup AP pertama kali
const char *ssid_server = "ESP32-AP";
const char *ssid_password = "ESP32!AP";

AsyncWebServer server(80);

// HTML untuk halaman pengaturan WiFi
const char* htmlHomePage PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>ESP32 WiFi Setup</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
</head>
<body>
    <h2>ESP32 WiFi Setup</h2>
    <form action="/connect" method="POST">
        SSID: <input type="text" name="ssid"><br>
        Password: <input type="password" name="password"><br>
        <input type="submit" value="Connect">
    </form>
</body>
</html>
)rawliteral";



void setup()
{
  Serial.begin(115200);

  //Bagian LCD
  lcd.init();
  lcd.begin(lcdColumns, lcdRows);
  lcd.backlight();
  lcd.clear();
  
  WiFi.softAP(ssid_server, ssid_password);
  Serial.println("WiFi AP setup complete.");
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  lcd.setCursor(0,0);
  lcd.print("Waiting for WiFi Setup...");

  // Menangani permintaan halaman utama dengan interrupt
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
  {
      request->send_P(200, "text/html", htmlHomePage);
  });

  // Menangani permintaan untuk menghubungkan ke WiFi lain dengan interrupt
  server.on("/connect", HTTP_POST, [](AsyncWebServerRequest *request)
  {
      if (request->hasParam("ssid", true)) 
      {
          ssid = request->getParam("ssid", true)->value();
      }

      if (request->hasParam("password", true)) 
      {
          password = request->getParam("password", true)->value();
      }

      Serial.println("Received WiFi credentials:");
      Serial.println("SSID: " + ssid);
      Serial.println("Password: " + password);

      // Menanggapi permintaan lebih dulu sebelum memutus koneksi
      request->send(200, "text/plain", "Connecting to WiFi. Wait 4 seconds, if failed connects again");

      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Connecting to WiFi...");

      WiFi.disconnect(); // Memutus koneksi ESP32 sebagai AP
      WiFi.begin(ssid.c_str(), password.c_str());

      int timeout = 4;
      while (WiFi.status() != WL_CONNECTED && timeout > 0) 
      {
          delay(1000);
          Serial.print(".");
          timeout--;
      }

      if (WiFi.status() == WL_CONNECTED)
      {
          Serial.println("\nSuccessfully connected to WiFi.");

          Serial.print("ESP32 IP Address: ");
          Serial.println(WiFi.localIP());
          // Mematikan Web Server setelah berhasil terkoneksi
          
          delay(500);
          server.end();
          delay(500);
          
          WiFi.softAPdisconnect(true);
          WiFi.mode(WIFI_STA);

          
          lcd.clear();
          lcd.setCursor(0,0);
          lcd.print("WiFi Connected");
          lcd.setCursor(0,1);
          lcd.print("IP: ");
          lcd.print(WiFi.localIP());

          //Bagian flowmeter

          //set sebagai pin dengan input pullup (tanpa input Vout = 3.3 V DC)
          pinMode(flowMeterPin, INPUT_PULLUP); 

          //Jika terjadi falling edge pada pin flowMeterPin, maka akan memanggil fungsi pulseCounter
          attachInterrupt(digitalPinToInterrupt(flowMeterPin), pulseCounter, FALLING);

          //Hitung waktu inisiasi sebagai previousTime
          previousTime = millis();

          //Bagian PZEM kwhmeter
          Serial2.begin(9600, SERIAL_8N1, rxPin, txPin);

          //Bagian Sambung ke Internet

          //Bagian Aktoator
          pinMode(SVPin, OUTPUT);
          pinMode(SSRPin, OUTPUT);
          digitalWrite(SVPin, HIGH);
          digitalWrite(SSRPin, HIGH);
          aktuator_state = true;
        
          //Bagian SD card
          pinMode(csSdPin, OUTPUT);
          digitalWrite(csSdPin, HIGH);  
          
          //Bagian RTC
          configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

          lcd.clear();
          lcd.setCursor(0,0);
          lcd.print("Connecting RTC...");
          while (!getLocalTime(&timeinfo, 50000)) 
          {
            Serial.print("Failed to obtain time ");
            Serial.println(ntpServer);
            delay(1000);
          }
          
          //Bagian Web Sockets
          lcd.clear();
          lcd.setCursor(0,0);
          lcd.print("Connecting Server...");
          // connectToWebSocket();

          energy_volume_intial();

          timecount1 = millis();
          lcdTime1 = millis();
          lastTime = millis();
          
          //Bagian RTOS (Kerja Paralel)
          xTaskCreatePinnedToCore
          (
            collectData,
            "collectData",
            10000,
            NULL,
            1,
            NULL,
            0
          );

          xTaskCreatePinnedToCore
          (
            sendData,
            "sendData",
            10000,
            NULL,
            1,
            NULL,
            1
          );
          
      }
      else
      {
          Serial.println("\nFailed to connect to WiFi. Back to main IP");
          request->send(200, "text/plain", "Failed to connect to WiFi. Back to main IP");
          WiFi.disconnect();
          WiFi.softAP(ssid_server, ssid_password); // Tetap jalankan ESP32 AP
      }
  });

  server.begin();

}

void loop()
{

}


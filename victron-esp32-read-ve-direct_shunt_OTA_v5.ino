/*
    BattMan - Solar Battery Monitoring Analytics - Victron VE.DIRECT
    Maker:  Dan Pancamo
    Updates
        V5.1 20230116 - OTA
        V5.2 20230116 - Disconnect reason FIx
        V5.3 20230118 Added ERR
*/

String Version = "BattMan OTA 5.3";                       // Version 
String BoardId = "BattMan.victron.HQ2212CHJG2.shunt";     //ESP32 - Victron Shunt
//String BoardId = "BattMan.victron.HQ2212CHJG2";         //ESP32 - Victron MPPT 100/30 

/*
  PID  0xA043      -- Product ID for BlueSolar MPPT 100/15
  FW  119     -- Firmware version of controller, v1.19
  SER#  HQXXXXXXXXX   -- Serial number
  V 13790     -- Battery voltage, mV
  I -10     -- Battery current, mA
  VPV 15950     -- Panel voltage, mV
  PPV 0     -- Panel power, W
  CS  5     -- Charge state, 0 to 9
  ERR 0     -- Error code, 0 to 119
  LOAD  ON      -- Load output state, ON/OFF
  IL  0     -- Load current, mA
  H19 0       -- Yield total, kWh
  H20 0     -- Yield today, kWh
  H21 397     -- Maximum power today, W
  H22 0       -- Yield yesterday, kWh
  H23 0     -- Maximum power yesterday, W
  HSDS  0     -- Day sequence number, 0 to 365
  Checksum  l:A0002000148   -- Message checksum
*/


#include "HardwareSerial.h"
#include <Adafruit_Sensor.h>
#include "DHTesp.h"

#include <WiFi.h>
#include <WiFiUDP.h>
#include <map>
#include <algorithm>


String line;
//WIFI

const char* ssid     = "168DST24";
const char* password = "olivia15";

//UDP
int port = 8089;
const char *influxDNS = "bi.pancamo.com";
IPAddress influxIP;
WiFiUDP udp;



int sampleCnt = 0;

//SERIAL ON RX2
HardwareSerial serialVE(2); // VE.Direct port is connected to UART1


//OTA
#include <ElegantOTA.h> 
#include <WiFiClient.h>
#include <WebServer.h>
WebServer webserver(80);

//WIFIMAN
uint8_t DisconnectReason=0;
unsigned long wifiUptime = millis();
unsigned long wifiDowntime = millis();


//DSP11
DHTesp dht;
#define DHT11_PIN 27

//WIFIMAN

void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info){
  Serial.println("Connected to AP successfully!");
}

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info){
  Serial.println("WiFi connected OTA");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("Version: " + Version);

  if (WiFi.hostByName(influxDNS, influxIP)) {
      Serial.print("Influx IP: ");
      Serial.println(influxIP);
    } else {
      Serial.println("DNS lookup failed for " + String(influxDNS));
    }
  
  wifiDowntime=millis();
  
  line = String(BoardId + ".wifi.disreason value=" + String(DisconnectReason));
  toInflux(line);  

}

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info){
  Serial.println("Disconnected from WiFi access point");
  Serial.print("WiFi lost connection. Reason: ");
  DisconnectReason = info.wifi_sta_disconnected.reason;

  wifiDowntime=millis();

  Serial.println(info.wifi_sta_disconnected.reason);
  Serial.println("Trying to Reconnect");
  WiFi.begin(ssid, password);
}


void wifiSetup()
{

  // WIFI RECONNECT
  WiFi.disconnect(true);
  wifiUptime=millis();
  wifiDowntime=millis();
  
  delay(1000);

  WiFi.onEvent(WiFiStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(WiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);


  WiFi.begin(ssid, password);
    
  Serial.println();
  Serial.println();
  Serial.println("Wait for WiFi... ");
  
}

void wifiStatus()
{
  line = String(BoardId + ".wifi.rssi value=" + String(WiFi.RSSI()));
  toInflux(line);

  wifiUptime=millis()-wifiDowntime; 
  line = String(BoardId + ".wifi.uptime value=" + String(wifiUptime/1000));
  toInflux(line);

}

//WIFI REONNECT END-------------------------

//OTA 
void handle_OnConnect() 
{
    webserver.send(200, "text/plain", "Hello from " + Version + " " + BoardId);
}

void otaSetup()
{
  webserver.on("/", handle_OnConnect);
  ElegantOTA.begin(&webserver);    // Start ElegantOTA
  webserver.begin();
}


//BATMAN

//Normalize Data, remove the bad data with 5 samples
std::map<String, int> data[5];
// Function prototype
int medianData(std::map<String, int> (&data)[5], String code);

int  medianData(std::map<String, int> (&data)[5], String code) {
  // Access the values of the map array
  int numbers[5];

  for (int i = 0; i < 5; i++) {
    numbers[i] = data[i][code];
  }
  std::sort(numbers, numbers + 5);
  return (numbers[2]);
}




void setup() 
{

  Serial.begin(9600);
  serialVE.begin(19200); // Set the baud rate for the VE.Direct port

  //DHT11 SETUP
    dht.setup(DHT11_PIN, DHTesp::DHT11);

  //WIFI SETUP  
      wifiSetup();
      otaSetup();
}


void toInflux (String line)
{

      Serial.println(line);

      udp.beginPacket(influxIP, port);
      udp.print(line);
      udp.endPacket();

}

void tempToInflux()
{

      //DHT11
      float temperature = dht.getTemperature()  * 1.8 + 32 ;
      float humidity = dht.getHumidity();
      //Serial.printf("Temp = %2.2f\n", temperature);   

      line = String(BoardId + ".battery.temperature value=" + String(temperature));
      toInflux(line);
      line = String(BoardId + ".battery.humidity value=" + String(humidity));
      toInflux(line);

}

void processCC()
{

      tempToInflux();

      int value = medianData(data, "V");      
      line = String(BoardId +".battery.voltage value=" + String(value/1000.0));
      toInflux(line);

      value = medianData(data, "I");
      line = String(BoardId +".battery.current value=" + String(value/1000.0));
      toInflux(line);

      value = medianData(data, "VPV");
      line = String(BoardId +".solar.voltage value=" + String(value/1000.0));
      toInflux(line);

      value = medianData(data, "PPV");
      line = String(BoardId +".solar.power value=" + String(value));
      toInflux(line);

      value = medianData(data, "H20");
      line = String(BoardId +".yieldtoday value=" + String(value));
      toInflux(line);
      
      value = medianData(data, "H21");
      line = String(BoardId +".maxpowertoday value=" + String(value));
      toInflux(line);

      value = medianData(data, "CS");
      line = String(BoardId +".chargestate value=" + String(value));
      toInflux(line);
      
      value = medianData(data, "ERR");
      line = String(BoardId +".errorcode value=" + String(value));
      toInflux(line);


}

void processShunt()
{
  
      int value = medianData(data, "V");      
      String line = String(BoardId +".battery.voltage value=" + String(value/1000.0));
      toInflux(line);

      value = medianData(data, "I");
      line = String(BoardId +".battery.current value=" + String(value/1000.0));
      toInflux(line);

      value = medianData(data, "P");
      line = String(BoardId +".P value=" + String(value));
      toInflux(line);

      value = medianData(data, "SOC");
      line = String(BoardId +".SOC value=" + String(value/10.0));
      toInflux(line);


      value = medianData(data, "CE");
      line = String(BoardId +".CE value=" + String(value));
      toInflux(line);

      value = medianData(data, "AR");
      line = String(BoardId +".AR value=" + String(value));
      toInflux(line);

      value = medianData(data, "MON");
      line = String(BoardId +".MON value=" + String(value));
      toInflux(line);


      value = medianData(data, "TTG");
      line = String(BoardId +".TTG value=" + String(value));
      toInflux(line);


      value = medianData(data, "H1");
      line = String(BoardId +".H1 value=" + String(value));
      toInflux(line);

      value = medianData(data, "H2");
      line = String(BoardId +".H2 value=" + String(value));
      toInflux(line);

      value = medianData(data, "H4");
      line = String(BoardId +".H4 value=" + String(value));
      toInflux(line);

      value = medianData(data, "H5");
      line = String(BoardId +".H5 value=" + String(value));
      toInflux(line);

      
      value = medianData(data, "H6");
      line = String(BoardId +".H6 value=" + String(value));
      toInflux(line);

          
      value = medianData(data, "H7");
      line = String(BoardId +".H7 value=" + String(value));
      toInflux(line);

      value = medianData(data, "H8");
      line = String(BoardId +".H8 value=" + String(value));
      toInflux(line);


      value = medianData(data, "H9");
      line = String(BoardId +".H9 value=" + String(value));
      toInflux(line);

      

      value = medianData(data, "H10");
      line = String(BoardId + ".H10 value=" + String(value));
      toInflux(line);
      
      value = medianData(data, "H11");
      line = String(BoardId +".H11 value=" + String(value));
      toInflux(line);
      
      value = medianData(data, "H12");
      line = String(BoardId +".H12 value=" + String(value));
      toInflux(line);

      value = medianData(data, "H15");
      line = String(BoardId +".H15 value=" + String(value));
      toInflux(line);
            

      value = medianData(data, "H16");
      line = String(BoardId +".H16 value=" + String(value));
      toInflux(line);

      value = medianData(data, "H17");
      line = String(BoardId +".H17 value=" + String(value));
      toInflux(line);
    
      value = medianData(data, "H18");
      line = String(BoardId +".H18 value=" + String(value));
      toInflux(line);
}

void loop() {
  // Read a line of text from the VE.Direct port

  
  webserver.handleClient();


  String inputString = serialVE.readStringUntil('\n');
  // Print the line to the serial monitor
  // Serial.println(inputString);

  int spaceIndex = inputString.indexOf('\t');

  String key = inputString.substring(0, spaceIndex);
  String svalue = inputString.substring(spaceIndex + 1);


    if (key == "Checksum")
    {  
      sampleCnt++;
      delay(1000);
    }
 
    if (sampleCnt <= 4)
    {
      data[sampleCnt][key] = svalue.toInt();
      Serial.println("Cnt=" + String(sampleCnt) + "  key: " + key + " Value=" + svalue );
    }
    else
    {
      wifiStatus();
      if (BoardId == "BattMan.victron.HQ2212CHJG2")
      {
        processCC();
      }
      else
      {
        processShunt();
      }
      sampleCnt=0;
    }      

}




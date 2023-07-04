/*
  noise_temp_meter.ino
  www.weigu.lu
  for UDP, listen on Linux PC (UDP_LOG_PC_IP) with netcat command:
  nc -kulw 0 6464
  more infos: www.weigu.lu/microcontroller/esptoolbox/index.html
  more infos: www.weigu.lu/microcontroller/noise_meter/index.html
  ---------------------------------------------------------------------------
  Copyright (C) 2023 Guy WEILER www.weigu.lu

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
  ---------------------------------------------------------------------------

  ESP8266: LOLIN/WEMOS D1 mini pro
  ESP32:   MH ET LIVE ESP32-MINI-KIT

  MHET    | MHET    - LOLIN        |---| LOLIN      - MHET    | MHET

  GND     | RST     - RST          |---| TxD        - RxD(3)  | GND
   NC     | SVP(36) -  A0          |---| RxD        - TxD(1)  | 27
  SVN(39) | 26      -  D0(16)      |---|  D1(5,SCL) -  22     | 25
   35     | 18      -  D5(14,SCK)  |---|  D2(4,SDA) -  21     | 32
   33     | 19      -  D6(12,MISO) |---|  D3(0)     -  17     | TDI(12)
   34     | 23      -  D7(13,MOSI) |---|  D4(2,LED) -  16     | 4
  TMS(14) | 5       -  D8(15,SS)   |---| GND        - GND     | 0
   NC     | 3V3     - 3V3          |---|  5V        -  5V     | 2
  SD2(9)  | TCK(13)                |---|              TD0(15) | SD1(8)
  CMD(11) | SD3(10)                |---|              SD0(7)  | CLK(6)
*/

/*!!!!!!       Make your changes in config.h (or secrets_xxx.h)      !!!!!!*/

/*------ Comment or uncomment the following line suiting your needs -------*/
//#define USE_SECRETS
#define OTA               // if Over The Air update needed (security risk!)
//#define MQTTPASSWORD    // if you want an MQTT connection with password (recommended!!)
#define STATIC            // if static IP needed (no DHCP)
#define BME280_I2C

/****** Arduino libraries needed ******/
#include "ESPToolbox.h"            // ESP helper lib (more on weigu.lu)
#ifdef USE_SECRETS
  // The file "secrets_xxx.h" has to be placed in a sketchbook libraries
  // folder. Create a folder named "Secrets" in sketchbook/libraries and copy
  // the config.h file there. Rename it to secrets_xxx.h
  #include <secrets_noise_temp.h> // things you need to change are here or
#else
  #include "config.h"              // things you need to change are here
#endif // USE_SECRETS
#include <PubSubClient.h>          // for MQTT
#include <ArduinoJson.h>           // convert MQTT messages to JSON
#ifdef BME280_I2C
  #include <Wire.h>                // BME280 on I2C (PU-resistors!)
  #include <BME280I2C.h>
#endif // ifdef BME280_I2C

/****** WiFi and network settings ******/
const char *WIFI_SSID = MY_WIFI_SSID;           // if no secrets, use config.h
const char *WIFI_PASSWORD = MY_WIFI_PASSWORD;   // if no secrets, use config.h
#ifdef STATIC
  IPAddress NET_LOCAL_IP (NET_LOCAL_IP_BYTES);  // 3x optional for static IP
  IPAddress NET_GATEWAY (NET_GATEWAY_BYTES);    // look in config.h
  IPAddress NET_MASK (NET_MASK_BYTES);
  IPAddress NET_DNS (NET_DNS_BYTES);
#endif // ifdef STATIC
#ifdef OTA                                      // Over The Air update settings
  const char *OTA_NAME = MY_OTA_NAME;
  const char *OTA_PASS_HASH = MY_OTA_PASS_HASH; // use the config.h file
#endif // ifdef OTA

IPAddress UDP_LOG_PC_IP(UDP_LOG_PC_IP_BYTES);   // UDP log if enabled in setup

/****** MQTT settings ******/
const short MQTT_PORT = MY_MQTT_PORT;
WiFiClient espClient;
PubSubClient MQTT_Client(espClient);
#ifdef MQTTPASSWORD
  const char *MQTT_USER = MY_MQTT_USER;
  const char *MQTT_PASS = MY_MQTT_PASS;
#endif // MQTTPASSWORD

/******* BME280 ******/
float temp(NAN), hum(NAN), pres(NAN);
#ifdef BME280_I2C
  BME280I2C bme;    // Default : forced mode, standby time = 1000 ms
                    // Oversampling = press. ×1, temp. ×1, hum. ×1, filter off
#endif // ifdef BME280_I2C

long rand_number = 0;
const unsigned int READ_SAMPLES = 1000;
const unsigned int AVG_ARRAY_SAMPLES = 30; 
unsigned int value = 0;
unsigned int max_value = 0;
unsigned int array_counter = 0;
unsigned int avg_array[AVG_ARRAY_SAMPLES]; 

ESPToolbox Tb;                                // Create an ESPToolbox Object

/****** SETUP *************************************************************/

void setup() {
  Tb.set_udp_log(true, UDP_LOG_PC_IP, UDP_LOG_PORT);
  Tb.set_led_log(true); // enable LED logging (pos logic)
  #ifdef STATIC
    Tb.set_static_ip(true,NET_LOCAL_IP, NET_GATEWAY, NET_MASK, NET_DNS);
  #endif // ifdef STATIC
  Tb.init_wifi_sta(WIFI_SSID, WIFI_PASSWORD, NET_MDNSNAME, NET_HOSTNAME);
  Tb.init_ntp_time();
  MQTT_Client.setBufferSize(MQTT_MAXIMUM_PACKET_SIZE);
  MQTT_Client.setServer(MQTT_SERVER,MQTT_PORT); //open connection MQTT server
  #ifdef OTA
    Tb.init_ota(OTA_NAME, OTA_PASS_HASH);
  #endif // ifdef OTA
  #ifdef BME280_I2C
    init_bme280();
  #endif // ifdef BME280_I2C
  randomSeed(analogRead(0));
  Tb.blink_led_x_times(3);
  Tb.log_ln("Setup done!");
}

/****** LOOP **************************************************************/

void loop() {
  #ifdef OTA
    ArduinoOTA.handle();
  #endif // ifdef OTA
  if (Tb.non_blocking_delay(PUBLISH_TIME)) { // PUBLISH_TIME in config.h
    mqtt_get_temp_and_noise_and_publish();
    Tb.blink_led_x_times(3);
    delay(1000);
  }
  if (WiFi.status() != WL_CONNECTED) {   // if WiFi disconnected, reconnect
    Tb.init_wifi_sta(WIFI_SSID, WIFI_PASSWORD, NET_MDNSNAME, NET_HOSTNAME);
  }
  if (!MQTT_Client.connected()) {        // reconnect mqtt client, if needed
    mqtt_connect();
  }
  MQTT_Client.loop();                    // make the MQTT live
  //delay(10); // needed for the watchdog! (alt. yield())
  // get noise  
  array_counter++;   
  if (array_counter == AVG_ARRAY_SAMPLES) {
    array_counter = 0;
  }  
  //long mil1 = millis();
  for (unsigned int i=0; i<READ_SAMPLES; i++) { // takes about 100ms with 1000 samples
    value = analogRead(A0);    
    if (value > max_value) {
      max_value = value;
    }    
  }
  //long mil2 = millis();  
  //Tb.log("Time: " + String(mil2-mil1)+ "\t     ");  
  //Tb.log_ln("ADC_max: " + (String(max_value)));  
  avg_array[array_counter] = max_value;  
  max_value = 0; 
}

/********** MQTT functions ***************************************************/

// connect to MQTT server
void mqtt_connect() {
  while (!MQTT_Client.connected()) { // Loop until we're reconnected
    Tb.log("Attempting MQTT connection...");
    #ifdef MQTTPASSWORD
      if (MQTT_Client.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
    #else
      if (MQTT_Client.connect(MQTT_CLIENT_ID)) { // Attempt to connect
    #endif // ifdef MQTTPASSWORD
      Tb.log_ln("MQTT connected");
      MQTT_Client.subscribe(MQTT_TOPIC_IN.c_str());
    }
    else {
      Tb.log("MQTT connection failed, rc=");
      Tb.log(String(MQTT_Client.state()));
      Tb.log_ln(" try again in 5 seconds");
      delay(5000);  // Wait 5 seconds before retrying
    }
  }
}

// MQTT get the time, relay flags ant temperature an publish the data
void mqtt_get_temp_and_noise_and_publish() {
  unsigned int avg_value = 0;
  DynamicJsonDocument doc_out(1024);
  String mqtt_msg, we_msg;
  Tb.get_time();
  doc_out["datetime"] = Tb.t.datetime;  
  avg_value = calculate_noise();
  doc_out["noise_adc"] = avg_value;  
  doc_out["noise_dB"] = calculate_noise_dB(avg_value);
  #ifdef BME280_I2C
    get_data_bme280();
    doc_out["temperature_C"] = (int)(temp*10.0 + 0.5)/10.0;
    doc_out["humidity_%"] = (int)(hum*10.0 + 0.5)/10.0;
    doc_out["pressure_hPa"] = (int)((pres + 5)/10)/10.0;
  #endif // ifdef BME280_I2C
  mqtt_msg = "";
  serializeJson(doc_out, mqtt_msg);
  MQTT_Client.publish(MQTT_TOPIC_OUT.c_str(),mqtt_msg.c_str());
  //Tb.log("MQTT published at ");
  //Tb.log(Tb.t.time);
  //Tb.log_ln(mqtt_msg);
}

/********** BME280 functions *************************************************/

// initialise the BME280 sensor
#ifdef BME280_I2C
  void init_bme280() {
    Wire.begin();
    while(!bme.begin()) {
      Tb.log_ln("Could not find BME280 sensor!");
      delay(1000);
    }
    switch(bme.chipModel())  {
       case BME280::ChipModel_BME280:
         Tb.log_ln("Found BME280 sensor! Success.");
         break;
       case BME280::ChipModel_BMP280:
         Tb.log_ln("Found BMP280 sensor! No Humidity available.");
         break;
       default:
         Tb.log_ln("Found UNKNOWN sensor! Error!");
    }
  }

// get BME280 data and log it
void get_data_bme280() {
  BME280::TempUnit tempUnit(BME280::TempUnit_Celsius);
  BME280::PresUnit presUnit(BME280::PresUnit_Pa);
  bme.read(pres, temp, hum, tempUnit, presUnit);
  //Tb.log_ln("Temp: " + (String(temp)) + " Hum: " + (String(hum)) +
  //         " Pres: " + (String(pres)));
}
#endif // ifdef BME280_I2C

/********** NOISE functions *************************************************/
unsigned int calculate_noise() {  
  unsigned long long avg_value = 0;
  for (unsigned int i=0; i<AVG_ARRAY_SAMPLES; i++) {    
    avg_value += avg_array[i];
    Tb.log(String(avg_array[i]) + "\t");
  }
  avg_value = avg_value/AVG_ARRAY_SAMPLES;
  Tb.log("\nADC: " + String(avg_value) + "\t");
  return avg_value;  
}

unsigned int calculate_noise_dB(unsigned int avg_value) {
  double value_db = 0;
  if (avg_value < 100) {
    value_db = map(avg_value,20,100,40,67); 
  }
  else if ((avg_value >= 100) && (avg_value <= 400)) {
    value_db = map(avg_value,100,400,67,82); 
  }
  else if (avg_value > 400) {
    value_db = map(avg_value,400,900,82,115); 
  }
  value_db = round(value_db*100)/100;  
  Tb.log_ln("  dB: " + String(value_db));
  return value_db;  
}
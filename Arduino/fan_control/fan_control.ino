/*
  fan_control.ino
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
#define USE_SECRETS
#define OTA               // if Over The Air update needed (security risk!)
//#define MQTTPASSWORD    // if you want an MQTT connection with password (recommended!!)
#define STATIC            // if static IP needed (no DHCP)
//#define BME280_I2C
#include <ArduinoJson.h>  // convert MQTT messages to JSON

/****** Arduino libraries needed ******/
#include "ESPToolbox.h"            // ESP helper lib (more on weigu.lu)
#ifdef USE_SECRETS
  // The file "secrets_xxx.h" has to be placed in a sketchbook libraries
  // folder. Create a folder named "Secrets" in sketchbook/libraries and copy
  // the config.h file there. Rename it to secrets_xxx.h
  #include <secrets_noctua_fan_control.h> // things you need to change are here or
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


const byte PINS_PWM[] = {16, 17, 21};
const byte PWM_CHANNELS[] = {0, 1, 2};
const byte PINS_SPEED[] = {23, 19, 18};

const byte nr_of_fans = sizeof(PINS_PWM) / sizeof(PINS_PWM[0]);
volatile unsigned int fan_speeds[nr_of_fans] = {0};

const unsigned int PWM_FREQ = 25000;   // freq limits depend on resolution
const byte PWM_RES = 8;                //resolution 1-16 bits

byte duty_cycle = 0;
int dc_current = 0;

ESPToolbox Tb;                                // Create an ESPToolbox Object

/****** SETUP *************************************************************/

void setup() {   
  delay(10000);  
  Tb.set_led_log(true); // enable LED logging (pos logic)  
  Tb.set_udp_log(true, UDP_LOG_PC_IP, UDP_LOG_PORT);  
  #ifdef STATIC
    Tb.set_static_ip(true,NET_LOCAL_IP, NET_GATEWAY, NET_MASK, NET_DNS);
  #endif // ifdef STATIC
  Tb.init_wifi_sta(WIFI_SSID, WIFI_PASSWORD, NET_MDNSNAME, NET_HOSTNAME);
  Tb.init_ntp_time();  
  MQTT_Client.setServer(MQTT_SERVER,MQTT_PORT); //open connection MQTT server
  MQTT_Client.setCallback(mqtt_callback);
  mqtt_connect();
  MQTT_Client.setBufferSize(MQTT_MAXIMUM_PACKET_SIZE);
  #ifdef OTA
    Tb.init_ota(OTA_NAME, OTA_PASS_HASH);
  #endif // ifdef OTA
  #ifdef BME280_I2C
    init_bme280();
  #endif // ifdef BME280_I2C  
  init_pins();
  Tb.blink_led_x_times(3);
  Tb.log_ln("Setup done!");
}

/****** LOOP **************************************************************/

void loop() {
  #ifdef OTA
    ArduinoOTA.handle();
  #endif // ifdef OTA
  if (Tb.non_blocking_delay(5000)) { // every 5s
    for (byte i=0; i<nr_of_fans; i++) {
      fan_speeds[i] = fan_speeds[i]*6; // calculate fan speed in rpm (2 imp. per revolution!)
    }    
    if (dc_current > 255) { 
       duty_cycle = 255;
    }   
    else {
      duty_cycle = dc_current;
    }
    for (byte i=0; i<nr_of_fans; i++) {
      ledcWrite(PWM_CHANNELS[i], duty_cycle); //150 = 1340 (no noise) RPM; 200 = 1700RPM; 255 = 2000RPM
    }
    mqtt_publish();
    Tb.log_ln(String(duty_cycle) + "\t" + String(fan_speeds[0])+ "\t" +
              String(fan_speeds[1])+ "\t" + String(fan_speeds[2]));    
    Tb.blink_led_x_times(1);    
    for (byte i=0; i<nr_of_fans; i++) {
      fan_speeds[i] = 0;  // reset fan speed counter
    }  
  }  
  if (WiFi.status() != WL_CONNECTED) {   // if WiFi disconnected, reconnect
    Tb.init_wifi_sta(WIFI_SSID, WIFI_PASSWORD, NET_MDNSNAME, NET_HOSTNAME);
  }
  if (!MQTT_Client.connected()) {        // reconnect mqtt client, if needed
    mqtt_connect();
  }
  MQTT_Client.loop();                    // make the MQTT live
  delay(10); // needed for the watchdog! (alt. yield())
}


/********** INIT and INTERRUPT functions ***********************************/

void IRAM_ATTR isr_speed_0() {
  fan_speeds[0]++;
}

void IRAM_ATTR isr_speed_1() {
  fan_speeds[1]++;
}

void IRAM_ATTR isr_speed_2() {
  fan_speeds[2]++;
}

void init_pins() {
  for (byte i=0; i<nr_of_fans; i++) {
    ledcAttachPin(PINS_PWM[i], PWM_CHANNELS[i]); // assign pin to channel
    ledcSetup(PWM_CHANNELS[i], PWM_FREQ, PWM_RES);    
    pinMode(PINS_SPEED[i], INPUT_PULLUP);
  }  
  attachInterrupt(PINS_SPEED[0], isr_speed_0, FALLING);
  attachInterrupt(PINS_SPEED[1], isr_speed_1, FALLING);
  attachInterrupt(PINS_SPEED[2], isr_speed_2, FALLING);
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
void mqtt_publish() {  
  DynamicJsonDocument doc_out(1024);
  String mqtt_msg, we_msg;
  Tb.get_time();
  doc_out["datetime"] = Tb.t.datetime;    
  #ifdef BME280_I2C
    get_data_bme280();
    doc_out["temperature_C"] = (int)(temp*10.0 + 0.5)/10.0;
    doc_out["humidity_%"] = (int)(hum*10.0 + 0.5)/10.0;
    doc_out["pressure_hPa"] = (int)((pres + 5)/10)/10.0;
  #endif // ifdef BME280_I2C
  for (byte i=0; i<nr_of_fans; i++) {
    doc_out["fan_speeds_rpm"][i] = fan_speeds[i];
  }  
  doc_out["dc_current"] = dc_current;
  serializeJson(doc_out, mqtt_msg);
  MQTT_Client.publish(MQTT_TOPIC_OUT.c_str(),mqtt_msg.c_str());
  //Tb.log("MQTT published at ");
  //Tb.log(Tb.t.time);
  //Tb.log_ln(mqtt_msg);
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {  
  DynamicJsonDocument doc_in(1024);
  deserializeJson(doc_in, (byte*)payload, length);   //parse MQTT message 
  if (String(topic) == MQTT_TOPIC_IN) {      
      dc_current = abs(int(doc_in["value"]));
      //Tb.log_ln(String(dc_current));
  }
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


#define VERSION "2019-11-28"

//----------------------------------------------------------------------------------------
//
//	MAXLAMP Firmware
//						
//		Target MCU: DOIT ESP32 DEVKIT V1
//		Copyright:	2019 Michael Egger, me@anyma.ch
//		License: 	This is FREE software (as in free speech, not necessarily free beer)
//					published under gnu GPL v.3
//
// !!!!!!!!!!!!!!!! 
// !!!!    Input functionality requires replacing the esp32-hal-uart.c
// !!!!  	files in Arduino/hardware/espressif/esp32/cores/esp32/
// !!!!!!!!!!!!!!!! 
//
//----------------------------------------------------------------------------------------


#include <Preferences.h>
#include "Timer.h"
#include "Password.h"
#include "WiFi.h"
#include <ESPmDNS.h>
#include <SPIFFS.h>
#include "ESPAsyncWebServer.h"
#include <ESP32Encoder.h>

//========================================================================================
//----------------------------------------------------------------------------------------
//																				GLOBALS
Preferences                             preferences;
Timer									t;

const int PIN_LED = 23; 
const int PIN_BTN = 26; 
const int PIN_ENCA = 25; 
const int PIN_ENCB = 33; 

// setting PWM properties
const int freq = 5000;
const int ledChannel = 0;
const int resolution = 16;

ESP32Encoder        encoder;

long            	last_time_brightness_set;
boolean            	save_brightness = false;

// .............................................................................WIFI STUFF 
#define WIFI_TIMEOUT					4000
String 									hostname;
AsyncWebServer                          server(80);
float brightness;
boolean on;


//----------------------------------------------------------------------------------------
//																				Preferences

void setup_read_preferences() {
    preferences.begin("changlier", false);

    hostname = preferences.getString("hostname");
    if (hostname == String()) { hostname = "changlier"; }
    Serial.print("Hostname: ");
    Serial.println(hostname);
    brightness = preferences.getFloat("brightness");
    Serial.print("Brightness: ");
    Serial.println(brightness);

	preferences.end();
}

//----------------------------------------------------------------------------------------
//																			file functions

 String readFile(fs::FS &fs, const char * path){
  Serial.printf("Reading file: %s\r\n", path);
  File file = fs.open(path, "r");
  if(!file || file.isDirectory()){
    Serial.println("- empty file or failed to open file");
    return String();
  }
  Serial.println("- read from file:");
  String fileContent;
  while(file.available()){
    fileContent+=String((char)file.read());
  }
  Serial.println(fileContent);
  return fileContent;
}

void writeFile(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Writing file: %s\r\n", path);
  File file = fs.open(path, "w");
  if(!file){
    Serial.println("- failed to open file for writing");
    return;
  }
  if(file.print(message)){
    Serial.println("- file written");
  } else {
    Serial.println("- write failed");
  }
}

//----------------------------------------------------------------------------------------
//																process webpage template

// Replaces placeholders
String processor(const String& var){
  if(var == "HOSTNAME"){
        return hostname;
    }
  return String();
}
//----------------------------------------------------------------------------------------
//																				WebServer

void setup_webserver() {
   WiFi.begin(ssid, pwd);
    long start_time = millis();
    while (WiFi.status() != WL_CONNECTED) { 
        delay(500); 
        if ((millis()-start_time) > WIFI_TIMEOUT) break;
	}

  	if (WiFi.status() == WL_CONNECTED) {
  	    Serial.print("Wifi connected. IP: ");
        Serial.println(WiFi.localIP());

        if (!MDNS.begin(hostname.c_str())) {
             Serial.println("Error setting up MDNS responder!");
        }
        Serial.println("mDNS responder started");

        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(SPIFFS, "/index.html",  String(), false, processor);
        });
 
        server.on("/src/bootstrap.bundle.min.js", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(SPIFFS, "/src/bootstrap.bundle.min.js", "text/javascript");
        });
 
        server.on("/src/jquery-3.4.1.min.js", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(SPIFFS, "/src/jquery-3.4.1.min.js", "text/javascript");
        });
        server.on("/src/bootstrap-toggle.min.js", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(SPIFFS, "/src/bootstrap-toggle.min.js", "text/javascript");
        });
 
        server.on("/src/bootstrap.min.css", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(SPIFFS, "/src/bootstrap.min.css", "text/css");
        });
        server.on("/src/bootstrap-toggle.min.css", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(SPIFFS, "/src/bootstrap-toggle.min.css", "text/css");
        });

         server.on("/readADC", HTTP_GET, [] (AsyncWebServerRequest *request) {
            request->send(200, "text/text", "Hello");
        });
 
         server.on("/get", HTTP_GET, [] (AsyncWebServerRequest *request) {
            String inputMessage;
            
            inputMessage = "Nothing done.";           
                        
            //List all parameters
            int params = request->params();
            for(int i=0;i<params;i++){
              AsyncWebParameter* p = request->getParam(i);
              if(p->isFile()){ //p->isPost() is also true
                Serial.printf("FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
              } else if(p->isPost()){
                Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
              } else {
                Serial.printf("GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
              }
            }
            
        if (request->hasParam("hostname")) {
                inputMessage = request->getParam("hostname")->value();
                hostname = inputMessage;
//                writeFile(SPIFFS, "/hostname.txt", inputMessage.c_str());
                preferences.begin("changlier", false);
            	preferences.putString("hostname", hostname);
                preferences.end();

            } else if (request->hasParam("brightness")) {
            	inputMessage = request->getParam("brightness")->value();

            	brightness = inputMessage.toFloat() / 100.;
            	last_time_brightness_set = millis();
            	save_brightness = true;
            	Serial.println(brightness);
            	
            } else if (request->hasParam("onoff")) {
            	inputMessage = request->getParam("onoff")->value();
				if (inputMessage == "true") on = true;
				else on = false;
				Serial.println(on);
            }
            
            request->send(200, "text/text", inputMessage);
        });

        server.begin();
    }
}

//----------------------------------------------------------------------------------------
//																				Brightness

void set_brightness() {
	if (on) {
	    ledcWrite(ledChannel, brightness * brightness * 30000);
	} else {
	    ledcWrite(ledChannel, 0);
	}
}


//----------------------------------------------------------------------------------------
//																				Button

void check_button() {
	static int last_button;
	int button = digitalRead(PIN_BTN);
	
	if (button == last_button) return;
	last_button = button;
	
	if (!button) {
		if (on) {
			on = false;
		} else {
	    	on = true;
		}
	}
	
	Serial.println(on);
}


//----------------------------------------------------------------------------------------
//																				Encoder

void check_encoder() {
	if (encoder.getCount() == 0) return;
    char dir = encoder.getCount() > 0;
    float f = (float)encoder.getCount() / 100.;
    f = abs(f);
    f = pow(f,2);
    if (!dir) f = -f;
    brightness += f;
    
    if (brightness > 1.) brightness = 1.;
    if (brightness < 0). brightness = 0.;
    encoder.setCount(0);
	last_time_brightness_set = millis();
	save_brightness = true;
	Serial.println(brightness);
}

//========================================================================================
//----------------------------------------------------------------------------------------
//																				SETUP

void setup(){
    Serial.begin(115200);
 
    if(!SPIFFS.begin()){
         Serial.println("An Error has occurred while mounting SPIFFS");
         return;
    }
    
    pinMode(PIN_BTN,INPUT_PULLUP);
    pinMode(PIN_ENCA,INPUT_PULLUP);
    pinMode(PIN_ENCB,INPUT_PULLUP);

    encoder.attachHalfQuad(ENCA, ENCB);

 	setup_read_preferences();
 	setup_webserver();
 	2
 	// configure LED PWM functionalitites
	ledcSetup(ledChannel, freq, resolution);
	ledcAttachPin(PIN_LED, ledChannel);
  
 	t.every(10,set_brightness);
 	t.every(10,check_button);
 	t.every(20,check_encoder);
}



//========================================================================================
//----------------------------------------------------------------------------------------
//																				loop
 
void loop(){
    t.update();
    
    if (save_brightness) {
    	if ((millis() - last_time_brightness_set) > 2000)  {
    		save_brightness = false;
			preferences.begin("changlier", false);
			preferences.putString("brightness", brightness);
			preferences.end();
    	}
    }
}
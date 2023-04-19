/**
 * Picoclick v2.2
 * 
 * Button to control Volumio instance
 * 
 * Author:  C Beck, 2022.  Updated 2023.
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <FastLED.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";
const uint8_t bssid[6] = {0xA8, 0x5E, 0x45, 0xAE, 0x6C, 0x08}; //put your access point's bssid here, this along with wifi_channel helps w/ faster network acquisition
const int wifi_channel = 8;  //set the channel in your access points settings, alternatively you can comment-out wifi_channel, bssid, line 149, and un-comment line 148 
IPAddress staticIP(192, 168, 1, 222);  //set a static IP here
IPAddress gateway(192, 168, 1, 1);  //gateway, generally your wireless router's local IP
IPAddress subnet(255, 255, 255, 0); //the subnet you are using
const char* volumioIP = "http://192.168.1.61";  //the local IP address of your volumio instance
const char* volPlus = "/api/v1/commands/?cmd=volume&volume=plus";  //API endpoint inc. volume
const char* volMinus = "/api/v1/commands/?cmd=volume&volume=minus";//API endpoint dec. volume
const char* volMute = "/api/v1/commands/?cmd=volume&volume=mute";//API endpoint mute
const char* toggle = "/api/v1/commands/?cmd=toggle";  //API endpoint toggle pause/play
const char* prev = "/api/v1/commands/?cmd=prev";  //API endpoint previous track
const char* next = "/api/v1/commands/?cmd=next";  //API endpoint next track
const char* stop = "/api/v1/commands/?cmd=stop";  //API endpoint stop

HTTPClient http;
WiFiClient client;

#define LIPO_MINIMAL 3200

#define NUM_LEDS 3
CRGB leds[NUM_LEDS];


int buttonState = 0;      // current state of the button
int numPresses = 0;       // number of presses in a row
int pressTimer = 0;       // timer for presses
int lastClick = 0;        // timer for presses
int lastButtonState = 0;  // previous state of the button
int startPressed = 0;     // the moment the button was pressed
int endPressed = 0;       // the moment the button was released
int holdTime = 0;         // how long the button was held
int idleTime = 0;         // how long the button was idle
boolean firstPress = false;

const int button = 12;
const int latch = 13;
const int status_mcp = 4;

bool state = false;

boolean battery_empty = false;
boolean battery_charging = false;
boolean stay_on = true;

int hue = 0;
double pi = 3.141;
double chg_bright = 100;

int getBatteryVoltage() {
  float factor = 5;                                          // (R1+R2)/R2, +/- X
  return (float(analogRead(0)) / 1023.0 * factor) * 1000.0;  //returns mV
}

void ChargingStatus() {
  if (digitalRead(status_mcp) == 0) {
    for (int i = 0; i < NUM_LEDS; i++) leds[i] = CHSV((hue + i * 85) % 255, 250, 100);
    FastLED.show();
    delay(50);
    hue = (hue + 1) % 255;
  } else {
    CRGB c = CRGB(100, 100, 100);
    if (getBatteryVoltage() > 4100) c = CRGB(0, 100, 0);
    else c = CRGB(100, 50, 0);

    for (int i = 0; i < NUM_LEDS; i++) leds[i] = c;
    FastLED.show();

    delay(3000);
    digitalWrite(latch, LOW);
    delay(1000);
  }
}

void ConnectionCheck() {
  int v = getBatteryVoltage();
  if (v < LIPO_MINIMAL) {
    for (int b = 100; b > 10; b--) {
      for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB(b, 0, 0);
      FastLED.show();
      delay(10);
    }

    delay(200);
    digitalWrite(latch, LOW);
  }

  if (millis() > 10000) {
    for (int x = 0; x < 10; x++) {
      for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB(100, 0, 0);
      }
      FastLED.show();
      delay(50);

      delay(50);
    }
    for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB(100, 0, 0);
    FastLED.show();
    delay(20);

    digitalWrite(latch, LOW);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(latch, OUTPUT);
  digitalWrite(latch, HIGH);
  pinMode(status_mcp, INPUT_PULLUP);  //read charging status

  pinMode(button, INPUT);
  state = digitalRead(button);

  FastLED.addLeds<WS2812, 14, GRB>(leds, 3).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(200);

  for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB(0, 0, 0);
  FastLED.show();

  if (digitalRead(status_mcp) == 0) {
    //usb cable plugged in
    battery_charging = true;

    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();
    delay(100);
  } else {
    //connect & act
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.config(staticIP, gateway, subnet);
    //WiFi.begin(ssid, password, wifi_channel);
    WiFi.begin(ssid, password, wifi_channel, bssid, true);
    delay(50);
    for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB(255, 255, 150);
    FastLED.show();

    if (WiFi.waitForConnectResult() == WL_CONNECTED || WiFi.status() == WL_CONNECTED) {
      Serial.print("wifi connected");
      for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB(191, 0, 255);
      FastLED.show();
      Serial.print("[HTTP] begin...\n");
    }
  }
}

void countFn() {
  numPresses++;
}

void resetFn() {
  numPresses = 0;  // reset the counter
  //lastClick = 0;
  endPressed = 0;
  pressTimer = 0;
  firstPress = false;
  delay(10);
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB(191, 0, 255);
  FastLED.show();
}

void callApi(String endpoint) {
  String endFinal = volumioIP + endpoint;  // concat ip and endpoint
  if (http.begin(client, endFinal)) {            // HTTP
    Serial.print("[HTTP] GET...\n");
    // start connection and send HTTP header
    int httpCode = http.GET();

    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      Serial.printf("[HTTP] GET... code: %d\n", httpCode);

      // file found at server
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        String payload = http.getString();
        Serial.println(payload);
      }
    } else {
      Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  } else {
    Serial.printf("[HTTP} Unable to connect\n");
  }
  resetFn();
}

void updateState() {
  // the button has been just pressed
  if (buttonState == HIGH) {
    Serial.println("");
    Serial.println("button pressed");

    for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB(0, 255, 0);
    FastLED.show();

    startPressed = millis();
    idleTime = startPressed - endPressed;

    if (idleTime >= 500 && idleTime < 1000) {
      Serial.println("Button was idle for half a second");
    }

    if (idleTime >= 1000) {
      Serial.println("Button was idle for one second or more");
    }

    while (digitalRead(button) == 1) {  //debouncing
      delay(20);
    }

    for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB(191, 0, 255);
    FastLED.show();
  } else {
    endPressed = millis();
    lastClick = millis();
    holdTime = endPressed - startPressed;

    if (holdTime <= 499) {  //if button not held down, register as a press
      if (!firstPress) {
        firstPress = true;
        pressTimer = millis();
      }

      if ((millis() - pressTimer) <= 1500) {  //give 2 seconds to press a series of buttons, or not
        countFn();                            //increment press counter
      }
    }

    if (holdTime >= 500 && holdTime < 1000) {  //half second press - volume up?
      Serial.println("Button was hold for half a second");
      numPresses = 0;
      //brighten then dim leds to signal long-press
      for (int b = 100; b > 10; b--) {
        for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB(b, 0, 255);
        FastLED.show();
      }
      callApi(volPlus);
    }

    if (holdTime >= 1000 && holdTime < 2000) {  //one second - volume down?
      Serial.println("Button was hold for one second or more");
      numPresses = 0;

      //brighten then dim leds to signal long-press
      for (int b = 255; b > 10; b--) {
        for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB(191, 0, b);
        FastLED.show();
      }
      callApi(volMinus);
    }

    if (holdTime >= 2000) {  //2 seconds - mute?
      Serial.println("Button was hold for two seconds or more");
      numPresses = 0;
      //brighten then dim leds to signal long-press
      for (int b = 100; b > 10; b--) {
        for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB(0, b, 0);
        FastLED.show();
      }
      callApi(volMute);
    }
  }
}


void loop() {
  if (!stay_on) {
    ChargingStatus();
  }

  if ((millis() - lastClick >= 10000) && (firstPress = true)) {  //turn off if not clicked since 10 secs
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();
    delay(500);
    digitalWrite(latch, LOW);
    delay(500);
    //never reaching this
  }

  buttonState = digitalRead(button);     //read the button input
  if (buttonState != lastButtonState) {  //button state changed
    updateState();
  }
  lastButtonState = buttonState;  //save state for next loop

  //check how many button presses since 1.5 seconds
  while ((millis() - pressTimer >= 1501) && (numPresses > 0)) {

    switch (numPresses) {
      case 1:
        Serial.println("");
        Serial.println("one press");
        callApi(toggle);
        break;  //play, pause
      case 2:
        Serial.println("");
        Serial.println("two press");
        callApi(next);
        break;  //forward
      case 3:
        Serial.println("");
        Serial.println("three press");
        callApi(prev);
        break;  //reverse
      case 4:
        Serial.println("");
        Serial.println("four press");
        callApi(stop);
        break;
      default:
        //user keeps clicking
        resetFn();
        break;
    }
  }
}

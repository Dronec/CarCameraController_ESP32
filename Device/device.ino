#include <ESP_8_BIT_GFX.h>

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include <Arduino_JSON.h>
#include <AsyncElegantOTA.h>

#include <DefsWiFi.h>

// Camera controller
#define ON HIGH
#define OFF LOW

#define longClick 1500 // long button click 1.5s
#define sensorThreshold 100
#define frontCameraAutoThreshold 10000 // after the rear camera the front one turns on for 10s
#define buttonPin 14
#define buttonEmulatorPin 15 // Control D(12) pins 10,11
#define frontCamera 13       // Control B(5) pins 3,4
#define rearCamera 12        // Control A(13) pins 1,2
#define analogInPin A0       // for detecting videosignal

#define pinA 23
#define pinB 22
#define pinC 15

const char *ssid = WIFISSID_2;
const char *password = WIFIPASS_2;
const char *softwareVersion = "0.001";

const int pinList[] = {pinA, pinB, pinC};

// Analog display
ESP_8_BIT_GFX videoOut(true /* = NTSC */, 8 /* = RGB332 color */);

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Create a WebSocket object
AsyncWebSocket ws("/ws");

unsigned long buttonTimer = 0;          // the last time the output pin was toggled
unsigned long frontCameraAutoTimer = 0; // the last time the output pin was toggled
unsigned long loopCounter = 0;

int clickType = 0;
int clickLength = 0;
int videoSensor = 0;
int currentCamera = 1; // 1 - rear, 0 - front;
bool displayEnabled = false;
bool WifiStatus = false;

// Initialize LittleFS
void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("An error has occurred while mounting SPIFFS");
  }
  Serial.println("SPIFFS mounted successfully");
}

void FrontCameraOn()
{
  digitalWrite(rearCamera, OFF);
  digitalWrite(frontCamera, ON);
  currentCamera = 0;
}
void BackCameraOn()
{
  digitalWrite(frontCamera, OFF);
  digitalWrite(rearCamera, ON);
  currentCamera = 1;
  frontCameraAutoTimer = 0;
}
void ToggleCamera()
{
  if (currentCamera == 1)
    FrontCameraOn();
  else
    BackCameraOn();
}
void ClickExternalButton()
{
  digitalWrite(buttonEmulatorPin, HIGH);
  delay(250);
  digitalWrite(buttonEmulatorPin, LOW);
}

void ChannelToPin(uint channel)
{
for (int inputPin = 0; inputPin < 3; inputPin++)
{
int pinState = bitRead(channel, inputPin);
// turn the pin on or off:
digitalWrite(pinList[inputPin],pinState);

Serial.printf("Pin: %d State: %d\n",pinList[inputPin],pinState);
}

  Serial.printf("Channel: %d\n", channel);
}

String getOutputStates()
{
  JSONVar myArray;
  myArray["ssid"] = ssid;
  myArray["version"] = softwareVersion;
  myArray["sensor"] = videoSensor;
  if (currentCamera == 1)
    myArray["camera"] = "rear";
  else
  {
    if (frontCameraAutoTimer > 0)
      myArray["camera"] = "auto front";
    else
      myArray["camera"] = "front";
  }
  myArray["button"] = clickLength;

  if (clickType == 1)
    myArray["press"] = "short";
  else
    myArray["press"] = "long";

  myArray["uptime"] = millis()/1000;
  myArray["ram"] = (int)ESP.getFreeHeap();

  String jsonString = JSON.stringify(myArray);
  return jsonString;
}

void notifyClients(String state)
{
  ws.textAll(state);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
  {
    data[len] = 0;
    int channel = atoi((char *)data);
    ChannelToPin(channel);
    notifyClients(getOutputStates());
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
  case WS_EVT_CONNECT:
    Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    break;
  case WS_EVT_DISCONNECT:
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
    break;
  case WS_EVT_DATA:
    handleWebSocketMessage(arg, data, len);
    break;
  case WS_EVT_PONG:
  case WS_EVT_ERROR:
    break;
  }
}

void initWebSocket()
{
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

void setup()
{
  Serial.begin(115200);

  videoOut.begin();

  pinMode(buttonPin, INPUT);

  pinMode(buttonEmulatorPin, OUTPUT);
  pinMode(frontCamera, OUTPUT);
  pinMode(rearCamera, OUTPUT);

  pinMode(pinA, OUTPUT);
  pinMode(pinB, OUTPUT);
  pinMode(pinC, OUTPUT);
  pinMode(33, OUTPUT);
  digitalWrite(33,HIGH);
  digitalWrite(pinA,LOW);
  digitalWrite(pinB,LOW);
  digitalWrite(pinC,LOW);

  BackCameraOn();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  initSPIFFS();
  initWebSocket();

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/index.html", "text/html", false); });

  server.serveStatic("/", SPIFFS, "/");

  // Start ElegantOTA
  AsyncElegantOTA.begin(&server);

  // Start server
  server.begin();
}
void Displaystats()
{
  videoOut.waitForFrame();
  videoOut.fillScreen(0);
  videoOut.drawRect(0, 0, 320, 47, 0xF4);
  videoOut.fillRect(0, 0, 320, 47, 0xF4);
  videoOut.setCursor(0, 16);
  videoOut.setTextColor(0x00);
  videoOut.setTextSize(2);
  videoOut.print(" Wifi: ");
  if (WifiStatus)
    videoOut.println(WiFi.localIP());
  else
    videoOut.println("No connection");

  videoOut.printf(" Version: %s\n", softwareVersion);

  videoOut.setTextColor(0xFF);

  videoOut.printf(" VideoSensor: %d\n", videoSensor);

  videoOut.print(" Camera: ");
  if (currentCamera == 1)
    videoOut.println("rear");
  else
  {
    if (frontCameraAutoTimer > 0)
      videoOut.print("auto ");
    videoOut.println("front");
  }

  videoOut.printf(" Click: %d\n", clickLength);

  videoOut.print(" Click type: ");
  if (clickType == 1)
    videoOut.println("short");
  else
    videoOut.println("long");

  videoOut.printf(" Uptime: %ds\n", millis()/1000);
  videoOut.printf(" RAM: %d\n", ESP.getFreeHeap());
}

void loop()
{
  /*
  videoSensor = 0;
  for (int i = 0; i < 20; i++)
  {
    videoSensor = videoSensor + analogRead(analogInPin);
    delay(10);
  }
  videoSensor = videoSensor / 10;
  // Condition 1: rear camera activated
  if (videoSensor > sensorThreshold)
  {
    BackCameraOn();
    frontCameraAutoTimer = millis();
  }
  // Condition 2: rear camera deactivated, enable auto front camera
  if (videoSensor < sensorThreshold && currentCamera == 1 && frontCameraAutoTimer > 0)
  {
    frontCameraAutoTimer = millis();
    FrontCameraOn();
  }
  // Condition 3: disable auto front camera after threshold
  if (currentCamera == 0 && frontCameraAutoTimer > 0 && millis() - frontCameraAutoTimer > frontCameraAutoThreshold)
    BackCameraOn();

  if (digitalRead(buttonPin) == ON)
  {

    buttonTimer = millis();
    while (millis() - buttonTimer < longClick && digitalRead(buttonPin) == ON)
    {
      delay(50);
    };
    if (millis() - buttonTimer >= longClick)
    {
      clickType = 2;
      clickLength = millis() - buttonTimer;
      ClickExternalButton();
    }
    else
    {
      clickType = 1;
      clickLength = millis() - buttonTimer;
      frontCameraAutoTimer = 0;
      ToggleCamera();
    }
    // waiting for releasing the button
    while (digitalRead(buttonPin) == ON)
    {
      delay(50);
    };
  }
*/
  if (WiFi.status() == WL_CONNECTED && WifiStatus == false)
    WifiStatus = true;

  if (WiFi.status() != WL_CONNECTED && WifiStatus == true)
    WifiStatus = false;

  ws.cleanupClients();

  if (loopCounter % 10 == 0)
  {
    if (WifiStatus)
      notifyClients(getOutputStates());
  }
  //if (displayEnabled)
    Displaystats();

  loopCounter++;
  delay(200);
}

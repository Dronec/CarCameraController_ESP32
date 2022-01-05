#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "LittleFS.h"
#include <Arduino_JSON.h>
#include <AsyncElegantOTA.h>

#include <DefsWiFi.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

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

#define OLED_RESET -1 // Reset pin # (or -1 if sharing Arduino reset pin)

// Set number of outputs
#define NUM_OUTPUTS 3

const char *ssid = WIFISSID_M;
const char *password = WIFIPASS_M;
const char *softwareVersion = "1.303";
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Create a WebSocket object
AsyncWebSocket ws("/ws");

// Assign each GPIO to an output
int outputGPIOs[NUM_OUTPUTS] = {frontCamera, rearCamera, buttonEmulatorPin};

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
void initLittleFS()
{
  if (!LittleFS.begin())
  {
    Serial.println("An error has occurred while mounting LittleFS");
  }
  Serial.println("LittleFS mounted successfully");
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

  myArray["lc"] = loopCounter;

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
    if (strcmp((char *)data, "states") == 0)
    {
      notifyClients(getOutputStates());
    }
    else
    {
      int gpio = atoi((char *)data);
      if (gpio == 0)
        ToggleCamera();
      else
        digitalWrite(gpio, !digitalRead(gpio));
      notifyClients(getOutputStates());
    }
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

  pinMode(buttonPin, INPUT);

  pinMode(buttonEmulatorPin, OUTPUT);
  pinMode(frontCamera, OUTPUT);
  pinMode(rearCamera, OUTPUT);

  BackCameraOn();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  initLittleFS();
  initWebSocket();

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(LittleFS, "/index.html", "text/html", false); });

  server.serveStatic("/", LittleFS, "/");

  // Start ElegantOTA
  AsyncElegantOTA.begin(&server);

  // Start server
  server.begin();
}
void Displaystats()
{
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.print("Wifi: ");
  if (WifiStatus)
    display.println(WiFi.localIP());
  else
    display.println("No connection");

  display.print("Version: ");
  display.println(softwareVersion);

  display.print("VideoSensor: ");
  display.println(videoSensor);

  display.print("Camera: ");
  if (currentCamera == 1)
    display.println("rear");
  else
  {
    if (frontCameraAutoTimer > 0)
      display.print("auto ");
    display.println("front");
  }

  display.print("Button: ");
  display.println(clickLength);

  display.print("Press type: ");
  if (clickType == 1)
    display.println("short");
  else
    display.println("long");

  display.print("LC: ");
  display.print(loopCounter);

  display.display();
}

void loop()
{
  videoSensor = 0;
  for (int i = 0; i < 20; i++)
  {
    videoSensor = videoSensor + analogRead(analogInPin);
    delay(10);
  }
  videoSensor = videoSensor / 10;
  // Condition 1: rear camera activated
  if (videoSensor > sensorThreshold && currentCamera == 0)
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
    // Serial.println("Button pressed!");
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

  if (WiFi.status() == WL_CONNECTED && WifiStatus == false)
    WifiStatus = true;

  if (WiFi.status() != WL_CONNECTED && WifiStatus == true)
    WifiStatus = false;

  ws.cleanupClients();

  if (loopCounter % 10 == 0)
  {
    if (!displayEnabled)
      displayEnabled = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    notifyClients(getOutputStates());
  }
  if (displayEnabled)
    Displaystats();

  loopCounter++;
}

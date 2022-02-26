#include <ESP_8_BIT_GFX.h>

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include <Arduino_JSON.h>
#include <AsyncElegantOTA.h>
#include <Preferences.h>
#include <DefsWiFi.h>

// Camera controller
#define ON HIGH
#define OFF LOW

#define longClick 1500       // long button click 1.5s
#define rearCameraSensor 36  // for detecting videosignal
#define trailCameraSensor 39 // for detecting videosignal

#define pinA 23 // 13, controls 1-2
#define pinB 15 // 5, controls 3-4
#define pinC 2  // 6, controls 8-9
#define pinD 22 // 12, controls 10-11

#define rearCamera 1
#define frontCamera 2
#define intCamera 3
#define trailCamera 4

const char *ssid = WIFISSID_2;
const char *password = WIFIPASS_2;
const char *softwareVersion = "0.002";

const char *camnames[4] = {"Rear", "Front",
                           "Internal", "Trailer"};

// Analog display
ESP_8_BIT_GFX videoOut(true /* = NTSC */, 8 /* = RGB332 color */);

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Create a WebSocket object
AsyncWebSocket ws("/ws");

// EEPROM settings
Preferences preferences;

int sensorThresholdMin;       // min value for camera sensor
int sensorThresholdMax;       // max value for camera sensor
int frontCameraAutoThreshold; // after the rear camera the front one turns on for 10s

unsigned long sync = 0;                 // the last time the output pin was toggled
unsigned long frontCameraAutoTimer = 0; // the last time the output pin was toggled

int clickType = 0;
int clickLength = 0;
bool rearCamActive = false;
bool trailCamActive = false;

int v1min = 0;
int v1max = 0;
int v1cur = 0;
int v2min = 0;
int v2max = 0;
int v2cur = 0;

int currentCamera = 3; // 1 - rear, 2 - front, 3 - internal, 4 - trailer;

bool WifiStatus = false;

// Initialize LittleFS
void initSPIFFS()
{
  if (!SPIFFS.begin(true))
  {
    Serial.println("An error has occurred while mounting SPIFFS");
  }
  Serial.println("SPIFFS mounted successfully");
}

void FrontCameraOn()
{
  EnableCamera(frontCamera);
  currentCamera = frontCamera;
}
void BackCameraOn()
{
  if (rearCamActive && trailCamActive)
  {
    EnableCamera(trailCamera);
    currentCamera = trailCamera;
  }
  else
  {
    EnableCamera(rearCamera);
    currentCamera = rearCamera;
  }
  frontCameraAutoTimer = 0;
}
/*
void ToggleCamera()
{
  if (currentCamera == 1)
    FrontCameraOn();
  else
    BackCameraOn();
}
*/
void EnableCamera(uint cameraN)
{
  switch (cameraN)
  {
  case rearCamera:
    digitalWrite(pinB, LOW);
    digitalWrite(pinC, LOW);
    digitalWrite(pinD, LOW);
    digitalWrite(pinA, HIGH);
    break;
  case frontCamera:
    digitalWrite(pinA, LOW);
    digitalWrite(pinC, LOW);
    digitalWrite(pinD, LOW);
    digitalWrite(pinB, HIGH);
    break;
  case intCamera:
    digitalWrite(pinA, LOW);
    digitalWrite(pinB, LOW);
    digitalWrite(pinD, LOW);
    digitalWrite(pinC, HIGH);
    break;
  case trailCamera:
    digitalWrite(pinA, LOW);
    digitalWrite(pinB, LOW);
    digitalWrite(pinC, LOW);
    digitalWrite(pinD, HIGH);
    break;
  default:
    digitalWrite(pinA, LOW);
    digitalWrite(pinB, LOW);
    digitalWrite(pinC, LOW);
    digitalWrite(pinD, LOW);
  }
  // Serial.printf("Camera: %d\n", cameraN);
}

String getOutputStates()
{
  JSONVar myArray;
  myArray["ssid"] = ssid;
  myArray["version"] = softwareVersion;
  myArray["rearsensor"] = rearCamActive;
  myArray["trailsensor"] = trailCamActive;
  myArray["camera"] = camnames[currentCamera - 1];
  myArray["cameraid"] = currentCamera;
  myArray["camsensmin"] = sensorThresholdMin;
  myArray["camsensmax"] = sensorThresholdMax;
  myArray["camautotime"] = frontCameraAutoThreshold;

  myArray["uptime"] = millis() / 1000;
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
    JSONVar webmsg = JSON.parse((char *)data);
    if (webmsg.hasOwnProperty("camera"))
    {
      currentCamera = atoi(webmsg["camera"]);
      EnableCamera(currentCamera);
    }
    if (webmsg.hasOwnProperty("camsensmin"))
    {
      sensorThresholdMin = atoi(webmsg["camsensmin"]);
      preferences.putInt("sensorMin", sensorThresholdMin);
    }
    if (webmsg.hasOwnProperty("camsensmax"))
    {
      sensorThresholdMax = atoi(webmsg["camsensmax"]);
      preferences.putInt("sensorMax", sensorThresholdMax);
    }
    if (webmsg.hasOwnProperty("camautotime"))
    {
      frontCameraAutoThreshold = atoi(webmsg["camautotime"]);
      preferences.putInt("camAutoThreshold", frontCameraAutoThreshold);
    }
    notifyClients(getOutputStates());
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
  case WS_EVT_CONNECT:
    // Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    break;
  case WS_EVT_DISCONNECT:
    // Serial.printf("WebSocket client #%u disconnected\n", client->id());
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

  // start settings
  preferences.begin("ccc-app", false);
  sensorThresholdMin = preferences.getInt("sensorMin", 200);
  sensorThresholdMax = preferences.getInt("sensorMax", 2300);
  frontCameraAutoThreshold = preferences.getInt("camAutoThreshold", 10000);
  // end settings

  videoOut.begin();

  pinMode(pinA, OUTPUT);
  pinMode(pinB, OUTPUT);
  pinMode(pinC, OUTPUT);
  pinMode(pinD, OUTPUT);

  // BackCameraOn();
  EnableCamera(3);
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

  videoOut.print(" Rear cam: ");
  if (rearCamActive)
    videoOut.println("on");
  else
    videoOut.println("off");
  videoOut.print(" Trailer cam: ");

  if (trailCamActive)
    videoOut.println("on");
  else
    videoOut.println("off");

  videoOut.printf(" R: %d-%d,%d\n", v1min, v1max, v1max - v1min);
  videoOut.printf(" T: %d-%d,%d\n", v2min, v2max, v2max - v2min);

  // Serial.printf("%d,%d\n",vmarker1,vmarker2);
  videoOut.printf(" Camera: %d\n", currentCamera);

  videoOut.printf(" Uptime: %ds\n", millis() / 1000);
  videoOut.printf(" RAM: %d\n", ESP.getFreeHeap());
}

void loop()
{
  v1min = 4095;
  v1max = 0;
  v2min = 4095;
  v2max = 0;
  sync = millis();
  while (sync + 1000 > millis())
  {
    v1cur = analogRead(rearCameraSensor);
    delay(10);
    v2cur = analogRead(trailCameraSensor);
    if (v1min > v1cur)
      v1min = v1cur;

    if (v1max < v1cur)
      v1max = v1cur;

    if (v2min > v2cur)
      v2min = v2cur;

    if (v2max < v2cur)
      v2max = v2cur;
  }
  Serial.printf("Rear\tTrailer\tMin\tMax\n");
  Serial.printf("%d\t%d\t%d\t%d\n", v1max - v1min, v2max - v2min, sensorThresholdMin, sensorThresholdMax);

  rearCamActive = v1max - v1min > sensorThresholdMin && v1max - v1min < sensorThresholdMax;
  trailCamActive = v2max - v2min > sensorThresholdMin && v2max - v2min < sensorThresholdMax;

  // Condition 1: rear camera activated
  if (rearCamActive)
  {
    BackCameraOn();
    frontCameraAutoTimer = millis();
  }
  // Condition 2: rear camera deactivated, enable auto front camera
  if (!rearCamActive && currentCamera != frontCamera && frontCameraAutoTimer > 0)
  {
    frontCameraAutoTimer = millis();
    FrontCameraOn();
  }
  // Condition 3: disable auto front camera after threshold
  if (currentCamera == frontCamera && frontCameraAutoTimer > 0 && millis() - frontCameraAutoTimer > frontCameraAutoThreshold)
    BackCameraOn();
  /*
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

  if (WifiStatus)
    notifyClients(getOutputStates());

  if (currentCamera == intCamera)
    Displaystats();
}

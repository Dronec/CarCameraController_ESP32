// CVideo libs
#include <ESP_8_BIT_GFX.h>

// Webserver libs
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
// Network libs
#include <WiFi.h>
#include <WiFiUdp.h>
// CAN libs
#include <driver/can.h>
#include <driver/gpio.h>
#include <esp_system.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
// Everything else
#include "SPIFFS.h"
#include <Arduino_JSON.h>
#include <Arduino.h>
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
const char *softwareVersion = "0.2";

const char *camnames[4] = {"Rear", "Front",
                           "Internal", "Trailer"};

// Analog display
ESP_8_BIT_GFX videoOut(true /* = NTSC */, 8 /* = RGB332 color */);

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Create a WebSocket object
AsyncWebSocket ws("/ws");

// Create a UDP client
WiFiUDP Udp;

// EEPROM settings
Preferences preferences;

int frontCamTimeout; // after the rear camera the front one turns on for 10s
int trailerCamMode;  // 0 - Auto, 1 - Off, 2 - On
int serialOutput;    // 0 - Off, 1 - Slow, 2 - Fast
int loopDelay;
bool autoSwitch;            // when true, camera auto-switching logic applies
bool serialOverUDP = false; // when true, the data get sent to a PC

String souIP;

unsigned long sync = 0;                 // the last time the output pin was toggled
unsigned long frontCameraAutoTimer = 0; // the last time the output pin was toggled

int clickType = 0;
int clickLength = 0;
bool rearCamActive = false;
bool trailCamActive = false;
int canInterface;
int canSpeed;
int rearCamSensor;

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

void initCAN()
{
  can_general_config_t general_config = {
      .mode = static_cast<can_mode_t>(canInterface), // CAN_MODE_NORMAL,
      .tx_io = (gpio_num_t)GPIO_NUM_5,
      .rx_io = (gpio_num_t)GPIO_NUM_4,
      .clkout_io = (gpio_num_t)CAN_IO_UNUSED,
      .bus_off_io = (gpio_num_t)CAN_IO_UNUSED,
      .tx_queue_len = 100,
      .rx_queue_len = 65,
      .alerts_enabled = CAN_ALERT_NONE,
      .clkout_divider = 0};
  can_timing_config_t timing_config;
  switch (canSpeed)
  {
  case 25:
    timing_config = CAN_TIMING_CONFIG_25KBITS();
    break;
  case 50:
    timing_config = CAN_TIMING_CONFIG_50KBITS();
    break;
  case 100:
    timing_config = CAN_TIMING_CONFIG_100KBITS();
    break;
  case 125:
    timing_config = CAN_TIMING_CONFIG_125KBITS();
    break;
  case 250:
    timing_config = CAN_TIMING_CONFIG_250KBITS();
    break;
  case 500:
    timing_config = CAN_TIMING_CONFIG_500KBITS();
    break;
  case 800:
    timing_config = CAN_TIMING_CONFIG_800KBITS();
    break;
  case 1000:
    timing_config = CAN_TIMING_CONFIG_1MBITS();
    break;
  }
  can_filter_config_t filter_config = CAN_FILTER_CONFIG_ACCEPT_ALL();
  esp_err_t error;

  error = can_driver_install(&general_config, &timing_config, &filter_config);
  if (error == ESP_OK)
  {
    Serial.println("CAN Driver installation success...");
  }
  else
  {
    Serial.println("CAN Driver installation fail...");
    return;
  }

  // start CAN driver
  error = can_start();
  if (error == ESP_OK)
  {
    Serial.println("CAN Driver start success...");
  }
  else
  {
    Serial.println("CAN Driver start FAILED...");
    return;
  }
}

int SerialPrintf(char *format, ...)
{
  int ret;
  va_list arg;
  char loc_buf[64];
  char *temp = loc_buf;
  va_start(arg, format);
  ret = vsnprintf(temp, sizeof(loc_buf), format, arg);
  va_end(arg);
  if (serialOverUDP)
  {
    Udp.beginPacket(souIP.c_str(), 11000);
    ret = Udp.print(temp);
    Udp.endPacket();
  }
  else
  {
    ret = Serial.print(temp);
  }
  if (temp != loc_buf)
  {
    free(temp);
  }
  return ret;
}

void FrontCameraOn()
{
  EnableCamera(frontCamera);
  currentCamera = frontCamera;
}
void BackCameraOn()
{
  if (rearCamActive)
  {
    if ((trailerCamMode == 0 && trailCamActive) || (trailerCamMode == 2))
    {
      EnableCamera(trailCamera);
      currentCamera = trailCamera;
    }
    else
    {
      EnableCamera(rearCamera);
      currentCamera = rearCamera;
    }
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
  // sending stats
  myArray["stats"]["ssid"] = ssid;
  myArray["stats"]["softwareVersion"] = softwareVersion;
  myArray["stats"]["rearCamActive"] = rearCamActive;
  myArray["stats"]["trailCamActive"] = trailCamActive;
  myArray["stats"]["camera"] = camnames[currentCamera - 1];
  myArray["stats"]["uptime"] = millis() / 1000;
  myArray["stats"]["ram"] = (int)ESP.getFreeHeap();

  // sending values
  myArray["settings"]["currentCamera"] = currentCamera;
  myArray["settings"]["frontCamTimeout"] = frontCamTimeout;
  myArray["settings"]["trailerCamMode"] = trailerCamMode;
  myArray["settings"]["serialOutput"] = serialOutput;
  myArray["settings"]["souIP"] = souIP;
  myArray["settings"]["loopDelay"] = loopDelay;
  myArray["settings"]["canSpeed"] = canSpeed;
  myArray["settings"]["canInterface"] = canInterface;
  myArray["settings"]["rearCamSensor"] = rearCamSensor;

  // sending checkboxes
  myArray["checkboxes"]["autoSwitch"] = autoSwitch;
  myArray["checkboxes"]["serialOverUDP"] = serialOverUDP;

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
    // values
    if (webmsg.hasOwnProperty("currentCamera"))
    {
      currentCamera = atoi(webmsg["currentCamera"]);
      EnableCamera(currentCamera);
    }
    if (webmsg.hasOwnProperty("frontCamTimeout"))
      frontCamTimeout = atoi(webmsg["frontCamTimeout"]);
    if (webmsg.hasOwnProperty("serialOutput"))
      serialOutput = atoi(webmsg["serialOutput"]);
    if (webmsg.hasOwnProperty("trailerCamMode"))
      trailerCamMode = atoi(webmsg["trailerCamMode"]);
    if (webmsg.hasOwnProperty("souIP"))
      souIP = webmsg["souIP"];
    if (webmsg.hasOwnProperty("loopDelay"))
      loopDelay = atoi(webmsg["loopDelay"]);
    if (webmsg.hasOwnProperty("canInterface"))
      canInterface = atoi(webmsg["canInterface"]);
    if (webmsg.hasOwnProperty("canSpeed"))
      canSpeed = atoi(webmsg["canSpeed"]);
    if (webmsg.hasOwnProperty("rearCamSensor"))
      rearCamSensor = atoi(webmsg["rearCamSensor"]);
    // checkboxes
    if (webmsg.hasOwnProperty("autoSwitch"))
      autoSwitch = webmsg["autoSwitch"];
    if (webmsg.hasOwnProperty("serialOverUDP"))
      serialOverUDP = webmsg["serialOverUDP"];

    if (webmsg.hasOwnProperty("command"))
    {
      int command = atoi(webmsg["command"]);
      if (command == 0)
        ESP.restart();
    }

    writeEEPROMSettings();
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

void readEEPROMSettings()
{
  frontCamTimeout = preferences.getInt("frontCamTimeout", 10000);
  serialOutput = preferences.getInt("serialOutput", 0);
  autoSwitch = preferences.getBool("autoSwitch", true);
  canInterface = preferences.getInt("canInterface", 0);
  canSpeed = preferences.getInt("canSpeed", 500);
  trailerCamMode = preferences.getInt("trailerCamMode", 0);
  souIP = preferences.getString("souIP", "192.168.1.12");
  loopDelay = preferences.getInt("loopDelay", 10);
  rearCamSensor = preferences.getInt("rearCamSensor", 10);
}

void writeEEPROMSettings()
{
  preferences.putInt("frontCamTimeout", frontCamTimeout);
  preferences.putInt("serialOutput", serialOutput);
  preferences.putBool("autoSwitch", autoSwitch);
  preferences.putInt("canInterface", canInterface);
  preferences.putInt("canSpeed", canSpeed);
  preferences.putInt("trailerCamMode", trailerCamMode);
  preferences.putString("souIP", souIP);
  preferences.putInt("loopDelay", loopDelay);
  preferences.putInt("rearCamSensor", rearCamSensor);
}

void DetectVideoStream(int vr[6], int val, unsigned long timing)
{
  vr[3] = vr[4];
  vr[4] = vr[5];
  vr[5] = val;
  if (vr[4] < (vr[3] - 0) && vr[4] < (vr[5] - 0))
  {
    vr[1] = (vr[1] + (timing - vr[2])) / 2;
    vr[2] = timing;
    vr[0]++;
  }
}

void setup()
{
  Serial.begin(115200);

  // start settings
  preferences.begin("ccc-app", false);
  readEEPROMSettings();
  // end settings

  // tuning analog reader
  analogSetClockDiv(255);
  //

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
  initCAN();

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
  videoOut.print(" IP: ");
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

  videoOut.printf(" CAN bus mode: %d\n", canInterface);

  videoOut.printf(" Camera: %s\n", camnames[currentCamera - 1]);

  videoOut.printf(" Uptime: %ds\n", millis() / 1000);
  videoOut.printf(" RAM: %d\n", ESP.getFreeHeap());
}

void loop()
{
  sync = millis();

  int vdrear[6] = {0, 0, 0, 0, 0, 0};
  int vdtrail[6] = {0, 0, 0, 0, 0, 0};
  while (sync + 1000 > millis())
  {
    can_message_t rx_frame;
    if (can_receive(&rx_frame, pdMS_TO_TICKS(loopDelay)) == ESP_OK)
    {
      if (serialOutput == 3) // CAN sniffing mode
      {
        SerialPrintf("timestamp,0x%08x,", rx_frame.identifier);
        SerialPrintf("%d,", rx_frame.data_length_code);
        SerialPrintf("%d", rx_frame.flags);
        for (int i = 0; i < 7; i++)
        {
          SerialPrintf("%02x,", rx_frame.data[i]);
        }
        SerialPrintf("%02x", rx_frame.data[7]);
        SerialPrintf("\n");
      }
    }
    if (static_cast<can_mode_t>(canInterface) == CAN_MODE_LISTEN_ONLY)
      delay(loopDelay);
    v1cur = analogRead(rearCameraSensor);
    DetectVideoStream(vdrear, v1cur, millis());
    v2cur = analogRead(trailCameraSensor);
    DetectVideoStream(vdtrail, v2cur, millis());

    if (serialOutput == 2)
    {
      Serial.println("Rear,Trailer");
      SerialPrintf("%d,%d\n", v1cur, v2cur);
    }
  }

  if (serialOutput == 1)
  {
    Serial.println("Rear_frames,Trailer_frames,T1,T2");
    SerialPrintf("%d,%d,%d,%d\n", vdrear[0], vdtrail[0], vdrear[1], vdtrail[1]);
  }
  rearCamActive = vdrear[0] >= rearCamSensor && vdrear[0] <= 50;
  trailCamActive = vdtrail[0] >= 36 && vdtrail[0] <= 40 && (vdtrail[1] == 23 || vdtrail[1] == 26);

  if (autoSwitch)
  {
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
    if (currentCamera == frontCamera && frontCameraAutoTimer > 0 && millis() - frontCameraAutoTimer > frontCamTimeout)
      BackCameraOn();
  }
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
  {
    WifiStatus = true;
    Serial.println(WiFi.localIP());
  }
  if (WiFi.status() != WL_CONNECTED && WifiStatus == true)
    WifiStatus = false;

  ws.cleanupClients();

  if (WifiStatus)
    notifyClients(getOutputStates());

  if (currentCamera == intCamera)
    Displaystats();
}

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

#define reverseInput 13  // reverse signal input
#define reverseOutput 27 // reverse signal output

#define pinA 23 // 13, controls 1-2
#define pinB 15 // 5, controls 3-4
#define pinC 2  // 6, controls 8-9
#define pinD 22 // 12, controls 10-11

#define noCamera 0
#define rearCamera 1
#define frontCamera 2
#define intCamera 3
#define trailCamera 4

#define rearCamModeRear 1
#define rearCamModeTrailer 2

const char *ssid = WIFISSID_M;
const char *password = WIFIPASS_M;
const char *softwareVersion = "1.0";

const char *camnames[5] = {"Off", "Rear", "Front",
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
int rearCamMode;     // 1 - Off (Rear camera), 2 - On (Trailer camera)
int serialOutput;    // 0 - Off, 1 - Serial ping, 3 - CANBUS dump
int loopDelay;
bool autoSwitch;            // when true, camera auto-switching logic applies
bool serialOverUDP = false; // when true, the data get sent to a PC

unsigned long sync = 0;
unsigned long frontCameraAutoTimer = 0; // Used for intelligent switching between rear and front cameras

int clickType = 0;
int clickLength = 0;
bool reverseGearActive;
int canInterface;
int canSpeed;

int currentCamera = 0; // 0 - off, 1 - rear, 2 - front, 3 - internal, 4 - trailer;

bool WifiStatus = false;
bool manualFrontCamTrigger = false;

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
    timing_config = CAN_TIMING_CONFIG_100KBITS(); // Hyundai Santa Fe 2015 MM_CAN
    break;
  case 125:
    timing_config = CAN_TIMING_CONFIG_125KBITS();
    break;
  case 250:
    timing_config = CAN_TIMING_CONFIG_250KBITS();
    break;
  case 500:
    timing_config = CAN_TIMING_CONFIG_500KBITS(); // Hyundai Santa Fe 2015 HS_CAN
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
  char loc_buf[128];
  char *temp = loc_buf;
  va_start(arg, format);
  ret = vsnprintf(temp, sizeof(loc_buf), format, arg);
  va_end(arg);
  if (serialOverUDP)
  {
    Udp.beginPacket("255.255.255.255", 11000);
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
}
void BackCameraOn()
{
  if (reverseGearActive)
  {
    if ((rearCamMode == rearCamModeTrailer))
    {
      EnableCamera(trailCamera);
    }
    else
    {
      EnableCamera(rearCamera);
    }
  }
  frontCameraAutoTimer = 0;
}
void AllCamerasOff()
{
  EnableCamera(noCamera);
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
    digitalWrite(reverseOutput, HIGH);
    break;
  case frontCamera:
    digitalWrite(pinA, LOW);
    digitalWrite(pinC, LOW);
    digitalWrite(pinD, LOW);
    digitalWrite(pinB, HIGH);
    digitalWrite(reverseOutput, HIGH);
    break;
  case intCamera:
    digitalWrite(pinA, LOW);
    digitalWrite(pinB, LOW);
    digitalWrite(pinD, LOW);
    digitalWrite(pinC, HIGH);
    digitalWrite(reverseOutput, HIGH);
    break;
  case trailCamera:
    digitalWrite(pinA, LOW);
    digitalWrite(pinB, LOW);
    digitalWrite(pinC, LOW);
    digitalWrite(pinD, HIGH);
    digitalWrite(reverseOutput, HIGH);
    break;
  default:
    digitalWrite(pinA, LOW);
    digitalWrite(pinB, LOW);
    digitalWrite(pinC, LOW);
    digitalWrite(pinD, LOW);
    digitalWrite(reverseOutput, LOW);
  }
  currentCamera = cameraN;
}

char *millisToTime(unsigned long currentMillis)
{
  unsigned long seconds = currentMillis / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;
  currentMillis %= 1000;
  seconds %= 60;
  minutes %= 60;
  hours %= 24;
  static char buffer[50];
  if (days == 0 && hours == 0 && minutes == 0)
    sprintf(buffer, "%lu sec ", seconds);
  else if (days == 0 && hours == 0 && minutes > 0)
    sprintf(buffer, "%lu min %lu sec ", minutes, seconds);
  else if (days == 0 && hours > 0)
    sprintf(buffer, "%lu h %lu m %lu s ", hours, minutes, seconds);
  else
    sprintf(buffer, "%lud %luh %lum %lus ", days, hours, minutes, seconds);
  return buffer;
}

String getOutputStates()
{
  JSONVar myArray;
  // sending stats
  myArray["stats"]["ssid"] = ssid;
  myArray["stats"]["softwareVersion"] = softwareVersion;
  myArray["stats"]["reverseGearActive"] = reverseGearActive;
  myArray["stats"]["camera"] = camnames[currentCamera];
  myArray["stats"]["uptime"] = millisToTime(millis());
  myArray["stats"]["ram"] = (int)ESP.getFreeHeap();

  // sending values
  myArray["settings"]["currentCamera"] = currentCamera;
  myArray["settings"]["frontCamTimeout"] = frontCamTimeout;
  myArray["settings"]["rearCamMode"] = rearCamMode;
  myArray["settings"]["serialOutput"] = serialOutput;
  myArray["settings"]["loopDelay"] = loopDelay;
  myArray["settings"]["canSpeed"] = canSpeed;
  myArray["settings"]["canInterface"] = canInterface;

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
    if (webmsg.hasOwnProperty("rearCamMode"))
      rearCamMode = atoi(webmsg["rearCamMode"]);
    if (webmsg.hasOwnProperty("loopDelay"))
      loopDelay = atoi(webmsg["loopDelay"]);
    if (webmsg.hasOwnProperty("canInterface"))
      canInterface = atoi(webmsg["canInterface"]);
    if (webmsg.hasOwnProperty("canSpeed"))
      canSpeed = atoi(webmsg["canSpeed"]);
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
  serialOutput = 0; // serialOutput = preferences.getInt("serialOutput", 0);
  autoSwitch = preferences.getBool("autoSwitch", true);
  canInterface = preferences.getInt("canInterface", 0);
  canSpeed = preferences.getInt("canSpeed", 500);
  rearCamMode = preferences.getInt("rearCamMode", 1);
  loopDelay = preferences.getInt("loopDelay", 10);
}

void writeEEPROMSettings()
{
  preferences.putInt("frontCamTimeout", frontCamTimeout);
  preferences.putInt("serialOutput", serialOutput);
  preferences.putBool("autoSwitch", autoSwitch);
  preferences.putInt("canInterface", canInterface);
  preferences.putInt("canSpeed", canSpeed);
  preferences.putInt("rearCamMode", rearCamMode);
  preferences.putInt("loopDelay", loopDelay);
}

void setup()
{
  Serial.begin(115200);

  // start settings
  preferences.begin("ccc-app", false);
  readEEPROMSettings();
  // end settings

  // setting up reverse control
  pinMode(reverseInput, INPUT_PULLUP);
  pinMode(reverseOutput, OUTPUT);
  //

  videoOut.begin();

  pinMode(pinA, OUTPUT);
  pinMode(pinB, OUTPUT);
  pinMode(pinC, OUTPUT);
  pinMode(pinD, OUTPUT);

  // BackCameraOn();
  EnableCamera(noCamera);
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

  videoOut.print(" Reverse gear: ");
  if (reverseGearActive)
    videoOut.println("on");
  else
    videoOut.println("off");
  videoOut.print(" Rear mode: ");

  if (rearCamMode == rearCamModeRear)
    videoOut.println("Rear");
  else
    videoOut.println("Trailer");

  videoOut.printf(" CAN bus mode: %d\n", canInterface);

  videoOut.printf(" Camera: %s\n", camnames[currentCamera]);

  videoOut.printf(" Uptime: %ds\n", millis() / 1000);
  videoOut.printf(" RAM: %d\n", ESP.getFreeHeap());
}

void loop()
{
  sync = millis();
  if (serialOutput == 1) // Serial ping
  {
    SerialPrintf("Current millis:%d\n", sync);
  }
  while (sync + loopDelay > millis())
  {
    can_message_t rx_frame;
    if (can_receive(&rx_frame, pdMS_TO_TICKS(loopDelay)) == ESP_OK)
    {
      if (serialOutput == 3) // CAN sniffing mode
      {
        SerialPrintf("#ts#,0x%04x,%d,%d,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x\n", rx_frame.identifier, rx_frame.data_length_code, rx_frame.flags, rx_frame.data[0], rx_frame.data[1], rx_frame.data[2], rx_frame.data[3], rx_frame.data[4], rx_frame.data[5], rx_frame.data[6], rx_frame.data[7]);
      }
      // Front cam manual trigger (DUAL button in the car)
      if (rx_frame.identifier == 0x132 && rx_frame.data[1] == 0x10)
        manualFrontCamTrigger = true;
    }
    // if (static_cast<can_mode_t>(canInterface) == CAN_MODE_LISTEN_ONLY)
    //   delay(loopDelay);
  }
  reverseGearActive = !digitalRead(reverseInput);

  if (autoSwitch)
  {
    // Condition 1: rear camera activated
    if (reverseGearActive)
    {
      BackCameraOn();
      frontCameraAutoTimer = millis();
    }
    // Condition 2: rear camera deactivated, enable auto front camera
    if ((!reverseGearActive) && ((currentCamera != frontCamera && frontCameraAutoTimer > 0) || manualFrontCamTrigger))
    {
      frontCameraAutoTimer = millis();
      FrontCameraOn();
      manualFrontCamTrigger = false;
    }
    // Condition 3: disable auto front camera after threshold
    if ((currentCamera == frontCamera && frontCameraAutoTimer > 0 && millis() - frontCameraAutoTimer > frontCamTimeout) ||
        (!reverseGearActive && frontCameraAutoTimer == 0))
      AllCamerasOff();
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

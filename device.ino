#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

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
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

unsigned long buttonTimer = 0;          // the last time the output pin was toggled
unsigned long frontCameraAutoTimer = 0; // the last time the output pin was toggled

int clickType = 0;
int clickLength = 0;
int videoSensor = 0;
int currentCamera = 1; // 1 - rear, 0 - front;
bool displayEnabled = false;

void setup()
{
  pinMode(buttonPin, INPUT);

  pinMode(buttonEmulatorPin, OUTPUT);
  pinMode(frontCamera, OUTPUT);
  pinMode(rearCamera, OUTPUT);

  BackCameraOn();

  displayEnabled = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
}
void Displaystats()
{
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.println("Wifi: No connection");
  display.println("Software version: 0.1");
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
  display.display();
}
void FrontCameraOn()
{
  digitalWrite(frontCamera, ON);
  digitalWrite(rearCamera, OFF);
  currentCamera = 0;
  delay(1000);
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
void loop()
{
  videoSensor = 0;
  for (int i = 0; i < 10; i++)
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
      digitalWrite(buttonEmulatorPin, HIGH);
      delay(250);
      digitalWrite(buttonEmulatorPin, LOW);
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
  if (displayEnabled)
    Displaystats();
}

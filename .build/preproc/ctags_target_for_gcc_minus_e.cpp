# 1 "c:\\Users\\Pups\\source\\repos\\projects\\CarCamCommutator\\Device\\device.ino"
# 2 "c:\\Users\\Pups\\source\\repos\\projects\\CarCamCommutator\\Device\\device.ino" 2
# 3 "c:\\Users\\Pups\\source\\repos\\projects\\CarCamCommutator\\Device\\device.ino" 2
# 4 "c:\\Users\\Pups\\source\\repos\\projects\\CarCamCommutator\\Device\\device.ino" 2
# 5 "c:\\Users\\Pups\\source\\repos\\projects\\CarCamCommutator\\Device\\device.ino" 2




// Camera controller
# 26 "c:\\Users\\Pups\\source\\repos\\projects\\CarCamCommutator\\Device\\device.ino"
Adafruit_SSD1306 display(128 /* OLED display width, in pixels*/, 64 /* OLED display height, in pixels*/, &Wire, -1 /* Reset pin # (or -1 if sharing Arduino reset pin)*/);

unsigned long buttonTimer = 0; // the last time the output pin was toggled
unsigned long frontCameraAutoTimer = 0; // the last time the output pin was toggled
int clickType = 0;
int clickLength = 0;
int videoSensor = 0;
int currentCamera = 1; // 1 - rear, 0 - front;
void setup()
{

  pinMode(14, 0x00);
  pinMode(15 /* Control B(5) pins 3,4*/, 0x01);

  pinMode(13 /* Control D(12) pins 10,11*/, 0x01);
  pinMode(12 /* Control A(13) pins 1,2*/, 0x01);

  BackCameraOn();

  if(!display.begin(0x02 /*|< Gen. display voltage from 3.3V*/, 0x3C)) {
    Serial.println(((reinterpret_cast<const __FlashStringHelper *>(
# 46 "c:\\Users\\Pups\\source\\repos\\projects\\CarCamCommutator\\Device\\device.ino" 3
                  (__extension__({static const char __pstr__[] __attribute__((__aligned__(4))) __attribute__((section( "\".irom0.pstr." "device.ino" "." "46" "." "9" "\", \"aSM\", @progbits, 1 #"))) = (
# 46 "c:\\Users\\Pups\\source\\repos\\projects\\CarCamCommutator\\Device\\device.ino"
                  "SSD1306 allocation failed"
# 46 "c:\\Users\\Pups\\source\\repos\\projects\\CarCamCommutator\\Device\\device.ino" 3
                  ); &__pstr__[0];}))
# 46 "c:\\Users\\Pups\\source\\repos\\projects\\CarCamCommutator\\Device\\device.ino"
                  ))));
    for(;;); // Don't proceed, loop forever
  }
}
void Displaystats()
{
  display.clearDisplay();
  display.setCursor(0,0);
  display.setTextColor(1 /*|< Draw 'on' pixels*/);
  display.setTextSize(1);
  display.println("Wifi: No connection");
  display.println("Software version: 0.1");
  display.print("VideoSensor: ");display.println(videoSensor);
  display.print("Camera: ");
  if (currentCamera==1)
    display.println("rear");
  else
  {
    if (frontCameraAutoTimer>0)
      display.print("auto ");
    display.println("front");
  }
  display.print("Button: ");display.println(clickLength);
  display.print("Press type: ");
  if (clickType==1)
    display.println("short");
  else
    display.println("long");
  display.display();
}
void FrontCameraOn()
{
  digitalWrite(13 /* Control D(12) pins 10,11*/, 0x1);
  digitalWrite(12 /* Control A(13) pins 1,2*/, 0x0);
  currentCamera=0;
  delay(1000);
}
void BackCameraOn()
{
  digitalWrite(13 /* Control D(12) pins 10,11*/, 0x0);
  digitalWrite(12 /* Control A(13) pins 1,2*/, 0x1);
  currentCamera=1;
  frontCameraAutoTimer = 0;
}
void ToggleCamera()
{
  if (currentCamera==1)
    FrontCameraOn();
  else
    BackCameraOn();
}
void loop()
{
  videoSensor=0;
  for (int i=0;i<10;i++)
  {
  videoSensor = videoSensor + analogRead(A0);
  delay(10);
  }
  videoSensor=videoSensor/10;
  // Condition 1: rear camera activated
  if (videoSensor>100)
  {
    BackCameraOn();
    frontCameraAutoTimer = millis();
  }
  //Condition 2: rear camera deactivated, enable auto front camera
  if (videoSensor<100 && currentCamera == 1 && frontCameraAutoTimer>0)
  {
    frontCameraAutoTimer = millis();
    FrontCameraOn();
  }
  //Condition 3: disable auto front camera after threshold
  if (currentCamera == 0 && frontCameraAutoTimer>0 && millis()-frontCameraAutoTimer>10000)
    BackCameraOn();

  if (digitalRead(14) == 0x1)
  {
    Serial.println("Button pressed!");
    buttonTimer = millis();
    while (millis() - buttonTimer < 1500 && digitalRead(14) == 0x1)
    {
      delay(50);
    };
    if (millis() - buttonTimer >= 1500)
    {
      clickType=2;
      clickLength = millis() - buttonTimer;
      digitalWrite(15 /* Control B(5) pins 3,4*/, 0x1);
      delay(250);
      digitalWrite(15 /* Control B(5) pins 3,4*/, 0x0);
    }
    else
    {
      clickType=1;
      clickLength = millis() - buttonTimer;
      ToggleCamera();
    }
    // waiting for releasing the button
    while (digitalRead(14) == 0x1)
    {
      delay(50);
    };
  }
  Displaystats();
}

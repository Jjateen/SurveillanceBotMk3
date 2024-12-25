#include <ESP32Servo.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include "HX710B.h"
#include <TinyGPSPlus.h>
//#include <FS.h>
//#include <LittleFS.h>

// Camera model selection
#define CAMERA_MODEL_AI_THINKER

// WiFi credentials
const char* ssid = "ssid";  
const char* password = "password";  

// Camera pin settings
#if defined(CAMERA_MODEL_AI_THINKER)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
#else
#error "Camera model not selected"
#endif

// GPIO pin settings for car control
extern int gpLb =  2;
extern int gpLf = 14;
extern int gpRb = 15;
extern int gpRf = 13;
extern int gpLed =  4;
extern String WiFiAddr = "";

// Server definitions
WiFiServer cameraServer(81);   // Camera web server (port 81)
WiFiServer controlServer(82);  // Control web server for GPS, sensors, servo (port 82)

// HX710B pressure sensor
HX710B pressure_sensor;
long offset = 1855509;

// The TinyGPSPlus object
TinyGPSPlus gps;

// Define the analog pins for the sensors
const int mq2Pin = 5;  // MQ-2 sensor pin

// Define the pins for the pressure sensor
const byte MPS_OUT_pin = 18;  // OUT data pin
const byte MPS_SCK_pin = 19;  // clock data pin

// WebServer and Servo motor
WebServer server(8080);  // Web server on port 8080
Servo servoMotor;        // Servo object

float pressureValue = 0.0;  // Pressure sensor value
int servoAngle = 90;        // Initial servo angle

// Task Handles
TaskHandle_t sensorTaskHandle;
TaskHandle_t cameraTaskHandle;

// GPS and Sensor Task
void gpsAndSensorTask(void* parameter) {
  for (;;) {
    // MQ-2 sensor reading
    int mq2Value = analogRead(mq2Pin);
    Serial.println("MQ-2 Gas Sensor Reading: " + String(mq2Value));

    // Pressure sensor reading
    pressureValue = pressure_sensor.is_ready() ? pressure_sensor.pascal() : 0;
    Serial.println("Pressure Sensor Reading: " + String(pressureValue, 2) + " kPa");

    // GPS data processing
    while (Serial2.available() > 0) {
      if (gps.encode(Serial2.read())) {
        displayInfo();
      }
    }

    if (millis() > 5000 && gps.charsProcessed() < 10) {
      Serial.println(F("No GPS detected: check wiring."));
    }

    delay(1000);  // Adjust the delay as needed
  }
}

// Camera Server Task (Running on Core 0)
void cameraServerTask(void* parameter) {
  for (;;) {
    server.handleClient();  // Handle incoming client requests for the web server
    delay(10); // Small delay for the server to process requests
  }
}

// Function to start the camera server
void startCameraServer() {
  cameraServer.begin();
  Serial.println("Camera server started on port 81");
}

// Function to start the control server (for GPS, sensors, servo)
void startControlServer() {
  controlServer.begin();
  Serial.println("Control server started on port 82");
}

void startWebServer() {
  server.on("/", HTTP_GET, handleRoot);  // Root URL for displaying sensor info
  server.on("/servo", HTTP_POST, handleServoControl);  // Handle servo angle adjustment
  server.begin();
  Serial.println("Web server started on port 8080");
}

// Function to handle the root page (sensor data)
void handleRoot() {
  String html = "<html><head>";
  html += "<link rel='stylesheet' href='https://unpkg.com/leaflet@1.7.1/dist/leaflet.css' />";
  html += "<style>";
  html += "body { background-color: #242b3a; color: white; font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 0; }";
  html += ".info { display: inline-block; margin: 0 10px; }";
  html += "#map-container { width: 600px; height: 600px; margin: 0 auto; border-radius: 20px; overflow: hidden; }";
  html += "#map { width: 100%; height: 100%; }";
  html += ".slider-container { margin-top: 20px; }";
  html += "</style>";
  html += "</head><body>";
  html += "<h1>Surveillance Bot Data</h1>";
  html += "<div class='info'><p>Latitude: " + String(gps.location.lat(), 6) + "</p></div>";
  html += "<div class='info'><p>Longitude: " + String(gps.location.lng(), 6) + "</p></div>";
  html += "<div class='info'><p>MQ-2 Gas Sensor Reading: " + String(analogRead(mq2Pin)) + "</p></div>";
  html += "<div class='info'><p>Pressure Sensor Reading: " + String(pressureValue, 2) + " kPa</p></div>";
  html += "<div class='slider-container'>";
  html += "<p>Servo Motor Control:</p>";
  html += "<input type='range' id='servoSlider' min='0' max='180' step='1' value='" + String(servoAngle) + "' oninput='updateServo(this.value)' />";
  html += "<span id='sliderValue'>" + String(servoAngle) + "</span>";
  html += "</div>";
  html += "<div id='map-container'><div id='map'></div></div>";
  html += "<script src='https://unpkg.com/leaflet@1.7.1/dist/leaflet.js'></script>";
  html += "<script>";
  html += "var map = L.map('map').setView([" + String(gps.location.lat(), 6) + ", " + String(gps.location.lng(), 6) + "], 13);";
  html += "L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png').addTo(map);";
  html += "L.marker([" + String(gps.location.lat(), 6) + ", " + String(gps.location.lng(), 6) + "]).addTo(map);";
  html += "function updateServo(value) { document.getElementById('sliderValue').textContent = value; }";
  html += "</script>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

// Function to handle the servo control
void handleServoControl() {
  if (server.hasArg("servo_angle")) {
    servoAngle = server.arg("servo_angle").toInt();
    servoMotor.write(servoAngle);
    String response = "Servo angle set to " + String(servoAngle);
    server.send(200, "text/plain", response);
  } else {
    server.send(400, "text/plain", "Bad Request: Missing servo_angle parameter");
  }
}

// GPS and Sensor Task for uploading data
void gpsAndSensorTask(void* parameter) {
  for (;;) {
    // MQ-2 sensor reading
    int mq2Value = analogRead(mq2Pin);
    Serial.println("MQ-2 Gas Sensor Reading: " + String(mq2Value));

    // Pressure sensor reading
    pressureValue = pressure_sensor.is_ready() ? pressure_sensor.pascal() : 0;
    Serial.println("Pressure Sensor Reading: " + String(pressureValue, 2) + " kPa");

    // GPS data processing
    while (Serial2.available() > 0) {
      if (gps.encode(Serial2.read())) {
        displayInfo();
      }
    }

    if (millis() > 5000 && gps.charsProcessed() < 10) {
      Serial.println(F("No GPS detected: check wiring."));
    }

    delay(1000);  // Adjust the delay as needed
  }
}

// Function to display GPS information
void displayInfo() {
  Serial.print(F("Location: "));
  if (gps.location.isValid()) {
    Serial.print("Lat: ");
    Serial.print(gps.location.lat(), 6);
    Serial.print(F(","));
    Serial.print("Lng: ");
    Serial.println(gps.location.lng(), 6);
  } else {
    Serial.println(F("INVALID"));
  }
}

// Camera Server Task (Running on Core 0)
void cameraServerTask(void* parameter) {
  for (;;) {
    server.handleClient();  // Handle incoming client requests for the web server
    delay(10); // Small delay for the server to process requests
  }
}

// Function to start the camera server
void startCameraServer() {
  cameraServer.begin();
  Serial.println("Camera server started on port 81");
}

// Function to start the control server (for GPS, sensors, servo)
void startControlServer() {
  controlServer.begin();
  Serial.println("Control server started on port 82");
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  // Initialize GPIO for car control
  pinMode(gpLb, OUTPUT);
  pinMode(gpLf, OUTPUT);
  pinMode(gpRb, OUTPUT);
  pinMode(gpRf, OUTPUT);
  pinMode(gpLed, OUTPUT);

  digitalWrite(gpLb, LOW);
  digitalWrite(gpLf, LOW);
  digitalWrite(gpRb, LOW);
  digitalWrite(gpRf, LOW);
  digitalWrite(gpLed, LOW);

  // Camera configuration
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_camera_init(&config);

  // Initialize Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to Wi-Fi");

  // Initialize sensor
  pressure_sensor.begin(MPS_SCK_pin, MPS_OUT_pin);
  pressure_sensor.set_offset(offset);

  // Initialize Servo
  servoMotor.attach(4);
  servoMotor.write(servoAngle);

  // Create tasks for multitasking
  xTaskCreate(gpsAndSensorTask, "GPS and Sensor Task", 2048, NULL, 1, &sensorTaskHandle);
  xTaskCreate(cameraServerTask, "Camera Server Task", 2048, NULL, 1, &cameraTaskHandle);

  // Start web servers
  startWebServer();
  startCameraServer();
  startControlServer();
}

void loop() {
  // Main loop
  delay(100);
}

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <U8g2lib.h>

// ---------------- CONFIG -------------------
#define DEVICE_ID "node1"

#define DHTPIN 15
#define DHTTYPE DHT11
#define MQ_PIN 34

#define I2C_SDA 21
#define I2C_SCL 22

#define BUTTON_PIN 5
#define LED_PIN 18

// U8G2 Display (SH1106 I2C 128x64)
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// CHANGE THIS TO YOUR MACHINE’S IP FOR LOCAL TESTING
#define SERVER_URL "https://sheat-iot-backend.onrender.com"


// Timers
const unsigned long POST_INTERVAL = 2000;
const unsigned long SENSOR_INTERVAL = 2000;

// -------------------------------------------
Preferences preferences;
WebServer webServer(80);
DHT dht(DHTPIN, DHTTYPE);

bool wifiConnected = false;
unsigned long lastPost = 0;
unsigned long lastSensor = 0;
unsigned long buttonPressTime = 0;

// ----------------------------------------------------
// WiFi Credentials Handling
// ----------------------------------------------------
bool hasSavedCredentials() {
  preferences.begin("wifi", true);
  String ssid = preferences.getString("ssid", "");
  preferences.end();
  return ssid.length() > 0;
}

void saveCredentials(String ssid, String pass) {
  preferences.begin("wifi", false);
  preferences.putString("ssid", ssid);
  preferences.putString("pass", pass);
  preferences.end();
}

bool connectToSavedWiFi() {
  preferences.begin("wifi", true);
  String ssid = preferences.getString("ssid", "");
  String pass = preferences.getString("pass", "");
  preferences.end();

  if (ssid == "") return false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long start = millis();
  while (millis() - start < 20000) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      digitalWrite(LED_PIN, LOW);  // LED OFF when connected
      Serial.println("WiFi connected.");
      Serial.println(WiFi.localIP());
      return true;
    }
    delay(300);
  }
  wifiConnected = false;
  WiFi.disconnect(true);
  return false;
}

// -------------------- CAPTIVE PORTAL -------------------------
const char PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html><body>
<h2>Enter WiFi Details</h2>
<form action="/save" method="POST">
SSID:<br><input name='ssid'><br>
Password:<br><input name='pass'><br><br>
<input type='submit' value='Save & Connect'>
</form>
</body></html>
)HTML";

void handleRoot() { webServer.send(200, "text/html", PAGE); }

void handleSave() {
  String ssid = webServer.arg("ssid");
  String pass = webServer.arg("pass");

  saveCredentials(ssid, pass);
  webServer.send(200, "text/html", "<h3>Saved. Rebooting...</h3>");
  delay(2000);
  ESP.restart();
}

void startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32-Setup-node1", "12345678");  // <-- PASSWORD ADDED

  Serial.println("AP Mode Started");
  Serial.println("SSID: ESP32-Setup-node1");
  Serial.println("PASSWORD: 12345678");
  Serial.println("Go to http://192.168.4.1/");

  webServer.on("/", handleRoot);
  webServer.on("/save", HTTP_POST, handleSave);
  webServer.begin();

  digitalWrite(LED_PIN, HIGH); // LED blinking in loop
}


// -------------------- OLED DISPLAY -------------------------
void displayData(float t, float h, int h2, int co, int ch4, int aqi) {
  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);

  display.setCursor(0, 10);
  display.printf("Temp: %.1f C", t);

  display.setCursor(0, 22);
  display.printf("Hum : %.1f %%", h);

  display.setCursor(0, 34);
  display.printf("H2  : %d ppm", h2);

  display.setCursor(0, 46);
  display.printf("CO  : %d ppm", co);

  display.setCursor(0, 58);
  display.printf("CH4 : %d ppm", ch4);

  display.setCursor(80, 58);
  display.printf("AQI:%d", aqi);

  display.sendBuffer();
}

// -------------------- SEND DATA -------------------------
void sendToServer(float temp, float hum, int h2, int co, int ch4, int aqi) {
  if (!wifiConnected) return;

  HTTPClient http;
  String url = String(SERVER_URL) + "/api/data/" + DEVICE_ID;

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["temperature"] = temp;
  doc["humidity"] = hum;
  doc["h2_ppm"] = h2;
  doc["co_ppm"] = co;
  doc["ch4_ppm"] = ch4;
  doc["aqi"] = aqi;

  String payload;
  serializeJson(doc, payload);

  int code = http.POST(payload);
  Serial.printf("POST %s -> %d\n", url.c_str(), code);

  http.end();
}

// ----------------------------------------------------
// SETUP
// ----------------------------------------------------
void showIntroScreens() {
  // Screen 1: WELCOME
  display.clearBuffer();
  display.setFont(u8g2_font_6x12_tf);
  display.setCursor(35, 32);
  display.println("WELCOME!!");
  display.sendBuffer();
  delay(2000);

  // Screen 2: Team Name
  display.clearBuffer();
  display.setFont(u8g2_font_6x12_tf);
  display.setCursor(10, 28);
  display.println("Made by");
  display.setCursor(10, 44);
  display.println("Team Nischay");
  display.sendBuffer();
  delay(2500);

  // Screen 3: Members
  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  display.setCursor(5, 24);
  display.println("by Tanmay Baranwal");
  display.setCursor(5, 40);
  display.println("and");
  display.setCursor(5, 55);
  display.println("Shravani Jadhav");
  display.sendBuffer();
  delay(3000);
}


void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  dht.begin();

  Wire.begin(I2C_SDA, I2C_SCL);

  // Start OLED
  display.begin();
  showIntroScreens();


  if (!connectToSavedWiFi()) {
    startAP();
  }
}

// ----------------------------------------------------
// LOOP
// ----------------------------------------------------
void loop() {

  // BUTTON PRESS HANDLER (5 sec hold)
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (buttonPressTime == 0) buttonPressTime = millis();
    if (millis() - buttonPressTime > 5000) {
      Serial.println("Button held 5 sec → AP Mode");
      startAP();
    }
  } else {
    buttonPressTime = 0;
  }

  // LED BLINKING IN AP MODE
  if (WiFi.getMode() == WIFI_AP) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    delay(300);
    webServer.handleClient();
    return;
  }

  // NORMAL WIFI MODE
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    digitalWrite(LED_PIN, HIGH);
  } else {
    wifiConnected = true;
    digitalWrite(LED_PIN, LOW);
  }

  // Read sensors
  if (millis() - lastSensor > SENSOR_INTERVAL) {
    lastSensor = millis();

    float hum = dht.readHumidity();
    float temp = dht.readTemperature();
    int mqRaw = analogRead(MQ_PIN);

    int h2_ppm = map(mqRaw, 200, 3500, 10, 300);
    int co_ppm = map(mqRaw, 200, 3500, 5, 200);
    int ch4_ppm = map(mqRaw, 200, 3500, 20, 500);

    int aqi = 110 + (co_ppm / 5) + (h2_ppm / 8);
    aqi = constrain(aqi, 110, 190);

    displayData(temp, hum, h2_ppm, co_ppm, ch4_ppm, aqi);

    if (millis() - lastPost > POST_INTERVAL) {
      lastPost = millis();
      sendToServer(temp, hum, h2_ppm, co_ppm, ch4_ppm, aqi);
    }
  }
}

/*
 * HydroFiliz — Hidroponik Bitki İzleme Sistemi
 * ESP32 + SSD1306 128x32 + DHT22 + BH1750 + DS18B20 + TDS + SCD30
 *
 * Ekran düzeni (32px yükseklik):
 *   y=0..7   : başlık barı (beyaz zemin, siyah yazı)
 *   y=8..25  : içerik alanı
 *   y=27..31 : sayfa noktaları (merkez y=29, r=2)
 *
 * NOT: struct tanımları #include'lardan ÖNCE — Arduino IDE önişlemcisi
 *      prototipleri son #include'dan sonraya ekler; struct burada olmazsa
 *      "not declared in this scope" hatası verir.
 */

// ── Struct Tanımları (include'lardan önce olmalı) ────────────────────────────
struct PlantRef {
  float tempMin, tempMax;
  float humMin,  humMax;
  float wTempMin, wTempMax;
  int   tdsMin,   tdsMax;
  float luxMin,   luxMax;
  int   co2Min,   co2Max;
  int   harvestDays;
};

struct Plant {
  int  id;
  char name[32];
  char sowDate[12]; // "YYYY-MM-DD"
  int  harvestDays;
  bool valid;
};

struct Button {
  int  pin;
  bool lastReading;
  bool triggered;
  unsigned long lastDebounce;
};

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <BH1750.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SparkFun_SCD30_Arduino_Library.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// ── Pinler ───────────────────────────────────────────────────────────────────
#define DHT_PIN       5
#define DS18B20_PIN   4
#define TDS_PIN       34
#define BTN_FORWARD   12
#define BTN_OK        13

// ── OLED ─────────────────────────────────────────────────────────────────────
#define SCREEN_W   128
#define SCREEN_H   32
#define OLED_ADDR  0x3C
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);

// ── Sensörler ────────────────────────────────────────────────────────────────
DHT dht(DHT_PIN, DHT22);
BH1750 lightMeter;
OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);
SCD30 scd30;

// ── WiFi & Sunucu ─────────────────────────────────────────────────────────────
const char* SSID     = "";
const char* PASS     = "";
const char* SERVER   = "";

// ── Referans Değerleri ───────────────────────────────────────────────────────
PlantRef getRef(const char* name) {
  if (strncmp(name, "Marul",  5) == 0) return {18,24, 50,70, 18,22, 700,1400,  5000,20000, 800,1200, 45};
  if (strncmp(name, "Fes",    3) == 0) return {20,26, 40,65, 20,24, 700,1400,  5000,30000, 800,1200, 21};
  if (strncmp(name, "Dom",    3) == 0) return {20,28, 50,70, 20,26,1400,3500, 10000,60000, 800,1500, 75};
  if (strncmp(name, "Cil",    3) == 0 ||
      strncmp(name, "\xC3\x87il", 4) == 0) // Ç (UTF-8)
                                       return {15,22, 60,80, 15,20, 700,1400,  5000,20000, 800,1200, 90};
  return {18,26, 40,70, 18,24, 700,1400, 3000,30000, 800,1500, 60};
}

// ── Pixel Art Bitmapleri (16×16, PROGMEM) ───────────────────────────────────
// Her satır 2 byte → 16 satır = 32 byte. MSB önce.

// Marul — yuvarlak yaprak kümesi + sap
static const uint8_t PROGMEM bmp_marul[32] = {
  0x06,0x00, // ....0110 ........
  0x0F,0x00, // ....1111 ........
  0x7F,0x80, // .1111111 1.......
  0xFF,0xC0, // 11111111 11......
  0xFF,0xE0, // 11111111 111.....
  0x7F,0xE0, // .1111111 111.....
  0x7F,0xC0, // .1111111 11......
  0x3F,0x80, // ..111111 1.......
  0x1F,0x00, // ...11111 ........
  0x0E,0x00, // ....1110 ........
  0x04,0x00, // .....100 ........
  0x04,0x00, // .....100 ........
  0x0E,0x00, // ....1110 ........
  0x0E,0x00, // ....1110 ........
  0x04,0x00, // .....100 ........
  0x00,0x00
};

// Fesleğen — ince oval yaprak + ince sap
static const uint8_t PROGMEM bmp_feslegen[32] = {
  0x06,0x00,
  0x1F,0x80,
  0x3F,0xC0,
  0x7F,0xE0,
  0xFF,0xF0,
  0xFF,0xF0,
  0x7F,0xE0,
  0x3F,0xC0,
  0x1F,0x80,
  0x06,0x00,
  0x06,0x00,
  0x06,0x00,
  0x06,0x00,
  0x0F,0x00,
  0x0F,0x00,
  0x06,0x00
};

// Domates — yuvarlak meyve + üstte çanak yapraklar
static const uint8_t PROGMEM bmp_domates[32] = {
  0x00,0x00,
  0x13,0x80, // çanak yaprak uçları
  0x0F,0x00,
  0x1F,0x80,
  0x3F,0xC0,
  0x7F,0xE0,
  0xFF,0xF0,
  0xFF,0xF0,
  0xFF,0xF0,
  0xFF,0xF0,
  0x7F,0xE0,
  0x3F,0xC0,
  0x1F,0x80,
  0x0F,0x00,
  0x06,0x00,
  0x00,0x00
};

// Çilek — konik meyve, tohumlar (boşluklar), üstte yapraklar
static const uint8_t PROGMEM bmp_cilek[32] = {
  0x00,0x00,
  0x1D,0xC0, // yaprak uçları
  0x0F,0x80,
  0x1F,0xC0,
  0x3F,0xE0,
  0x7B,0xF0, // tohum
  0xFF,0xF0,
  0xF7,0xF0, // tohum
  0xFF,0xF0,
  0xFB,0xF0, // tohum
  0x7F,0xE0,
  0x3F,0xC0,
  0x1F,0x80,
  0x0F,0x00,
  0x06,0x00,
  0x00,0x00
};

// Varsayılan — filiz simgesi
static const uint8_t PROGMEM bmp_default[32] = {
  0x00,0x00,
  0x06,0x00,
  0x1F,0x80,
  0x3F,0xC0,
  0x1F,0x80,
  0x06,0x00,
  0x06,0x00,
  0x3F,0xC0,
  0x7F,0x00,
  0x1E,0x00,
  0x06,0x00,
  0x06,0x00,
  0x06,0x00,
  0x0F,0x00,
  0x0F,0x00,
  0x06,0x00
};

const uint8_t* getBitmap(const char* name) {
  if (strncmp(name, "Marul",  5) == 0) return bmp_marul;
  if (strncmp(name, "Fes",    3) == 0) return bmp_feslegen;
  if (strncmp(name, "Dom",    3) == 0) return bmp_domates;
  if (strncmp(name, "Cil",    3) == 0 ||
      strncmp(name, "\xC3\x87il", 4) == 0) return bmp_cilek;
  return bmp_default;
}

// ── Bitki Verisi ─────────────────────────────────────────────────────────────
Plant plants[10];
int   plantCount  = 0;
int   activePlant = 0;

// ── Ekran Durumu ─────────────────────────────────────────────────────────────
// 0=kart, 1=sıcaklık/nem, 2=tds/su, 3=ışık, 4=co2
int currentScreen = 0;
const int TOTAL_SCREENS = 5;

// ── Sensör Verileri ──────────────────────────────────────────────────────────
float airTemp   = 0;
float airHum    = 0;
float waterTemp = 0;
float tds       = 0;
float lux       = 0;
float co2_ppm   = 0;

// ── WiFi & Zamanlama ─────────────────────────────────────────────────────────
bool wifiConnected = false;
unsigned long lastSensorRead = 0;
unsigned long lastDataSend   = 0;
const unsigned long SENSOR_INTERVAL = 2000;
const unsigned long SEND_INTERVAL   = 30000;

// ── Buton (debounce) ─────────────────────────────────────────────────────────
Button btnFwd = {BTN_FORWARD, false, false, 0};
Button btnOk  = {BTN_OK,      false, false, 0};
const unsigned long DEBOUNCE_MS = 50;

// Tuşa bir kez basılınca true döner (yükselen kenar dedektörü)
bool readBtn(Button& b) {
  bool pressed = (digitalRead(b.pin) == LOW);
  if (pressed != b.lastReading) {
    b.lastDebounce = millis();
    b.lastReading  = pressed;
  }
  if ((millis() - b.lastDebounce) >= DEBOUNCE_MS) {
    if (pressed && !b.triggered) {
      b.triggered = true;
      return true;
    }
    if (!pressed) b.triggered = false;
  }
  return false;
}

// ── Tarih Hesaplama ──────────────────────────────────────────────────────────
int daysSince(const char* dateStr) {
  if (!dateStr || strlen(dateStr) < 10) return 0;
  time_t now = time(nullptr);
  if (now < 1000000UL) return 0; // NTP senkronize değil
  struct tm t = {};
  t.tm_year  = atoi(dateStr) - 1900;
  t.tm_mon   = atoi(dateStr + 5) - 1;
  t.tm_mday  = atoi(dateStr + 8);
  t.tm_isdst = -1;
  time_t sowTime = mktime(&t);
  if (sowTime < 0) return 0;
  long diff = (long)((now - sowTime) / 86400L);
  return diff < 0 ? 0 : (int)diff;
}

// ── Aralık Kontrolü ──────────────────────────────────────────────────────────
bool outOfRange(float val, float mn, float mx) {
  return (val < mn || val > mx);
}

// ── Görüntüleme Yardımcıları ──────────────────────────────────────────────────
void drawHeader(const char* title) {
  display.fillRect(0, 0, SCREEN_W, 8, WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(1);
  display.setCursor(2, 1);
  display.print(title);
  display.setTextColor(WHITE);
}

// Sayfa noktaları — y=29, r=2; en fazla 8 nokta sığar
void drawDots(int total, int active) {
  const int dotR = 2;
  const int dotSp = 7;
  int totalW = total * dotSp - (dotSp - dotR * 2);
  int startX = (SCREEN_W - totalW) / 2 + dotR;
  for (int i = 0; i < total; i++) {
    int cx = startX + i * dotSp;
    if (i == active)
      display.fillCircle(cx, 29, dotR, WHITE);
    else
      display.drawCircle(cx, 29, dotR, WHITE);
  }
}

// Küçük WiFi ikonu (sağ üst, başlık barında)
void drawWifiIcon(bool connected) {
  // Başlık barı içinde, siyah çiz
  int x = 118, y = 1;
  display.setTextColor(BLACK);
  display.setCursor(x, y);
  display.print(connected ? "W" : "-");
  display.setTextColor(WHITE);
}

// ── Ekranlar ──────────────────────────────────────────────────────────────────

void drawSplash() {
  for (int w = 0; w <= 120; w += 5) {
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(2);
    // "HydroFiliz" = 10 × 12px = 120px → x=4
    display.setCursor(4, 4);
    display.print("HydroFiliz");
    display.setTextSize(1);
    display.drawRect(4, 24, 120, 5, WHITE);
    if (w > 0) display.fillRect(4, 24, w, 5, WHITE);
    display.display();
    delay(40);
  }
  delay(400);
}

void drawCardScreen() {
  display.clearDisplay();

  if (plantCount == 0) {
    drawHeader("HydroFiliz");
    display.setCursor(4, 12);
    display.print("Bitki bulunamadi");
    display.setCursor(4, 20);
    display.print("Sunucu kontrol et");
    drawDots(TOTAL_SCREENS, 0);
    display.display();
    return;
  }

  Plant& p   = plants[activePlant];
  PlantRef r = getRef(p.name);
  int harvest = (p.harvestDays > 0) ? p.harvestDays : r.harvestDays;
  int days    = daysSince(p.sowDate);
  if (days > harvest) days = harvest;

  // Başlık barı
  display.fillRect(0, 0, SCREEN_W, 8, WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(1);
  display.setCursor(2, 1);
  char hdr[24];
  snprintf(hdr, sizeof(hdr), "%s  %d/%d", p.name, activePlant + 1, plantCount);
  display.print(hdr);
  // WiFi durumu (sağda)
  display.setCursor(118, 1);
  display.print(wifiConnected ? "W" : "-");
  display.setTextColor(WHITE);

  // Pixel art ikon — x=2, y=8 (16×16)
  display.drawBitmap(2, 8, getBitmap(p.name), 16, 16, WHITE);

  // Gün bilgisi — x=22, y=9
  display.setTextSize(1);
  display.setCursor(22, 9);
  char dayBuf[20];
  if (time(nullptr) > 1000000UL)
    snprintf(dayBuf, sizeof(dayBuf), "Gun:%d/%d", days, harvest);
  else
    snprintf(dayBuf, sizeof(dayBuf), "Hasat:%dgn", harvest);
  display.print(dayBuf);

  // İlerleme çubuğu — x=22..124, y=18..21 (genişlik=103)
  int barW = (harvest > 0) ? min(days * 103 / harvest, 103) : 0;
  display.drawRect(22, 18, 103, 4, WHITE);
  if (barW > 0) display.fillRect(22, 18, barW, 4, WHITE);

  // Uyarı göstergesi — x=22, y=23
  bool warn = outOfRange(airTemp,   r.tempMin,  r.tempMax)  ||
              outOfRange(airHum,    r.humMin,   r.humMax)   ||
              outOfRange(waterTemp, r.wTempMin, r.wTempMax) ||
              outOfRange(tds,       r.tdsMin,   r.tdsMax)   ||
              outOfRange(lux,       r.luxMin,   r.luxMax)   ||
              outOfRange(co2_ppm,   r.co2Min,   r.co2Max);
  display.setCursor(22, 23);
  display.print(warn ? "! UYARI" : "+ Normal");

  drawDots(TOTAL_SCREENS, 0);
  display.display();
}

void drawTempHumScreen() {
  display.clearDisplay();
  drawHeader("Sicaklik & Nem");

  PlantRef r = (plantCount > 0) ? getRef(plants[activePlant].name) : getRef("");
  bool tWarn = outOfRange(airTemp, r.tempMin, r.tempMax);
  bool hWarn = outOfRange(airHum,  r.humMin,  r.humMax);

  char buf[22];
  display.setTextSize(1);

  // Satır 1 — y=10
  display.setCursor(2, 10);
  if (tWarn)
    snprintf(buf, sizeof(buf), "!T:%.1fC[%.0f-%.0f]", airTemp, r.tempMin, r.tempMax);
  else
    snprintf(buf, sizeof(buf), "+T: %.1f C", airTemp);
  buf[21] = '\0';
  display.print(buf);

  // Satır 2 — y=19
  display.setCursor(2, 19);
  if (hWarn)
    snprintf(buf, sizeof(buf), "!H:%.0f%%[%.0f-%.0f]", airHum, r.humMin, r.humMax);
  else
    snprintf(buf, sizeof(buf), "+H: %.0f %%", airHum);
  buf[21] = '\0';
  display.print(buf);

  drawDots(TOTAL_SCREENS, 1);
  display.display();
}

void drawTdsWaterScreen() {
  display.clearDisplay();
  drawHeader("TDS & Su Sicakl.");

  PlantRef r = (plantCount > 0) ? getRef(plants[activePlant].name) : getRef("");
  bool tdsWarn  = outOfRange(tds,       r.tdsMin,   r.tdsMax);
  bool wtWarn   = outOfRange(waterTemp, r.wTempMin, r.wTempMax);

  char buf[22];
  display.setTextSize(1);

  display.setCursor(2, 10);
  if (tdsWarn)
    snprintf(buf, sizeof(buf), "!TDS:%.0f[%d-%d]", tds, r.tdsMin, r.tdsMax);
  else
    snprintf(buf, sizeof(buf), "+TDS: %.0f ppm", tds);
  buf[21] = '\0';
  display.print(buf);

  display.setCursor(2, 19);
  if (wtWarn)
    snprintf(buf, sizeof(buf), "!Su:%.1f[%.0f-%.0f]", waterTemp, r.wTempMin, r.wTempMax);
  else
    snprintf(buf, sizeof(buf), "+Su: %.1f C", waterTemp);
  buf[21] = '\0';
  display.print(buf);

  drawDots(TOTAL_SCREENS, 2);
  display.display();
}

void drawLightScreen() {
  display.clearDisplay();
  drawHeader("Isik");

  PlantRef r = (plantCount > 0) ? getRef(plants[activePlant].name) : getRef("");
  bool luxWarn = outOfRange(lux, r.luxMin, r.luxMax);

  char buf[22];
  display.setTextSize(1);

  display.setCursor(2, 10);
  if (luxWarn)
    snprintf(buf, sizeof(buf), "!%.0flux[%.0f-%.0f]", lux, r.luxMin, r.luxMax);
  else
    snprintf(buf, sizeof(buf), "+%.0f lux", lux);
  buf[21] = '\0';
  display.print(buf);

  display.setCursor(2, 19);
  snprintf(buf, sizeof(buf), "Ref:%.0f-%.0flux", r.luxMin, r.luxMax);
  buf[21] = '\0';
  display.print(buf);

  drawDots(TOTAL_SCREENS, 3);
  display.display();
}

void drawCO2Screen() {
  display.clearDisplay();
  drawHeader("CO2");

  PlantRef r = (plantCount > 0) ? getRef(plants[activePlant].name) : getRef("");
  bool co2Warn = outOfRange(co2_ppm, r.co2Min, r.co2Max);

  char buf[22];
  display.setTextSize(1);

  display.setCursor(2, 10);
  if (co2Warn)
    snprintf(buf, sizeof(buf), "!CO2:%.0fppm", co2_ppm);
  else
    snprintf(buf, sizeof(buf), "+CO2: %.0f ppm", co2_ppm);
  buf[21] = '\0';
  display.print(buf);

  display.setCursor(2, 19);
  snprintf(buf, sizeof(buf), "Ref:%d-%dppm", r.co2Min, r.co2Max);
  buf[21] = '\0';
  display.print(buf);

  drawDots(TOTAL_SCREENS, 4);
  display.display();
}

void drawCurrentScreen() {
  switch (currentScreen) {
    case 0: drawCardScreen();    break;
    case 1: drawTempHumScreen(); break;
    case 2: drawTdsWaterScreen();break;
    case 3: drawLightScreen();   break;
    case 4: drawCO2Screen();     break;
  }
}

// ── Sensör Okuma ─────────────────────────────────────────────────────────────
void readSensors() {
  // DHT22
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) airTemp = t;
  if (!isnan(h)) airHum  = h;

  // DS18B20
  ds18b20.requestTemperatures();
  float wt = ds18b20.getTempCByIndex(0);
  if (wt != DEVICE_DISCONNECTED_C) waterTemp = wt;

  // BH1750
  float lv = lightMeter.readLightLevel();
  if (lv >= 0) lux = lv;

  // TDS — ESP32 ADC 12-bit (0..4095) → 0..3.3V
  int raw = analogRead(TDS_PIN);
  float v = raw * 3.3f / 4095.0f;
  // Standart TDS polinom (25°C referanslı)
  tds = (133.42f * v * v * v - 255.86f * v * v + 857.39f * v) * 0.5f;
  if (tds < 0) tds = 0;

  // SCD30
  if (scd30.dataAvailable()) {
    float c = scd30.getCO2();
    if (c > 0) co2_ppm = c;
  }
}

// ── Sunucu: Bitki Listesi Al ─────────────────────────────────────────────────
void fetchPlants() {
  if (!wifiConnected) return;

  HTTPClient http;
  String url = String(SERVER) + "/bitkiler";
  http.begin(url);
  http.setTimeout(5000);
  int code = http.GET();

  if (code == 200) {
    String body = http.getString();
    StaticJsonDocument<2048> doc;
    if (!deserializeJson(doc, body)) {
      JsonArray arr = doc.as<JsonArray>();
      int cnt = 0;
      for (JsonObject obj : arr) {
        if (cnt >= 10) break;
        Plant& p = plants[cnt];
        p.id = obj["id"] | 0;
        strlcpy(p.name,    obj["ad"]          | "Bitki",      sizeof(p.name));
        strlcpy(p.sowDate, obj["ekim_tarihi"] | "2026-01-01", sizeof(p.sowDate));
        p.harvestDays = obj["hasat_gun"] | 0;
        p.valid = true;
        cnt++;
      }
      if (cnt > 0) {
        plantCount = cnt;
        if (activePlant >= plantCount) activePlant = 0;
      }
    }
  }
  http.end();
}

// ── Sunucu: Veri Gönder ───────────────────────────────────────────────────────
void sendData() {
  if (!wifiConnected || plantCount == 0) return;

  HTTPClient http;
  String url = String(SERVER) + "/veri";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);

  StaticJsonDocument<256> doc;
  doc["bitki_id"] = plants[activePlant].id;
  doc["sicaklik"] = round(airTemp   * 10) / 10.0;
  doc["nem"]      = round(airHum    * 10) / 10.0;
  doc["su_sicak"] = round(waterTemp * 10) / 10.0;
  doc["tds"]      = (int)tds;
  doc["isik"]     = (int)lux;
  doc["co2"]      = (int)co2_ppm;

  String body;
  serializeJson(doc, body);
  http.POST(body);
  http.end();
}

// ── WiFi Bağlantısı ───────────────────────────────────────────────────────────
void connectWiFi() {
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(2, 2);
  display.print("WiFi baglaniyor...");
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASS);

  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    display.drawRect(4, 14, 120, 8, WHITE);
    display.fillRect(4, 14, i * 6, 8, WHITE);
    display.display();
  }
  wifiConnected = (WiFi.status() == WL_CONNECTED);
}

// ── Buton Navigasyonu ─────────────────────────────────────────────────────────
void handleButtons() {
  bool fwd = readBtn(btnFwd);
  bool ok  = readBtn(btnOk);

  if (fwd) {
    // İLERİ: sonraki ekrana geç, en sonda karta dön
    currentScreen = (currentScreen + 1) % TOTAL_SCREENS;
  }

  if (ok) {
    if (currentScreen == 0) {
      // Kart ekranı: sonraki bitkiye geç
      if (plantCount > 0)
        activePlant = (activePlant + 1) % plantCount;
    } else {
      // Sensör ekranı: bir önceki ekrana dön
      currentScreen--;
      // screen 0'a (karta) düşebilir — bu istenen davranış
    }
  }
}

// ── WiFi Yeniden Bağlanma ─────────────────────────────────────────────────────
void checkWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    WiFi.reconnect();
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 10) {
      delay(500);
      tries++;
    }
  }
  wifiConnected = (WiFi.status() == WL_CONNECTED);
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  pinMode(BTN_FORWARD, INPUT_PULLUP);
  pinMode(BTN_OK,      INPUT_PULLUP);

  Wire.begin(21, 22); // SDA=21, SCL=22

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("SSD1306 baslatilamadi!");
    for (;;) delay(100);
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.display();

  // Açılış animasyonu
  drawSplash();

  // DHT22
  dht.begin();

  // BH1750
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

  // DS18B20
  ds18b20.begin();

  // SCD30
  if (!scd30.begin()) {
    Serial.println("SCD30 bulunamadi, devam ediliyor.");
  }

  // WiFi
  connectWiFi();

  if (wifiConnected) {
    // NTP — Türkiye UTC+3
    configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    // NTP senkronizasyonu için kısa bekle
    unsigned long ntpStart = millis();
    while (time(nullptr) < 1000000UL && millis() - ntpStart < 5000) {
      delay(200);
    }
    fetchPlants();
  }

  // İlk sensör okuması
  readSensors();
  lastSensorRead = millis();
  lastDataSend   = millis();
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  handleButtons();

  // Sensör — her 2 saniye
  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = now;
    readSensors();
  }

  // Veri gönder & bitki listesini güncelle — her 30 saniye
  if (now - lastDataSend >= SEND_INTERVAL) {
    lastDataSend = now;
    checkWiFi();
    if (wifiConnected) {
      fetchPlants();
      sendData();
    }
  }

  drawCurrentScreen();
  delay(50); // ~20 fps
}

# 🌱 HydroFiliz

**Topraksız tarımda bitki sağlığını gerçek zamanlı izleyen ESP32 tabanlı sensör sistemi.**

HydroFiliz; hidroponik ortamlarda yetiştirilen bitkilerin ortam koşullarını sürekli ölçerek seçilen bitkinin ideal değer aralıklarıyla karşılaştırır. Veriler hem OLED ekranda anlık gösterilir hem de WiFi üzerinden web dashboard'a aktarılır.

---

## 🔧 Donanım

| Bileşen | Görev |
|---|---|
| ESP32 | Ana mikrodenetleyici, WiFi |
| DHT22 | Hava sıcaklığı & nem |
| BH1750 | Işık yoğunluğu (lux) |
| DS18B20 | Su sıcaklığı |
| TDS Sensörü | Besin çözeltisi yoğunluğu (ppm) |
| SCD30 | CO₂ seviyesi (ppm) |
| SSD1306 OLED (128×32) | Anlık değer ekranı |
| 2 Buton | Ekranlar arası gezinme |

**Pin Bağlantıları:**

| Pin | Sensör |
|---|---|
| GPIO 5 | DHT22 data |
| GPIO 4 | DS18B20 data |
| GPIO 34 | TDS analog çıkış |
| GPIO 12 | Buton İleri |
| GPIO 13 | Buton Tamam |
| GPIO 21/22 | I²C (SDA/SCL) — BH1750, SSD1306, SCD30 |

---

## 💻 Arduino Kurulumu

Arduino IDE'de şu kütüphaneleri yükle (Tools → Manage Libraries):

- DHT sensor library
- BH1750
- Adafruit SSD1306 + Adafruit GFX
- OneWire + DallasTemperature
- SparkFun SCD30 Arduino Library
- ArduinoJson

`hydrofiliz_esp32.ino` ve `hydrofiliz_wifi.ino` dosyalarını **aynı klasöre** koy. Arduino IDE ikisini birlikte derler.

`hydrofiliz_wifi.ino` içinde şu 3 satırı düzenle:

```cpp
#define WIFI_SSID   "HOTSPOT_ADINIZ"
#define WIFI_PASS   "HOTSPOT_SIFRESI"
#define SUNUCU_IP   "192.168.X.X"   // bilgisayarının IP'si (ipconfig)
```

---

## 🌿 Desteklenen Bitkiler

Her bitki için sıcaklık, nem, TDS, su sıcaklığı, ışık ve CO₂ referans aralıkları tanımlanmıştır. Ölçülen değer aralık dışına çıktığında OLED'de `(!)` uyarısı gösterilir.

| Bitki | TDS (ppm) | Hava Sıc. (°C) | Işık (lux) | Hasat |
|---|---|---|---|---|
| Marul | 560 – 840 | 16 – 24 | 2000 – 4000 | 28 gün |
| Fesleğen | 700 – 1120 | 20 – 28 | 3000 – 6000 | 21 gün |
| Domates | 1400 – 3500 | 20 – 27 | 5000 – 8000 | 56 gün |
| Çilek | 1260 – 1540 | 15 – 25 | 4000 – 7000 | 42 gün |

---

## 📊 Web Dashboard

Sensör verileri WiFi üzerinden Flask sunucusuna gönderilir, `dashboard.html` ile tarayıcıdan takip edilebilir.

Sunucuyu başlatmak için:

```bash
pip install flask
python serverfiliz.py
```

Ardından `http://localhost:5000` adresini aç. Veritabanı ilk çalıştırmada otomatik oluşur.

---

> **HydroFiliz**, topraksız tarıma ilgi duyan herkesin kendi düşük maliyetli izleme sistemini kurabilmesi amacıyla geliştirilmiştir.

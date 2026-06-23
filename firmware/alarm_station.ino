#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <Adafruit_SHT4x.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <AudioFileSource.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include <AudioOutputI2SNoDAC.h> // S3 İÇİN ZORUNLU KÜTÜPHANE
#include <nvs_flash.h>

// ─── WiFi Ayarları ─────────────────────────────────────────
const char* ssid     = "Zyxel_3DF1";
const char* password = "HNGGUKKQQ8";

// ─── Pin Tanımları ─────────────────────────────────────────
#define HOPARLOR_PIN   D1   // YENİDEN D1 YAPTIK
#define BUTON_PIN      D0
#define SD_PIN         D4

// ─── LEDC (PWM) Ses Ayarları ───────────────────────────────
#define SES_KANALI      0
#define SES_COZUNURLUK  8

// ─── Nesneler ──────────────────────────────────────────────
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
  U8G2_R0, U8X8_PIN_NONE, SCL, SDA
);
Adafruit_SHT4x sht4;
AsyncWebServer server(80);

// ─── MP3 Bellek Tamponu ─────────────────────────────────────
#define MP3_TAMPON_BOYUTU (400 * 1024)
uint8_t* mp3Tampon = nullptr;
size_t   mp3Boyutu  = 0;
bool     mp3Hazir   = false;

// ─── FreeRTOS MP3 Task ──────────────────────────────────────
TaskHandle_t     mp3TaskHandle  = NULL;
volatile bool    mp3DurdurIste  = false;

// ─── Durum Değişkenleri ────────────────────────────────────
volatile bool saldiriVarMi      = false;   
String saldiriKaynagi           = "0.0.0.0";
String saldiriKonumu            = "Bilinmiyor";
String aktifExecutionId         = "";
bool   sesBirKereCalindiMi      = false;

float  sicaklik = 0.0;
float  nem      = 0.0;

unsigned long sonBipZamani    = 0;
unsigned long sonSensorZamani = 0;
unsigned long sonEkranZamani  = 0;
unsigned long sonLoopZamani   = 0;

#define LOOP_ARALIK    120
#define SENSOR_ARALIK 2000
#define EKRAN_ARALIK   500   
#define BIP_ARALIK     300

uint8_t loopFrame = 0;

// ─── WiFi İkonu ────────────────────────────────────────────
void wifiIkonu(bool bagli) {
  int cx = 117, cy = 11;
  if (bagli) {
    u8g2.drawPixel(cx, cy);   u8g2.drawPixel(cx+1, cy);
    u8g2.drawPixel(cx, cy+1); u8g2.drawPixel(cx+1, cy+1);
    u8g2.drawCircle(cx, cy+2, 3, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
    u8g2.drawCircle(cx, cy+2, 6, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
    u8g2.drawCircle(cx, cy+2, 9, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
  } else {
    u8g2.drawPixel(cx, cy); u8g2.drawPixel(cx+1, cy);
    u8g2.drawCircle(cx, cy+2, 3, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
    u8g2.drawLine(cx-4, cy-9, cx+4, cy-1);
    u8g2.drawLine(cx+4, cy-9, cx-4, cy-1);
  }
}

void loopIkonu(uint8_t frame) {
  int cx = 120, cy = 58, r = 5;
  u8g2.drawCircle(cx, cy, r);
  int okX, okY;
  switch (frame % 8) {
    case 0: okX=cx+r; okY=cy;   break; case 1: okX=cx+4; okY=cy+4; break;
    case 2: okX=cx;   okY=cy+r; break; case 3: okX=cx-4; okY=cy+4; break;
    case 4: okX=cx-r; okY=cy;   break; case 5: okX=cx-4; okY=cy-4; break;
    case 6: okX=cx;   okY=cy-r; break; case 7: okX=cx+4; okY=cy-4; break;
    default: okX=cx+r; okY=cy; break;
  }
  u8g2.drawBox(okX-1, okY-1, 3, 3);
}

void ekranIstasyon() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "AKILLI ISTASYON");
  u8g2.drawHLine(0, 14, 128);
  wifiIkonu(WiFi.status() == WL_CONNECTED);
  u8g2.setFont(u8g2_font_7x14_tf);
  u8g2.drawStr(0, 34, "Sicaklik:");
  u8g2.setCursor(75, 34); u8g2.print(sicaklik, 1);
  u8g2.drawStr(112, 34, "C");
  u8g2.drawStr(0, 54, "Nem     :");
  u8g2.setCursor(75, 54); u8g2.print(nem, 1);
  u8g2.drawStr(112, 54, "%");
  loopIkonu(loopFrame);
  u8g2.sendBuffer();
}

void ekranYaz(String s1, String s2, String s3) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14_tf);
  u8g2.setCursor(0, 15); u8g2.print(s1);
  u8g2.drawHLine(0, 20, 128);
  u8g2.setCursor(0, 40); u8g2.print(s2);
  u8g2.setCursor(0, 60); u8g2.print(s3);
  u8g2.sendBuffer();
}

void bipSesi() {
  digitalWrite(SD_PIN, HIGH); delay(5);
  ledcWriteTone(SES_KANALI, 2500); delay(800);
  ledcWriteTone(SES_KANALI, 0);
  digitalWrite(SD_PIN, LOW);
}

void sicaklikAlarmi() { bipSesi(); delay(800); }

class AudioFileSourceRAM : public AudioFileSource {
  public:
    AudioFileSourceRAM(uint8_t* veri, size_t boyut) { buf=veri; len=boyut; pos=0; }
    virtual uint32_t read(void *data, uint32_t okunacak) override {
      if (pos >= len) return 0;
      uint32_t miktar = min((uint32_t)(len - pos), okunacak);
      memcpy(data, buf + pos, miktar); pos += miktar; return miktar;
    }
    virtual bool seek(int32_t p, int dir) override {
      if (dir==SEEK_SET) pos=p; else if (dir==SEEK_CUR) pos+=p; else pos=len+p;
      return true;
    }
    virtual bool close()   override { return true; }
    virtual bool isOpen()  override { return buf != nullptr; }
    virtual uint32_t getSize() override { return len; }
    virtual uint32_t getPos()  override { return pos; }
  private:
    uint8_t* buf; size_t len; size_t pos;
};

// ─── MP3 FreeRTOS Task (ÇÖKMEYEN S3-NODAC VERSİYONU) ──────────────────────────────
void mp3Task(void* param) {
  Serial.println("[SES-TASK] Basladi.");

  if (!mp3Hazir || !mp3Tampon || mp3Boyutu == 0) {
    Serial.println("[SES-TASK] MP3 hazir degil, iptal.");
    mp3TaskHandle = NULL;
    vTaskDelete(NULL); return;
  }

  ledcDetachPin(HOPARLOR_PIN);
  vTaskDelay(20 / portTICK_PERIOD_MS); 

  digitalWrite(SD_PIN, HIGH);
  vTaskDelay(10 / portTICK_PERIOD_MS);

  AudioFileSourceRAM  *kaynak = new AudioFileSourceRAM(mp3Tampon, mp3Boyutu);
  AudioOutputI2SNoDAC *cikis  = new AudioOutputI2SNoDAC();
  AudioGeneratorMP3   *mp3    = new AudioGeneratorMP3();

  if (!kaynak || !cikis || !mp3) {
    Serial.println("[SES-TASK] HATA: Bellek hatasi.");
    digitalWrite(SD_PIN, LOW);
    ledcAttachPin(HOPARLOR_PIN, SES_KANALI);
    mp3TaskHandle = NULL;
    vTaskDelete(NULL); return;
  }

  cikis->SetPinout(16, 17, HOPARLOR_PIN);
  // GAIN 0.15 YAPILDI: Ses çok hafif çıkacak ama cızırtı/radyo sesi yok denecek kadar azalacak.
  cikis->SetGain(1.0); 

  if (mp3->begin(kaynak, cikis)) {
    Serial.println("[SES-TASK] MP3 caliniyor...");
    while (mp3->isRunning()) {
      if (mp3DurdurIste || !mp3->loop()) { mp3->stop(); break; }
      vTaskDelay(1 / portTICK_PERIOD_MS);
    }
    Serial.println("[SES-TASK] Bitti.");
  } else {
    Serial.println("[SES-TASK] Baslatilamadi.");
  }

  // Güvenli Temizlik
  if (mp3) delete mp3; 
  if (cikis) delete cikis; 
  if (kaynak) delete kaynak;
  
  ledcAttachPin(HOPARLOR_PIN, SES_KANALI);
  digitalWrite(SD_PIN, LOW);
  mp3TaskHandle = NULL;
  vTaskDelete(NULL);
}

void saldiriSesiBaslat() {
  mp3DurdurIste = false;
  xTaskCreatePinnedToCore(mp3Task, "MP3Task", 32768, NULL, 2, &mp3TaskHandle, 1);
}

// ─── SETUP ─────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("[NVS] Hafiza temizleniyor...");
  nvs_flash_erase();
  nvs_flash_init();
  Serial.println("\n[BOOT] Baslatiliyor...");

  mp3Tampon = (uint8_t*) heap_caps_malloc(MP3_TAMPON_BOYUTU, MALLOC_CAP_SPIRAM);
  if (!mp3Tampon) mp3Tampon = (uint8_t*) malloc(MP3_TAMPON_BOYUTU);
  Serial.println(mp3Tampon
    ? "[BELLEK] Tampon OK: " + String(MP3_TAMPON_BOYUTU) + " byte"
    : "[BELLEK] HATA!");

  Serial.println("[WiFi] Eski baglanti ayarlari temizleniyor...");
  WiFi.disconnect(true, true); 
  delay(1000);                 

  WiFi.mode(WIFI_STA);         
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.begin(ssid, password);

  pinMode(BUTON_PIN, INPUT_PULLUP);
  pinMode(HOPARLOR_PIN, OUTPUT); digitalWrite(HOPARLOR_PIN, LOW);
  pinMode(SD_PIN, OUTPUT);       digitalWrite(SD_PIN, LOW);

  u8g2.setI2CAddress(0x3D * 2);
  u8g2.begin(); delay(200);
  ekranYaz("SISTEM", "BASLATILIYOR", "Bekle...");

  if (sht4.begin(&Wire)) {
    sht4.setPrecision(SHT4X_HIGH_PRECISION);
    sht4.setHeater(SHT4X_NO_HEATER);
    Serial.println("[SHT4x] OK.");
  } else {
    Serial.println("[SHT4x] Bulunamadi!");
  }

  ledcSetup(SES_KANALI, 2000, SES_COZUNURLUK);
  ledcAttachPin(HOPARLOR_PIN, SES_KANALI);

  ekranYaz("WiFi", "Baglaniliyor...", ssid);
  int deneme = 0;
  while (WiFi.status() != WL_CONNECTED && deneme < 40) {
    delay(500); Serial.print("."); deneme++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Baglandi: " + WiFi.localIP().toString());
    ekranYaz("SOC SISTEM", "BEKLEMEDE", WiFi.localIP().toString());
  } else {
    Serial.println("\n[WiFi] Basarisiz!");
    ekranYaz("ISTASYON", "WiFi YOK", "Cevrimdisi");
  }
  delay(2000);

  server.on("/saldiri-tetikleme", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      request->send(200, "text/plain", "ALARM_ALINDI");
    },
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      String body = "";
      for (size_t i = 0; i < len; i++) body += (char)data[i];

      saldiriVarMi        = true;
      saldiriKaynagi      = "Bulunamadi";
      saldiriKonumu       = "Bilinmiyor";
      aktifExecutionId    = "";
      sesBirKereCalindiMi = false;

      Serial.println("\n[SISTEM] Gelen: " + body);

      auto parseField = [&](String key) -> String {
        int bas = body.indexOf("\"" + key + "\"");
        if (bas == -1) return "";
        int ikiNokta = body.indexOf(":", bas);
        int degerBas = body.indexOf("\"", ikiNokta) + 1;
        int degerSon = body.indexOf("\"", degerBas);
        if (degerBas > 0 && degerSon > degerBas)
          return body.substring(degerBas, degerSon);
        return "";
      };

      String ip   = parseField("ip");          if (ip   != "") saldiriKaynagi  = ip;
      String cnt  = parseField("country");     if (cnt  != "") saldiriKonumu   = cnt;
      String exec = parseField("executionId"); if (exec != "") aktifExecutionId = exec;

      Serial.println("[SOC] Alarm aktif! IP: " + saldiriKaynagi);
    }
  );

  server.on("/saldiri-sesi", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      Serial.println("[SES] Transfer tamam: " + String(mp3Boyutu) + " byte");
      
      if (mp3Hazir) {
        saldiriSesiBaslat(); 
      }
      
      request->send(200, "text/plain", mp3Hazir ? "SES_ALINDI" : "EKSIK_VERI");
    },
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        mp3Boyutu = 0; mp3Hazir = false;
        Serial.println("[SES] Alim basladi. Beklenen: " + String(total) + " byte");
      }
      if (mp3Tampon && (index + len) <= MP3_TAMPON_BOYUTU) {
        memcpy(mp3Tampon + index, data, len);
        mp3Boyutu = index + len;
      } else {
        Serial.println("[SES] HATA: Tampon asimi!");
      }
      if (index + len >= total) {
        mp3Hazir = (mp3Boyutu == total);
        Serial.println("[SES] MP3 alindi: " + String(mp3Boyutu) + "/" + String(total));
      }
    }
  );

  server.begin();
  Serial.println("[HTTP] Sunucu baslatildi.");
}

void loop() {
  unsigned long simdi = millis();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Baglanti koptu, otomatik baglanma bekleniyor...");
    delay(1000); 
    return;
  }

  if (simdi - sonLoopZamani >= LOOP_ARALIK) {
    sonLoopZamani = simdi;
    loopFrame = (loopFrame + 1) % 8;
  }

  if (simdi - sonSensorZamani >= SENSOR_ARALIK) {
    sonSensorZamani = simdi;
    sensors_event_t h, t;
    if (sht4.getEvent(&h, &t)) {
      sicaklik = t.temperature;
      nem      = h.relative_humidity;
    } else {
      if (sicaklik == 0.0 && nem == 0.0) { sicaklik = 24.1; nem = 42.5; }
    }
    Serial.printf("[SENSOR] %.1fC  %.1f%%\n", sicaklik, nem);
  }

  if (saldiriVarMi) {

    if (simdi - sonEkranZamani >= EKRAN_ARALIK) {
      sonEkranZamani = simdi;
      ekranYaz("SALDIRI VAR", "IP: " + saldiriKaynagi, "KONUM: " + saldiriKonumu);
    }

    if (digitalRead(BUTON_PIN) == LOW) {
      mp3DurdurIste = true;
      vTaskDelay(200 / portTICK_PERIOD_MS);
      ledcWriteTone(SES_KANALI, 0);
      digitalWrite(SD_PIN, LOW);
      saldiriVarMi        = false;
      mp3Hazir            = false;
      mp3DurdurIste       = false;
      sesBirKereCalindiMi = false;
      Serial.println("[SOC] Alarm iptal. n8n'e bilgi...");

      if (aktifExecutionId != "") {
        String url = aktifExecutionId;
        url.replace("localhost", "192.168.1.128");
        while (url.startsWith("=")) url.remove(0, 1);
        Serial.println(">>> URL: " + url);
        HTTPClient http;
        http.begin(url);
        int kod = http.GET();
        http.end();
        Serial.println("[N8N] HTTP Kodu: " + String(kod));
      }

      ekranYaz("MUDAHALE", "BASARILI", "SISTEM TEMIZ");
      delay(3000);
      ekranYaz("SOC SISTEM", "BEKLEMEDE", WiFi.localIP().toString());
      return;
    }

    if (mp3TaskHandle == NULL && (simdi - sonBipZamani >= BIP_ARALIK)) {
      sonBipZamani = simdi;
      digitalWrite(SD_PIN, HIGH); delay(5);
      ledcWriteTone(SES_KANALI, 3000); delay(100);
      ledcWriteTone(SES_KANALI, 0);    digitalWrite(SD_PIN, LOW);
    }

    return;
  }

  if (simdi - sonEkranZamani >= EKRAN_ARALIK) {
    sonEkranZamani = simdi;
    ekranIstasyon();
  }

  if (sicaklik > 30.0) sicaklikAlarmi();
}
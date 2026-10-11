

#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ===================== PIN =================================
#define SOIL_PIN   A0
#define DHT_PIN    2
#define DHT_TYPE   DHT22
#define RELAY_PIN  7

DHT dht(DHT_PIN, DHT_TYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ===================== KALIBRASI SENSOR TANAH ==============
const int SOIL_DRY = 850;   // ADC saat kering (udara)
const int SOIL_WET = 350;   // ADC saat basah (air)

// ===================== VARIABEL GLOBAL =====================
float soilPercent  = 0;
float temperature  = 0;
float humidity     = 0;
float pumpDuration = 0;

unsigned long lastRead = 0;
const unsigned long READ_INTERVAL = 3000;

// ============================================================
//         FUNGSI KEANGGOTAAN (TRIANGLE & TRAPEZOID)
// ============================================================

float segitiga(float x, float a, float b, float c) {
  if (x <= a || x >= c) return 0.0;
  if (x == b) return 1.0;
  if (x < b)  return (x - a) / (b - a);
  return (c - x) / (c - b);
}

float trapesium(float x, float a, float b, float c, float d) {
  if (x <= a || x >= d) return 0.0;
  if (x >= b && x <= c) return 1.0;
  if (x < b)  return (x - a) / (b - a);
  return (d - x) / (d - c);
}

// ---------- KELEMBAPAN TANAH ----------
float soilKering(float x) { return trapesium(x, 0,   0,  30, 45); }
float soilSedang(float x) { return segitiga (x, 30, 50, 70);       }
float soilBasah (float x) { return trapesium(x, 55, 70, 100, 100); }

// ---------- SUHU UDARA ----------
float suhuDingin(float x) { return trapesium(x, 0,   0,  20, 27); }
float suhuNormal(float x) { return segitiga (x, 22, 29, 36);      }
float suhuPanas (float x) { return trapesium(x, 32, 38, 50, 50);  }

// ============================================================
//   FUNGSI MIN (NAMA DIGANTI AGAR TIDAK KONFLIK DENGAN BAWAAN)
// ============================================================
float minimum(float a, float b) { return (a < b) ? a : b; }

// ============================================================
//               FUZZY SUGENO ORDE-0 (KONSTANTA)
// ============================================================
// Output tiap rule berupa konstanta z (durasi detik)
// Rumus: z* = Σ(αᵢ × zᵢ) / Σ(αᵢ)

float hitungDurasiSugeno0(float kelembapan, float suhu) {

  // ---------- 1. FUZZIFIKASI ----------
  float sKering = soilKering(kelembapan);
  float sSedang = soilSedang(kelembapan);
  float sBasah  = soilBasah (kelembapan);

  float tDingin = suhuDingin(suhu);
  float tNormal = suhuNormal(suhu);
  float tPanas  = suhuPanas (suhu);

  // ---------- 2. FIRING STRENGTH (α) tiap rule ----------
  // z = konstanta output (detik)
  float a1 = minimum(sKering, tDingin);  float z1 = 6.0;   // Kering & Dingin
  float a2 = minimum(sKering, tNormal);  float z2 = 8.0;   // Kering & Normal
  float a3 = minimum(sKering, tPanas );  float z3 = 9.5;   // Kering & Panas

  float a4 = minimum(sSedang, tDingin);  float z4 = 2.0;   // Sedang & Dingin
  float a5 = minimum(sSedang, tNormal);  float z5 = 4.5;   // Sedang & Normal
  float a6 = minimum(sSedang, tPanas );  float z6 = 6.5;   // Sedang & Panas

  float a7 = minimum(sBasah,  tDingin);  float z7 = 0.0;   // Basah & Dingin
  float a8 = minimum(sBasah,  tNormal);  float z8 = 0.0;   // Basah & Normal
  float a9 = minimum(sBasah,  tPanas );  float z9 = 1.5;   // Basah & Panas

  // ---------- 3. DEFUZZIFIKASI WEIGHTED AVERAGE ----------
  float num = a1*z1 + a2*z2 + a3*z3
            + a4*z4 + a5*z5 + a6*z6
            + a7*z7 + a8*z8 + a9*z9;

  float den = a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9;

  if (den < 0.0001) return 0.0;
  return num / den;
}

// ============================================================
//               BACA SENSOR
// ============================================================
float bacaKelembapanTanah() {
  int adc = analogRead(SOIL_PIN);
  float persen = map(adc, SOIL_DRY, SOIL_WET, 0, 100);
  return constrain(persen, 0, 100);
}

// ============================================================
//               SETUP
// ============================================================
void setup() {
  Serial.begin(9600);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);   // Pompa OFF (active HIGH)

  dht.begin();

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("Penyiram Fuzzy");
  lcd.setCursor(0, 1); lcd.print("Sugeno Orde-0");
  delay(2000);
  lcd.clear();

  Serial.println("=== Fuzzy Sugeno Penyiram Tanaman ===");
}

// ============================================================
//               LOOP UTAMA
// ============================================================
void loop() {
  if (millis() - lastRead >= READ_INTERVAL) {
    lastRead = millis();

    // --- Baca sensor ---
    soilPercent = bacaKelembapanTanah();
    temperature = dht.readTemperature();
    humidity    = dht.readHumidity();

    if (isnan(temperature) || isnan(humidity)) {
      Serial.println("Gagal baca DHT!");
      return;
    }

    // --- Fuzzy Sugeno ---
    pumpDuration = hitungDurasiSugeno0(soilPercent, temperature);

    // --- Serial Monitor ---
    Serial.print("Tanah="); Serial.print(soilPercent, 1); Serial.print("% | ");
    Serial.print("Suhu=");  Serial.print(temperature, 1); Serial.print("C | ");
    Serial.print("Durasi="); Serial.print(pumpDuration, 2); Serial.println(" s");

    // --- LCD ---
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("T:"); lcd.print(soilPercent, 0); lcd.print("% ");
    lcd.print("S:"); lcd.print(temperature, 1); lcd.print("C");
    lcd.setCursor(0, 1);
    lcd.print("Pompa:"); lcd.print(pumpDuration, 1); lcd.print("s ");

    // --- Aktuasi pompa ---
    if (pumpDuration > 0.5) {
      Serial.println(">> Pompa ON");
      lcd.setCursor(11, 1); lcd.print("ON ");
      digitalWrite(RELAY_PIN, HIGH);
      delay((unsigned long)(pumpDuration * 1000));
      digitalWrite(RELAY_PIN, LOW);
      Serial.println(">> Pompa OFF");
      lcd.setCursor(11, 1); lcd.print("OFF");
    }
  }
}

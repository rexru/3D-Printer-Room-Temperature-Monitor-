/************************************************************
   Vaisala HMW90 (4–20 mA) → ESP32
   Reads Humidity on GPIO35 and Temperature on GPIO34
   Resistors: 100 Ω (0.4–2.0 V range, safe for ESP32)
 ************************************************************/
//

#include <Wire.h>
#include "RTClib.h"
RTC_DS3231 rtc;

#include <Preferences.h>
Preferences prefs;

#include <driver/adc.h>
#include <esp_adc_cal.h>
esp_adc_cal_characteristics_t adc_chars;

// ADC pins (ADC1 – safe with WiFi)
const int pinRH = 35;   // Humidity
const int pinT  = 34;   // Temperature

// Constants
const float VREF    = 3.3;     // ESP32 ADC reference
const int   ADC_MAX = 4095;    // 12-bit ADC
const float R_SHUNT = 100.0;   // resistor in ohms

// Optional smoothing (0.0 = off, 1.0 = heavy filtering)
float filterStrength = 0.2;
float RH_filtered    = 0;
float T_filtered     = 0;

// Day high/low values
float RH_day_high = -999;
float RH_day_low  = 999;
float T_day_high  = -999;
float T_day_low   = 999;

// 52-week high/low values
float RH_52_high = -999;
float RH_52_low  = 999;
float T_52_high  = -999;
float T_52_low   = 999;

// All-time high/low values
float RH_all_high = -999;
float RH_all_low  = 999;
float T_all_high  = -999;
float T_all_low   = 999;

DateTime bootTime;
bool allowHighLow = false;   // default: do NOT update highs/lows yet


float loadFloat(const char* key, float defaultValue) {
  if (!prefs.isKey(key)) {
    return defaultValue;
  }
  return prefs.getFloat(key, defaultValue);
}

void saveFloat(const char* key, float value) {
  prefs.putFloat(key, value);
}

void loadStoredValues() {
  // Day
  RH_day_high = loadFloat("RH_day_high", -999);
  RH_day_low  = loadFloat("RH_day_low",   999);
  T_day_high  = loadFloat("T_day_high",  -999);
  T_day_low   = loadFloat("T_day_low",    999);

  // 52-Week
  RH_52_high = loadFloat("RH_52_high", -999);
  RH_52_low  = loadFloat("RH_52_low",   999);
  T_52_high  = loadFloat("T_52_high",  -999);
  T_52_low   = loadFloat("T_52_low",    999);

  // All-Time
  RH_all_high = loadFloat("RH_all_high", -999);
  RH_all_low  = loadFloat("RH_all_low",   999);
  T_all_high  = loadFloat("T_all_high",  -999);
  T_all_low   = loadFloat("T_all_low",    999);

  Serial.println("NVS values loaded.");
}

// ======= GENERIC HIGH/LOW UPDATE FUNCTION =======
void updateHighLow(float value, float &highVar, float &lowVar, const char* label, const char* period, const char* highKey, const char* lowKey){
  if (value > highVar) {
    highVar = value;
    Serial.print("New ");
    Serial.print(period);
    Serial.print(" ");
    Serial.print(label);
    Serial.print(" High: ");
    Serial.println(value, 1);

    saveFloat(highKey, highVar);
  }

  if (value < lowVar) {
    lowVar = value;
    Serial.print("New ");
    Serial.print(period);
    Serial.print(" ");
    Serial.print(label);
    Serial.print(" Low: ");
    Serial.println(value, 1);

    saveFloat(lowKey, lowVar);
  }
}

void setup() {
  Serial.begin(9600);
  delay(500);

  // Configure ADC
  analogSetAttenuation(ADC_11db);  // 0-3.3V range
  
  // Calibrate ADC
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);

  // Start NVS
  prefs.begin("envData", false);

  // Load stored highs and lows
  loadStoredValues();

  // Start RTC
  Wire.begin();
  if (!rtc.begin()) {
    Serial.println("RTC missing!");
  }

  Serial.println("Starting HMW90 Reader (ESP32)...");

  bootTime = rtc.now();
  Serial.print("Boot timestamp: ");
  Serial.println(bootTime.timestamp());
}

void loop() {

  DateTime now = rtc.now();
  int hour = now.hour();
  int minute = now.minute();
  int second = now.second();


  // ********* READ RAW ADC VALUES *********
  int rawRH = analogRead(pinRH);
  int rawT  = analogRead(pinT);

  // ********* Convert ADC → Voltage *********
  // float voltRH = (rawRH * VREF) / ADC_MAX;
  // float voltT  = (rawT  * VREF) / ADC_MAX;
  // ********* Convert ADC → Voltage (calibrated) *********
  uint32_t voltRH_mv = esp_adc_cal_raw_to_voltage(rawRH, &adc_chars);
  uint32_t voltT_mv  = esp_adc_cal_raw_to_voltage(rawT, &adc_chars);

  float voltRH = voltRH_mv / 1000.0;  // Convert mV to V
  float voltT  = voltT_mv / 1000.0;

  // ********* Convert Voltage → Current (mA) *********
  float currentRH = (voltRH / R_SHUNT) * 1000.0;  // mA
  float currentT  = (voltT  / R_SHUNT) * 1000.0;  // mA

  // ********* Detect disconnected sensor *********
  if (currentRH < 3.0) Serial.println("WARNING: RH loop possibly open!");
  if (currentT  < 3.0) Serial.println("WARNING: T loop possibly open!");

  // ********* Convert Current → Physical Values *********
  // Humidity scaling (typical Vaisala 4–20 mA = 0–100 %RH)
  float humidity = (currentRH - 4.0) * (100.0 / 16.0);

  // Temperature scaling (often -20 to +60°C or per your unit)
  float temperatureC = (currentT - 4.0) * (60.0 / 16.0) - 5.0;

  // ********* Apply smoothing filter *********
  RH_filtered = RH_filtered + filterStrength * (humidity - RH_filtered);
  T_filtered  = T_filtered  + filterStrength * (temperatureC - T_filtered);

  // ===== CHECK IF 3 MINUTES HAVE PASSED SINCE BOOT =====
  if (!allowHighLow) {
    TimeSpan sinceBoot = now - bootTime;

    if (sinceBoot.totalseconds() >= 180) {  // 180 sec = 3 minutes
        allowHighLow = true;
        Serial.println("High/Low tracking ENABLED (3 minutes passed)");
    } else {
        // Still waiting — skip all updates
        Serial.print("High/Low tracking locked for ");
        Serial.print(180 - sinceBoot.totalseconds());
        Serial.println(" more seconds.");
    }
  }


  // ====== UPDATE ALL HIGHS / LOWS ======
  if (allowHighLow) {
    updateHighLow(RH_filtered, RH_day_high, RH_day_low, "Humidity", "Day", "RH_day_high", "RH_day_low");

    updateHighLow(T_filtered, T_day_high, T_day_low, "Temperature", "Day", "T_day_high", "T_day_low");

    updateHighLow(RH_filtered, RH_52_high, RH_52_low, "Humidity", "52wk", "RH_52_high", "RH_52_low");

    updateHighLow(T_filtered, T_52_high, T_52_low, "Temperature", "52wk", "T_52_high", "T_52_low");

    updateHighLow(RH_filtered, RH_all_high, RH_all_low, "Humidity", "AllTime", "RH_all_high", "RH_all_low");

    updateHighLow(T_filtered, T_all_high, T_all_low, "Temperature", "AllTime", "T_all_high", "T_all_low");
  }


  // ********* SERIAL OUTPUT *********
  Serial.print("Humidity: ");
  Serial.print(RH_filtered, 1);
  Serial.print(" %RH     |   ");

  Serial.print("Temp: ");
  Serial.print(T_filtered, 1);
  Serial.println(" °C");

  delay(500);
}

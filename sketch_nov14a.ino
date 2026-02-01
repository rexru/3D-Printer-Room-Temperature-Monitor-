/************************************************************
   Vaisala HMW90 (4–20 mA) → Arduino Uno
   Reads Humidity on A0 and Temperature on A1
   Resistors: 250 Ω each (1–5 V range)
 ************************************************************/

// ADC pins
const int pinRH = A1;
const int pinT  = A0;

// Constants
const float VREF       = 5.0;        // Arduino 5V reference
const int   ADC_MAX    = 1023;       // 10-bit ADC
const float R_SHUNT    = 220.0;      // resistor in ohms

// Optional smoothing (0.0 = off, 1.0 = heavy filtering)
float filterStrength = 0.2;
float RH_filtered    = 0;
float T_filtered     = 0;

// High and Low Variables 
float T_day_high  = -999;
float T_day_low   =  999;

float RH_day_high = -999;
float RH_day_low  =  999;

float T_52_high  = -999;
float T_52_low   =  999;

float RH_52_high = -999;
float RH_52_low  =  999;

float T_all_high  = -999;
float T_all_low   =  999;

float RH_all_high = -999;
float RH_all_low  =  999;

// ======= GENERIC HIGH/LOW UPDATE FUNCTION =======
void updateHighLow(float value, float &highVar, float &lowVar, const char* label, const char* period){
  if (value > highVar) {
    highVar = value;
    Serial.print("New ");
    Serial.print(period);
    Serial.print(" ");
    Serial.print(label);
    Serial.print(" High: ");
    Serial.println(value, 1);
  }

  if (value < lowVar) {
    lowVar = value;
    Serial.print("New ");
    Serial.print(period);
    Serial.print(" ");
    Serial.print(label);
    Serial.print(" Low: ");
    Serial.println(value, 1);
  }
}

void setup() {
  Serial.begin(9600);
  delay(1000);
  Serial.println("Starting HMW90 Reader...");
}

void loop() {

  // ********* READ RAW ADC VALUES *********
  int rawRH = analogRead(pinRH);
  int rawT  = analogRead(pinT);

  // ********* Convert ADC → Voltage *********
  float voltRH = (rawRH * VREF) / ADC_MAX;
  float voltT  = (rawT  * VREF) / ADC_MAX;

  // ********* Convert Voltage → Current (mA) *********
  float currentRH = (voltRH / R_SHUNT) * 1000.0;  // mA
  float currentT  = (voltT  / R_SHUNT) * 1000.0;  // mA

  // ********* Detect disconnected sensor *********
  if (currentRH < 3.0) Serial.println("WARNING: RH loop possibly open!");
  if (currentT  < 3.0) Serial.println("WARNING: T loop possibly open!");

  // ********* Convert Current → Physical Values *********
  // Humidity scaling (typical Vaisala 4–20 mA = 0–100 %RH)
  float humidity = (currentRH - 4.0) * (100.0 / 16.0);

  // Temperature scaling (your model is normally 4–20 mA = -20 to +60 °C)
  float temperatureC = (currentT - 4.0) * (60.0 / 16.0) - 5.0;

  // ********* Apply smoothing filter *********
  RH_filtered = RH_filtered + filterStrength * (humidity - RH_filtered);
  T_filtered  = T_filtered  + filterStrength * (temperatureC - T_filtered);

   //********* Check for day high *********
  if (RH_filtered > RH_day_high){
    RH_day_high = RH_filtered;  
    Serial.println("New Humidity Day High"); 
    Serial.print(RH_day_high, 1); 
    Serial.print(" %RH");
  } 
  
  if (T_filtered > T_day_high){
    T_day_high = T_filtered;  
    Serial.println("New Temperature Day High"); 
    Serial.print(T_day_high, 1); 
    Serial.print(" °C");
  } 

    //********* Check for day low *********
  if (RH_filtered < RH_day_low || RH_day_low == 0) {
    RH_day_low = RH_filtered;
    Serial.println("New Humidity Day Low");
    Serial.print(RH_day_low, 1);
    Serial.print(" %RH");
  }

  if (T_filtered < T_day_low || T_day_low == 0) {
    T_day_low = T_filtered;
    Serial.println("New Temperature Day Low");
    Serial.print(T_day_low, 1);
    Serial.print(" °C");
  }

  //********* Check for 52-week high *********
  if (RH_filtered > RH_52_high) {
    RH_52_high = RH_filtered;
    Serial.println("New 52-Week Humidity High");
    Serial.print(RH_52_high, 1);
    Serial.print(" %RH");
  }

  if (T_filtered > T_52_high) {
    T_52_high = T_filtered;
    Serial.println("New 52-Week Temperature High");
    Serial.print(T_52_high, 1);
    Serial.print(" °C");
  }

  //********* Check for 52-week low *********
  if (RH_filtered < RH_52_low || RH_52_low == 0) {
    RH_52_low = RH_filtered;
    Serial.println("New 52-Week Humidity Low");
    Serial.print(RH_52_low, 1);
    Serial.print(" %RH");
  }

  if (T_filtered < T_52_low || T_52_low == 0) {
    T_52_low = T_filtered;
    Serial.println("New 52-Week Temperature Low");
    Serial.print(T_52_low, 1);
    Serial.print(" °C");
  }

  //********* Check for ALL-TIME high *********
  if (RH_filtered > RH_all_high) {
    RH_all_high = RH_filtered;
    Serial.println("New ALL-TIME Humidity High");
    Serial.print(RH_all_high, 1);
    Serial.print(" %RH");
  }

  if (T_filtered > T_all_high) {
    T_all_high = T_filtered;
    Serial.println("New ALL-TIME Temperature High");
    Serial.print(T_all_high, 1);
    Serial.print(" °C");
  }

  //********* Check for ALL-TIME low *********
  if (RH_filtered < RH_all_low || RH_all_low == 0) {
    RH_all_low = RH_filtered;
    Serial.println("New ALL-TIME Humidity Low");
    Serial.print(RH_all_low, 1);
    Serial.print(" %RH");
  }

  if (T_filtered < T_all_low || T_all_low == 0) {
    T_all_low = T_filtered;
    Serial.println("New ALL-TIME Temperature Low");
    Serial.print(T_all_low, 1);
    Serial.print(" °C");
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


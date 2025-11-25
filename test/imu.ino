#include <Wire.h>

#define INC_ADDRESS 0x69
#define ACC_CONF  0x20  // Page 91
#define GYR_CONF  0x21  // Page 93
#define CMD       0x7E  // Page 65

int16_t  x, y, z;
int16_t  gyro_x, gyro_y, gyro_z;
int16_t  temp_raw;

float accelX_m_s2 , accelY_m_s2, accelZ_m_s2;
float gyroX_dps, gyroY_dps, gyroZ_dps;
float temperature_c;

// Gyro sensitivity for ±125 dps range (see BMI323 datasheet)
const float GYRO_SENS_125DPS = 262.1f;

void setup(void) { 
  Serial.begin(115200);

  Wire.begin(8, 9); 
  Wire.setClock(400000);      // I2C Fast Mode (400kHz) 

  softReset(); 

  /*
   * Acc_Conf P.91
   * mode:        0x7000  -> High
   * average:     0x0000  -> No
   * filtering:   0x0080  -> ODR/4
   * range:       0x0000  -> 2G   (adjust conversion if you change this)
   * ODR:         0x000B  -> 800Hz
   */
  writeRegister16(ACC_CONF, 0x753D);  // Setting accelerometer 

  /*
   * Gyr_Conf P.93
   * mode:        0x7000  -> High
   * average:     0x0000  -> No
   * filtering:   0x0080  -> ODR/4
   * range:       0x0000  -> ±125 dps (assumed for sensitivity below)
   * ODR:         0x000B  -> 800Hz
   */
  writeRegister16(GYR_CONF, 0x758D);  // Setting gyroscope   
}

void softReset(){ 
  writeRegister16(CMD, 0xDEAF);
  delay(50);   
}

void loop() {
  // Example check – you can keep/remove depending on how you use reg 0x02
  // if (readRegister16(0x02) == 0x00) {
    // Read accel + gyro + temp
    readAllSensors();             

    Serial.print("ACC [m/s^2] X: ");
    Serial.print(accelX_m_s2);
    Serial.print("  Y: ");
    Serial.print(accelY_m_s2);
    Serial.print("  Z: ");
    Serial.println(accelZ_m_s2);

    Serial.print("GYR [dps]   X: ");
    Serial.print(gyroX_dps);
    Serial.print("  Y: ");
    Serial.print(gyroY_dps);
    Serial.print("  Z: ");
    Serial.println(gyroZ_dps);

    Serial.print("TEMP [C]: ");
    Serial.println(temperature_c);

    delay(50);
  // }
}

// ---------------- I2C helpers ----------------

// Write data in 16 bits
void writeRegister16(uint16_t reg, uint16_t value) {
  Wire.beginTransmission(INC_ADDRESS);
  Wire.write((uint8_t)reg);
  // Low
  Wire.write(value & 0xFF);
  // High
  Wire.write((value >> 8) & 0xFF);
  Wire.endTransmission();
}

// Read data in 16 bits
uint16_t readRegister16(uint8_t reg) {
  Wire.beginTransmission(INC_ADDRESS);
  Wire.write(reg);
  Wire.endTransmission(false);

  Wire.requestFrom(INC_ADDRESS, (uint8_t)2);
  uint8_t lo = 0, hi = 0;
  if (Wire.available()) lo = Wire.read();
  if (Wire.available()) hi = Wire.read();

  return (uint16_t)(lo | (hi << 8));
}

// ---------------- Sensor reading ----------------

void readAllSensors() {

  Wire.beginTransmission(INC_ADDRESS);
  Wire.write(0x03);   // ACC data start
  Wire.endTransmission(false);

  Wire.requestFrom(INC_ADDRESS, (uint8_t)16);  // now reading 16 bytes
  uint8_t data[16];
  int i = 0;
  while (Wire.available() && i < 16) {
    data[i++] = Wire.read();
  }

  int offset = 2;  // discard dummy bytes

  x       = (int16_t)(data[offset + 0] | (data[offset + 1] << 8));
  y       = (int16_t)(data[offset + 2] | (data[offset + 3] << 8));
  z       = (int16_t)(data[offset + 4] | (data[offset + 5] << 8));

  gyro_x  = (int16_t)(data[offset + 6]  | (data[offset + 7]  << 8));
  gyro_y  = (int16_t)(data[offset + 8]  | (data[offset + 9]  << 8));
  gyro_z  = (int16_t)(data[offset + 10] | (data[offset + 11] << 8));

  temp_raw = (int16_t)(data[offset + 12] | (data[offset + 13] << 8));

  accelX_m_s2 = lsbToM2S(x);
  accelY_m_s2 = lsbToM2S(y);
  accelZ_m_s2 = lsbToM2S(z);

  gyroX_dps = gyro_x / GYRO_SENS_125DPS;
  gyroY_dps = gyro_y / GYRO_SENS_125DPS;
  gyroZ_dps = gyro_z / GYRO_SENS_125DPS;

  temperature_c = (float)temp_raw / 512.0f + 23.0f;
}

// Acc LSB → m/s² (keep your chosen sensitivity here)
float lsbToM2S(int16_t rawData) {
  // NOTE: 2048 LSB/g corresponds to ±16 g.
  // If you really are using ±2 g, change to 16384.0f.
  const float sensitivity = 16384.0;
  const float gToM2S = 9.80665f;
  return (rawData / sensitivity) * gToM2S;
}
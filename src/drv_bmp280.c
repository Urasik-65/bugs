/*
 * This file is part of baseflight
 * Licensed under GPL V3 or modified DCL - see https://github.com/multiwii/baseflight/blob/master/README.md
 */
#include "board.h"

// BMP280, address 0x76

#define BMP280_I2C_ADDR                      (0x76)
#define BMP280_DEFAULT_CHIP_ID               (0x58)

#define BMP280_CHIP_ID_REG                   (0xD0)  /* Chip ID Register */
#define BMP280_RST_REG                       (0xE0)  /* Softreset Register */
#define BMP280_STAT_REG                      (0xF3)  /* Status Register */
#define BMP280_CTRL_MEAS_REG                 (0xF4)  /* Ctrl Measure Register */
#define BMP280_CONFIG_REG                    (0xF5)  /* Configuration Register */
#define BMP280_PRESSURE_MSB_REG              (0xF7)  /* Pressure MSB Register */
#define BMP280_PRESSURE_LSB_REG              (0xF8)  /* Pressure LSB Register */
#define BMP280_PRESSURE_XLSB_REG             (0xF9)  /* Pressure XLSB Register */
#define BMP280_TEMPERATURE_MSB_REG           (0xFA)  /* Temperature MSB Reg */
#define BMP280_TEMPERATURE_LSB_REG           (0xFB)  /* Temperature LSB Reg */
#define BMP280_TEMPERATURE_XLSB_REG          (0xFC)  /* Temperature XLSB Reg */
#define BMP280_FORCED_MODE                   (0x01)

#define BMP280_TEMPERATURE_CALIB_DIG_T1_LSB_REG             (0x88)
#define BMP280_PRESSURE_TEMPERATURE_CALIB_DATA_LENGTH       (24)
#define BMP280_DATA_FRAME_SIZE               (6)

#define BMP280_OVERSAMP_SKIPPED          (0x00)
#define BMP280_OVERSAMP_1X               (0x01)
#define BMP280_OVERSAMP_2X               (0x02)
#define BMP280_OVERSAMP_4X               (0x03)
#define BMP280_OVERSAMP_8X               (0x04)
#define BMP280_OVERSAMP_16X              (0x05)

// configure pressure and temperature oversampling, forced sampling mode
#define BMP280_PRESSURE_OSR              (BMP280_OVERSAMP_8X)
#define BMP280_TEMPERATURE_OSR           (BMP280_OVERSAMP_1X)
#define BMP280_MODE                      (BMP280_PRESSURE_OSR << 2 | BMP280_TEMPERATURE_OSR << 5 | BMP280_FORCED_MODE)

#define T_INIT_MAX                       (20)
// 20/16 = 1.25 ms
#define T_MEASURE_PER_OSRS_MAX           (37)
// 37/16 = 2.3125 ms
#define T_SETUP_PRESSURE_MAX             (10)
// 10/16 = 0.625 ms

typedef struct bmp280_calib_param_t {
    uint16_t dig_T1; /* calibration T1 data */
    int16_t dig_T2; /* calibration T2 data */
    int16_t dig_T3; /* calibration T3 data */
    uint16_t dig_P1; /* calibration P1 data */
    int16_t dig_P2; /* calibration P2 data */
    int16_t dig_P3; /* calibration P3 data */
    int16_t dig_P4; /* calibration P4 data */
    int16_t dig_P5; /* calibration P5 data */
    int16_t dig_P6; /* calibration P6 data */
    int16_t dig_P7; /* calibration P7 data */
    int16_t dig_P8; /* calibration P8 data */
    int16_t dig_P9; /* calibration P9 data */
    int32_t t_fine; /* calibration t_fine data */
} bmp280_calib_param_t;

static uint8_t bmp280_chip_id = 0;
static bool bmp280InitDone = false;
static bmp280_calib_param_t bmp280_cal;
// uncompensated pressure and temperature
static int32_t bmp280_up = 0;
static int32_t bmp280_ut = 0;

static void bmp280_start_ut(void);
static void bmp280_get_ut(void);
static void bmp280_start_up(void);
static void bmp280_get_up(void);
static void bmp280_calculate(int32_t *pressure, int32_t *temperature);

bool bmp280Detect(baro_t *baro)
{
    if (bmp280InitDone)
        return true;

    delay(20);

    i2cRead(BMP280_I2C_ADDR, BMP280_CHIP_ID_REG, 1, &bmp280_chip_id);  /* read Chip Id */
    if (bmp280_chip_id != BMP280_DEFAULT_CHIP_ID)
        return false;

    // read calibration
    i2cRead(BMP280_I2C_ADDR, BMP280_TEMPERATURE_CALIB_DIG_T1_LSB_REG, 24, (uint8_t *)&bmp280_cal);
    // set oversampling + power mode (forced), and start sampling
    i2cWrite(BMP280_I2C_ADDR, BMP280_CTRL_MEAS_REG, BMP280_MODE);

    bmp280InitDone = true;

    // these are dummy as temperature is measured as part of pressure
    baro->ut_delay = 0;
    baro->get_ut = bmp280_get_ut;
    baro->start_ut = bmp280_start_ut;

    // only _up part is executed, and gets both temperature and pressure
    baro->up_delay = ((T_INIT_MAX + T_MEASURE_PER_OSRS_MAX * (((1 << BMP280_TEMPERATURE_OSR) >> 1) + ((1 << BMP280_PRESSURE_OSR) >> 1)) + (BMP280_PRESSURE_OSR ? T_SETUP_PRESSURE_MAX : 0) + 15) / 16) * 1000;
    baro->start_up = bmp280_start_up;
    baro->get_up = bmp280_get_up;
    baro->calculate = bmp280_calculate;

    return true;
}

static void bmp280_start_ut(void)
{
    // dummy
}

static void bmp280_get_ut(void)
{
    // dummy
}

static void bmp280_start_up(void)
{
    // start measurement
    // set oversampling + power mode (forced), and start sampling
    i2cWrite(BMP280_I2C_ADDR, BMP280_CTRL_MEAS_REG, BMP280_MODE);
}

static void bmp280_get_up(void)
{
    uint8_t data[BMP280_DATA_FRAME_SIZE];

    // read data from sensor
    i2cRead(BMP280_I2C_ADDR, BMP280_PRESSURE_MSB_REG, BMP280_DATA_FRAME_SIZE, data);
    bmp280_up = (int32_t)((((uint32_t)(data[0])) << 12) | (((uint32_t)(data[1])) << 4) | ((uint32_t)data[2] >> 4));
    bmp280_ut = (int32_t)((((uint32_t)(data[3])) << 12) | (((uint32_t)(data[4])) << 4) | ((uint32_t)data[5] >> 4));
}

// Returns temperature in DegC, float precision. Output value of ?51.23? equals 51.23 DegC.
// t_fine carries fine temperature as global value
float bmp280_compensate_T(int32_t adc_T)
{
    float var1, var2, T;

    var1 = (((float)adc_T) / 16384.0f - ((float)bmp280_cal.dig_T1) / 1024.0f) * ((float)bmp280_cal.dig_T2);
    var2 = ((((float)adc_T) / 131072.0f - ((float)bmp280_cal.dig_T1) / 8192.0f) * (((float)adc_T) / 131072.0f - ((float)bmp280_cal.dig_T1) / 8192.0f)) * ((float)bmp280_cal.dig_T3);
    bmp280_cal.t_fine = (int32_t)(var1 + var2);
    T = (var1 + var2) / 5120.0f;

    return T;
}

// Returns pressure in Pa as float. Output value of ?96386.2? equals 96386.2 Pa = 963.862 hPa
float bmp280_compensate_P(int32_t adc_P)
{
    float var1, var2, p;
    var1 = ((float)bmp280_cal.t_fine / 2.0f) - 64000.0f;
    var2 = var1 * var1 * ((float)bmp280_cal.dig_P6) / 32768.0f;
    var2 = var2 + var1 * ((float)bmp280_cal.dig_P5) * 2.0f;
    var2 = (var2 / 4.0f) + (((float)bmp280_cal.dig_P4) * 65536.0f);
    var1 = (((float)bmp280_cal.dig_P3) * var1 * var1 / 524288.0f + ((float)bmp280_cal.dig_P2) * var1) / 524288.0f;
    var1 = (1.0f + var1 / 32768.0f) * ((float)bmp280_cal.dig_P1);
    if (var1 == 0.0f)
        return 0.0f; // avoid exception caused by division by zero

    p = 1048576.0f - (float)adc_P;
    p = (p - (var2 / 4096.0f)) * 6250.0f / var1;
    var1 = ((float)bmp280_cal.dig_P9) * p * p / 2147483648.0f;
    var2 = p * ((float)bmp280_cal.dig_P8) / 32768.0f;
    p = p + (var1 + var2 + ((float)bmp280_cal.dig_P7)) / 16.0f;

    return p;
}

static void bmp280_calculate(int32_t *pressure, int32_t *temperature)
{
    // calculate
    float t, p;
    t = bmp280_compensate_T(bmp280_ut);
    p = bmp280_compensate_P(bmp280_up);

    if (pressure)
        *pressure = (int32_t)p;
    if (temperature)
        *temperature = (int32_t)t * 100;
}

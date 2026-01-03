/**\
 * Copyright (c) 2024 Bosch Sensortec GmbH. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 **/

/******************************************************************************/
/*!                 Header Files                                              */
#include <stdio.h>
#include <math.h>
#include "bmi330.h"
#include "common.h"

/******************************************************************************/
/*!         Macros definition                                       */

#define ACCEL        UINT8_C(0x00)
#define GYRO         UINT8_C(0x01)
#define TEMPERATURE  UINT8_C(0x02)

/******************************************************************************/
/*!         Structure Definition                                              */

/*! Structure to define accelerometer and gyroscope configuration. */
struct bmi3_sens_config config[2];

/******************************************************************************/
/*!         Static Function Declaration                                       */

/*!
 *  @brief This internal API is used to set configurations for FIFO, accelerometer and gyroscope.
 *
 *  @param[in] dev       : Structure instance of bmi3_dev.
 *
 *  @return void.
 */
static void set_sensor_config(struct bmi3_dev *dev);

/*! @brief This internal API converts raw sensor values(LSB) to G value
 *
 *  @param[in] val        : Raw sensor value.
 *  @param[in] g_range    : Accel Range selected (4G).
 *  @param[in] bit_width  : Resolution of the sensor.
 *
 *  @return Accel values in Gravity(G)
 *
 */
static float lsb_to_g(int16_t val, float g_range, uint8_t bit_width);

/*!
 *  @brief This function converts lsb to degree per second for 16 bit gyro at
 *  range 125, 250, 500, 1000 or 2000dps.
 *
 *  @param[in] val       : LSB from each axis.
 *  @param[in] dps       : Degree per second.
 *  @param[in] bit_width : Resolution for gyro.
 *
 *  @return Degree per second.
 */
static float lsb_to_dps(int16_t val, float dps, uint8_t bit_width);

/******************************************************************************/
/*!               Functions                                                   */

/* This function starts the execution of program. */
int main(void)
{
    struct bmi3_dev dev;
    int8_t rslt;

    /* Variable to set the data sample rate of i3c sync
     * 0x0032 is set to 50 samples this value can vary  */
    uint16_t sample_rate = 0x0032;

    /* Variable to set the delay time of i3c sync */
    uint8_t delay_time = BMI3_I3C_SYNC_DIVISION_FACTOR_11;

    /* Variable to set the i3c sync ODR */
    uint8_t odr = BMI3_I3C_SYNC_ODR_50HZ;

    /* Variable to enable the filer */
    uint8_t i3c_tc_res = BMI330_ENABLE;

    uint8_t limit = 20;

    uint16_t int_status;

    struct bmi3_feature_enable feature = { 0 };

    struct bmi3_sensor_data sensor_data[3];

    /* Variable to store temperature */
    float temperature_value;

    float acc_x = 0, acc_y = 0, acc_z = 0;
    float gyr_x = 0, gyr_y = 0, gyr_z = 0;
    uint8_t indx = 0;

    /* Function to select interface between SPI and I2C, according to that the device structure gets updated.
     * Interface reference is given as a parameter
     * For I2C : BMI3_I2C_INTF
     * For SPI : BMI3_SPI_INTF
     */
    rslt = bmi3_interface_init(&dev, BMI3_SPI_INTF);
    bmi3_error_codes_print_result("bmi3_interface_init", rslt);

    if (rslt == BMI330_OK)
    {
        /* Initialize BMI330 */
        printf("Uploading configuration file\n");
        rslt = bmi330_init(&dev);
        bmi3_error_codes_print_result("bmi330_init", rslt);

        printf("Configuration file uploaded\n");
        printf("Chip ID :0x%x\n", dev.chip_id);

        if (rslt == BMI330_OK)
        {
            rslt = bmi330_configure_enhanced_flexibility(&dev);
            bmi3_error_codes_print_result("bmi330_configure_enhanced_flexibility", rslt);

            if (rslt == BMI330_OK)
            {
                set_sensor_config(&dev);

                /* Enable i3c_sync feature */
                feature.i3c_sync_en = BMI330_ENABLE;

                /* Enable the selected sensors */
                rslt = bmi330_select_sensor(&feature, &dev);
                bmi3_error_codes_print_result("bmi330_select_sensor", rslt);

                if (rslt == BMI330_OK)
                {
                    printf("\nI3C sync feature enabled\n");
                }

                /* Set the data sample rate of i3c sync */
                rslt = bmi330_set_i3c_tc_sync_tph(sample_rate, &dev);
                bmi3_error_codes_print_result("set_i3c_tc_sync_tph", rslt);

                /* Set the delay time of i3c sync */
                rslt = bmi330_set_i3c_tc_sync_tu(delay_time, &dev);
                bmi3_error_codes_print_result("set_i3c_tc_sync_tu", rslt);

                /* Set i3c sync ODR */
                rslt = bmi330_set_i3c_tc_sync_odr(odr, &dev);
                bmi3_error_codes_print_result("set_i3c_tc_sync_odr", rslt);

                /* Enable the i3c sync filter */
                rslt = bmi330_set_i3c_sync_i3c_tc_res(i3c_tc_res, &dev);
                bmi3_error_codes_print_result("set_i3c_sync_i3c_tc_res", rslt);

                /* After any change to the I3C-Sync configuration parameters,
                 * a config changed CMD (0x0201) must be written
                 * to CMD register to update the internal configuration */
                rslt = bmi330_set_command_register(BMI3_CMD_I3C_TCSYNC_UPDATE, &dev);
                bmi3_error_codes_print_result("bmi330_set_command_register", rslt);

                /* Set the type of sensor data */
                sensor_data[ACCEL].type = BMI3_I3C_SYNC_ACCEL;
                sensor_data[GYRO].type = BMI3_I3C_SYNC_GYRO;
                sensor_data[TEMPERATURE].type = BMI3_I3C_SYNC_TEMP;

                printf("\nI3C accel, gyro and temperature data\n");

                printf("%8s %10s %10s %10s %10s %10s %10s %10s %10s %10s %10s %10s %7s %10s %10s %10s %10s %8s\n",
                       "Data_set",
                       "i3c_acc_x",
                       "i3c_acc_y",
                       "i3c_acc_z",
                       "i3c_acc_time(lsb)",
                       "acc_gravity_x",
                       "acc_gravity_y",
                       "acc_gravity_z",
                       "i3c_gyro_x",
                       "i3c_gyro_y",
                       "i3c_gyro_z",
                       "i3c_gyr_time(lsb)",
                       "gyro_dps_x",
                       "gyro_dps_y",
                       "gyro_dps_z",
                       "i3c_temp(lsb)",
                       "i3c_sync_time",
                       "Temp(C)");

                while (indx <= limit)
                {
                    /* Delay provided based on i3c sync ODR */
                    dev.delay_us(20000, dev.intf_ptr);

                    /* To get the status of i3c sync interrupt status. */
                    rslt = bmi330_get_int1_status(&int_status, &dev);
                    bmi3_error_codes_print_result("bmi330_get_int1_status", rslt);

                    /* To check the i3c sync accel data */
                    if (int_status & BMI3_INT_STATUS_I3C)
                    {
                        rslt = bmi330_get_sensor_data(sensor_data, BMI3_N_SENSE_COUNT_3, &dev);
                        bmi3_error_codes_print_result("bmi330_get_sensor_data", rslt);

                        /* Converting lsb to gravity for 16 bit accelerometer at 2G range. */
                        acc_x =
                            lsb_to_g((int16_t)sensor_data[ACCEL].sens_data.i3c_sync.sync_x,
                                     BMI3_GET_RANGE_VAL(BMI3_ACC_RANGE_2G),
                                     dev.resolution);
                        acc_y =
                            lsb_to_g((int16_t)sensor_data[ACCEL].sens_data.i3c_sync.sync_y,
                                     BMI3_GET_RANGE_VAL(BMI3_ACC_RANGE_2G),
                                     dev.resolution);
                        acc_z =
                            lsb_to_g((int16_t)sensor_data[ACCEL].sens_data.i3c_sync.sync_z,
                                     BMI3_GET_RANGE_VAL(BMI3_ACC_RANGE_2G),
                                     dev.resolution);

                        /* Converting lsb to degree per second for 16 bit gyro at 2000dps range. */
                        gyr_x = lsb_to_dps((int16_t)sensor_data[GYRO].sens_data.i3c_sync.sync_x,
                                           BMI3_GET_DPS_VAL(BMI3_GYR_RANGE_2000DPS),
                                           dev.resolution);
                        gyr_y = lsb_to_dps((int16_t)sensor_data[GYRO].sens_data.i3c_sync.sync_y,
                                           BMI3_GET_DPS_VAL(BMI3_GYR_RANGE_2000DPS),
                                           dev.resolution);
                        gyr_z = lsb_to_dps((int16_t)sensor_data[GYRO].sens_data.i3c_sync.sync_z,
                                           BMI3_GET_DPS_VAL(BMI3_GYR_RANGE_2000DPS),
                                           dev.resolution);

                        temperature_value =
                            (float)((((float)((int16_t)sensor_data[TEMPERATURE].sens_data.i3c_sync.sync_temp)) /
                                     256.0) +
                                    23.0);

                        printf(
                            "%6d %10d %10d %10d %10d %15.2f %15.2f %12.2f %10d %10d %10d %13d %15.2f %14.2f %13.2f %12d %12d %7.2f\n",
                            indx,
                            (int16_t)sensor_data[ACCEL].sens_data.i3c_sync.sync_x,
                            (int16_t)sensor_data[ACCEL].sens_data.i3c_sync.sync_y,
                            (int16_t)sensor_data[ACCEL].sens_data.i3c_sync.sync_z,
                            sensor_data[ACCEL].sens_data.i3c_sync.sync_time,
                            acc_x,
                            acc_y,
                            acc_z,
                            (int16_t)sensor_data[GYRO].sens_data.i3c_sync.sync_x,
                            (int16_t)sensor_data[GYRO].sens_data.i3c_sync.sync_y,
                            (int16_t)sensor_data[GYRO].sens_data.i3c_sync.sync_z,
                            sensor_data[GYRO].sens_data.i3c_sync.sync_time,
                            gyr_x,
                            gyr_y,
                            gyr_z,
                            sensor_data[TEMPERATURE].sens_data.i3c_sync.sync_temp,
                            sensor_data[TEMPERATURE].sens_data.i3c_sync.sync_time,
                            temperature_value);
                    }

                    indx++;
                }
            }
        }
    }

    bmi3_coines_deinit();

    return rslt;
}

/*!
 * @brief This internal API is used to set configurations for accelerometer, gyroscope and FIFO.
 */
static void set_sensor_config(struct bmi3_dev *dev)
{
    int8_t rslt;

    /* Structure to define interrupt with its feature */
    struct bmi3_map_int map_int = { 0 };

    /* Configure type of feature */
    config[ACCEL].type = BMI330_ACCEL;
    config[GYRO].type = BMI330_GYRO;

    /* Get default configurations for the type of feature selected */
    rslt = bmi330_get_sensor_config(config, BMI3_N_SENSE_COUNT_2, dev);
    bmi3_error_codes_print_result("bmi330_get_sensor_config", rslt);

    /* Configure the accel and gyro settings */
    config[ACCEL].cfg.acc.odr = BMI3_ACC_ODR_50HZ;
    config[ACCEL].cfg.acc.bwp = BMI3_ACC_BW_ODR_QUARTER;
    config[ACCEL].cfg.acc.range = BMI3_ACC_RANGE_2G;
    config[ACCEL].cfg.acc.acc_mode = BMI3_ACC_MODE_NORMAL;

    config[GYRO].cfg.gyr.odr = BMI3_GYR_ODR_50HZ;
    config[GYRO].cfg.gyr.bwp = BMI3_GYR_BW_ODR_HALF;
    config[GYRO].cfg.gyr.range = BMI3_GYR_RANGE_2000DPS;
    config[GYRO].cfg.gyr.gyr_mode = BMI3_GYR_MODE_NORMAL;

    rslt = bmi330_set_sensor_config(config, BMI3_N_SENSE_COUNT_2, dev);
    bmi3_error_codes_print_result("bmi330_set_sensor_config", rslt);

    printf("\nAccel Configurations\n");
    printf("ODR : %s\t\n", enum_to_string(BMI3_ACC_ODR_50HZ));
    printf("Bandwith : %s\t\n", enum_to_string(BMI3_ACC_BW_ODR_QUARTER));
    printf("Range : %s\t\n", enum_to_string(BMI3_ACC_RANGE_2G));
    printf("Accel mode : %s\t\n", enum_to_string(BMI3_ACC_MODE_NORMAL));

    printf("\nGyro Configurations\n");
    printf("ODR : %s\t\n", enum_to_string(BMI3_GYR_ODR_50HZ));
    printf("Bandwith : %s\t\n", enum_to_string(BMI3_GYR_BW_ODR_HALF));
    printf("Range : %s\t\n", enum_to_string(BMI3_GYR_RANGE_2000DPS));
    printf("Gyro mode : %s\t\n", enum_to_string(BMI3_GYR_MODE_NORMAL));

    /* Get configurations for the type of feature selected */
    rslt = bmi330_get_sensor_config(config, BMI3_N_SENSE_COUNT_2, dev);
    bmi3_error_codes_print_result("bmi330_get_sensor_config", rslt);

    /* Select the feature and map the interrupt to pin BMI3_INT1 or BMI3_INT2 */
    map_int.i3c_out = BMI3_INT1;

    /* Map i3c sync interrupt to interrupt pin. */
    rslt = bmi330_map_interrupt(map_int, dev);
    bmi3_error_codes_print_result("bmi330_map_interrupt", rslt);
}

/*!
 * @brief This internal API converts raw sensor values(LSB) to G value
 */
static float lsb_to_g(int16_t val, float g_range, uint8_t bit_width)
{
    double power = 2;

    float half_scale = (float)((pow((double)power, (double)bit_width) / 2.0f));

    return (val * g_range) / half_scale;
}

/*!
 * @brief This function converts lsb to degree per second for 16 bit gyro at
 * range 125, 250, 500, 1000 or 2000dps.
 */
static float lsb_to_dps(int16_t val, float dps, uint8_t bit_width)
{
    double power = 2;

    float half_scale = (float)((pow((double)power, (double)bit_width) / 2.0f));

    return (dps / (half_scale)) * (val);
}

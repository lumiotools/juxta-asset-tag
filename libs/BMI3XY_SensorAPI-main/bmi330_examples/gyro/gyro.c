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
/*!                Macro definition                                           */

/* Pi value used in converting gyro degrees per second to radian per second */
#define PI  (3.14f)

/******************************************************************************/
/*!         Static Function Declaration                                       */

/*!
 *  @brief This internal API is used to set configurations for gyro.
 *
 *  @param[in] dev       : Structure instance of bmi3_dev.
 *
 *  @return Status of execution.
 */
static int8_t set_gyro_config(struct bmi3_dev *dev);

/*!
 *  @brief This function converts lsb to degree per second for 16 bit gyro at
 *  range 125, 250, 500, 1000 or 2000dps.
 *
 *  @param[in] gyro_lsb  : LSB from each axis.
 *  @param[in] dps_range : Degree per second.
 *  @param[in] bit_width : Resolution for gyro.
 *  @param[in] gyro_data : Structure instance to store converted gyro data.
 *
 *  @return void.
 */
static void lsb_to_dps(int16_t gyro_lsb, float dps_range, uint8_t bit_width,
                       struct bmi3_gyro_processed_data *gyro_data);

/******************************************************************************/
/*!            Functions                                                      */

/* This function starts the execution of program. */
int main(void)
{
    /* Status of API are returned to this variable. */
    int8_t rslt;

    /* Variable to define limit to print gyro data. */
    uint8_t limit = 100;
    uint8_t indx = 1;

    /* Sensor initialization configuration. */
    struct bmi3_dev dev = { 0 };

    /* Create an instance of sensor data structure. */
    struct bmi3_sensor_data sensor_data = { 0 };

    /* Variable that stores converted gyro data*/
    struct bmi3_gyro_processed_data gyro_data_x;
    struct bmi3_gyro_processed_data gyro_data_y;
    struct bmi3_gyro_processed_data gyro_data_z;

    /* Initialize the interrupt status of gyro. */
    uint16_t int_status = 0;

    /* Function to select interface between SPI and I2C, according to that the device structure gets updated.
     * Interface reference is given as a parameter
     * For I2C : BMI3_I2C_INTF
     * For SPI : BMI3_SPI_INTF
     */
    rslt = bmi3_interface_init(&dev, BMI3_SPI_INTF);
    bmi3_error_codes_print_result("bmi3_interface_init", rslt);

    if (rslt == BMI330_OK)
    {
        /* Initialize bmi330. */
        printf("Uploading configuration file\n");
        rslt = bmi330_init(&dev);
        bmi3_error_codes_print_result("bmi330_init", rslt);

        printf("Configuration file uploaded\n");
        printf("Chip ID :0x%x\n", dev.chip_id);
        ;

        if (rslt == BMI330_OK)
        {
            /* Gyro configuration settings. */
            rslt = set_gyro_config(&dev);

            if (rslt == BMI330_OK)
            {
                /* Select gyro sensor. */
                sensor_data.type = BMI330_GYRO;

                printf("\nAquisition Iteration Count:\t %d\n\n", limit);
                printf("\n%10s %12s %12s %12s %12s %12s %12s %12s %12s %12s %12s %12s %12s\n",
                       "SENS_TIME",
                       "GYRO_LSB_X",
                       "GYRO_LSB_Y",
                       "GYRO_LSB_Z",
                       "GYRO_X",
                       "GYRO_Y",
                       "GYRO_Z",
                       "GYRO_DPS_X",
                       "GYRO_DPS_Y",
                       "GYRO_DPS_Z",
                       "GYRO_RPS_X",
                       "GYRO_RPS_Y",
                       "GYRO_RPS_Z");

                /* Loop to print gyro data when interrupt occurs. */
                while (indx <= limit)
                {
                    /* To get the data ready interrupt status of gyro. */
                    rslt = bmi330_get_int1_status(&int_status, &dev);
                    bmi3_error_codes_print_result("Get interrupt status", rslt);

                    /* To check the data ready interrupt status and print the status for 10 samples. */
                    if (int_status & BMI3_INT_STATUS_GYR_DRDY)
                    {
                        /* Get gyro data for x, y and z axis. */
                        rslt = bmi330_get_sensor_data(&sensor_data, BMI3_N_SENSE_COUNT_1, &dev);
                        bmi3_error_codes_print_result("Get sensor data", rslt);

                        if (rslt == BMI3_OK)
                        {
                            /* Converting lsb to degree per second for 16 bit gyro at 2000dps range. */
                            lsb_to_dps(sensor_data.sens_data.gyr.x,
                                       BMI3_GET_DPS_VAL(BMI3_GYR_RANGE_2000DPS),
                                       dev.resolution,
                                       &gyro_data_x);
                            lsb_to_dps(sensor_data.sens_data.gyr.y,
                                       BMI3_GET_DPS_VAL(BMI3_GYR_RANGE_2000DPS),
                                       dev.resolution,
                                       &gyro_data_y);
                            lsb_to_dps(sensor_data.sens_data.gyr.z,
                                       BMI3_GET_DPS_VAL(BMI3_GYR_RANGE_2000DPS),
                                       dev.resolution,
                                       &gyro_data_z);
                            #ifndef PC

                            printf(
                                "%10lu %12d %12d %12d %12.3f %12.3f %12.3f %12.3f %12.3f %12.3f %12.3f %12.3f %12.3f\n",
                                sensor_data.sens_data.gyr.sens_time,
                                sensor_data.sens_data.gyr.x,
                                sensor_data.sens_data.gyr.y,
                                sensor_data.sens_data.gyr.z,
                                gyro_data_x.gyro_raw,
                                gyro_data_y.gyro_raw,
                                gyro_data_z.gyro_raw,
                                gyro_data_x.gyro_dps,
                                gyro_data_y.gyro_dps,
                                gyro_data_z.gyro_dps,
                                gyro_data_x.gyro_rps,
                                gyro_data_y.gyro_rps,
                                gyro_data_z.gyro_rps);
                            #else
                            printf(
                                "%10u %12d %12d %12d %12.3f %12.3f %12.3f %12.3f %12.3f %12.3f %12.3f %12.3f %12.3f\n",
                                sensor_data.sens_data.gyr.sens_time,
                                sensor_data.sens_data.gyr.x,
                                sensor_data.sens_data.gyr.y,
                                sensor_data.sens_data.gyr.z,
                                gyro_data_x.gyro_raw,
                                gyro_data_y.gyro_raw,
                                gyro_data_z.gyro_raw,
                                gyro_data_x.gyro_dps,
                                gyro_data_y.gyro_dps,
                                gyro_data_z.gyro_dps,
                                gyro_data_x.gyro_rps,
                                gyro_data_y.gyro_rps,
                                gyro_data_z.gyro_rps);
                                #endif

                            indx++;
                        }
                    }
                }
            }
        }
    }

    bmi3_coines_deinit();

    return rslt;
}

/*!
 *  @brief This internal API is used to set configurations for gyro.
 */
static int8_t set_gyro_config(struct bmi3_dev *dev)
{
    struct bmi3_map_int map_int = { 0 };

    /* Status of API are returned to this variable. */
    int8_t rslt;

    /* Structure to define the type of sensor and its configurations. */
    struct bmi3_sens_config config;

    /* Configure the type of feature. */
    config.type = BMI330_GYRO;

    /* Get default configurations for the type of feature selected. */
    rslt = bmi330_get_sensor_config(&config, BMI3_N_SENSE_COUNT_1, dev);
    bmi3_error_codes_print_result("bmi330_get_sensor_config", rslt);

    if (rslt == BMI330_OK)
    {
        map_int.gyr_drdy_int = BMI3_INT1;

        /* Map data ready interrupt to interrupt pin. */
        rslt = bmi330_map_interrupt(map_int, dev);
        bmi3_error_codes_print_result("Map interrupt", rslt);

        if (rslt == BMI330_OK)
        {
            /* The user can change the following configuration parameters according to their requirement. */
            /* Output Data Rate. By default ODR is set as 100Hz for gyro. */
            config.cfg.gyr.odr = BMI3_GYR_ODR_800HZ;

            /* Gyroscope Angular Rate Measurement Range. By default the range is 2000dps. */
            config.cfg.gyr.range = BMI3_GYR_RANGE_2000DPS;

            /*  The Gyroscope bandwidth coefficient defines the 3 dB cutoff frequency in relation to the ODR
             *  Value   Name      Description
             *    0   odr_half   BW = gyr_odr/2
             *    1  odr_quarter BW = gyr_odr/4
             */
            config.cfg.gyr.bwp = BMI3_GYR_BW_ODR_HALF;

            /* By default the gyro is disabled. Gyro is enabled by selecting the mode. */
            config.cfg.gyr.gyr_mode = BMI3_GYR_MODE_NORMAL;

            /* Value    Name    Description
             *  0b000     avg_1   No averaging; pass sample without filtering
             *  0b001     avg_2   Averaging of 2 samples
             *  0b010     avg_4   Averaging of 4 samples
             *  0b011     avg_8   Averaging of 8 samples
             *  0b100     avg_16  Averaging of 16 samples
             *  0b101     avg_32  Averaging of 32 samples
             *  0b110     avg_64  Averaging of 64 samples
             */
            config.cfg.gyr.avg_num = BMI3_GYR_AVG1;

            /* Set the gyro configurations. */
            rslt = bmi330_set_sensor_config(&config, BMI3_N_SENSE_COUNT_1, dev);
            bmi3_error_codes_print_result("Set sensor config", rslt);

            printf("*************************************\n");
            printf("Gyro configuration\n");
            printf("ODR:\t %s\n", enum_to_string(BMI3_GYR_ODR_800HZ));
            printf("Range:\t %s\n", enum_to_string(BMI3_GYR_RANGE_2000DPS));
            printf("Bandwidth:\t %s\n", enum_to_string(BMI3_GYR_BW_ODR_HALF));
            printf("Average samples:\t %s\n", enum_to_string(BMI3_GYR_AVG1));
            printf("Gyro Mode:\t %s\n", enum_to_string(BMI3_GYR_MODE_NORMAL));
            printf("Resolution:%u\n", dev->resolution);
        }
    }

    return rslt;
}

/*!
 * @brief This function converts lsb to degree per second for 16 bit gyro at
 * range 125, 250, 500, 1000 or 2000dps.
 */
static void lsb_to_dps(int16_t gyro_lsb, float dps_range, uint8_t bit_width, struct bmi3_gyro_processed_data *gyro_data)
{
    double power = 2;

    float half_scale = (float)((pow((double)power, (double)bit_width) / power));

    gyro_data->gyro_raw = (float)gyro_lsb / half_scale;
    gyro_data->gyro_dps = gyro_data->gyro_raw * dps_range;
    gyro_data->gyro_rps = (gyro_data->gyro_dps * PI) / 180;

}

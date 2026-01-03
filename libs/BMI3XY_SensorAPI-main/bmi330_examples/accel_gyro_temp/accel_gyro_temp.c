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
/*!           Static Function                                                 */

/*!
 *  @brief This internal API is used to set configurations for accel.
 *
 *  @param[in] dev       : Structure instance of bmi3_dev.
 *
 *  @return Status of execution.
 */
static int8_t set_accel_gyro_config(struct bmi3_dev *dev)
{
    /* Status of API are returned to this variable. */
    int8_t rslt;

    /* Structure to define accelerometer configuration. */
    struct bmi3_sens_config config[2];

    /* Structure to map interrupt */
    struct bmi3_map_int map_int = { 0 };

    /* Configure the type of feature. */
    config[0].type = BMI330_ACCEL;
    config[1].type = BMI330_GYRO;

    /* Get default configurations for the type of feature selected. */
    rslt = bmi330_get_sensor_config(config, BMI3_N_SENSE_COUNT_2, dev);
    bmi3_error_codes_print_result("bmi330_get_sensor_config", rslt);

    if (rslt == BMI330_OK)
    {
        map_int.acc_drdy_int = BMI3_INT1;
        map_int.gyr_drdy_int = BMI3_INT1;
        map_int.temp_drdy_int = BMI3_INT1;

        /* Map data ready interrupt to interrupt pin. */
        rslt = bmi330_map_interrupt(map_int, dev);
        bmi3_error_codes_print_result("bmi330_map_interrupt", rslt);

        if (rslt == BMI330_OK)
        {
            /* NOTE: The user can change the following configuration parameters according to their requirement. */
            /* Output Data Rate. By default ODR is set as 100Hz for accel. */
            config[0].cfg.acc.odr = BMI3_ACC_ODR_50HZ;

            /* Gravity range of the sensor (+/- 2G, 4G, 8G, 16G). */
            config[0].cfg.acc.range = BMI3_ACC_RANGE_2G;

            /* The Accel bandwidth coefficient defines the 3 dB cutoff frequency in relation to the ODR. */
            config[0].cfg.acc.bwp = BMI3_ACC_BW_ODR_QUARTER;

            /* Set number of average samples for accel. */
            config[0].cfg.acc.avg_num = BMI3_ACC_AVG64;

            /* Enable the accel mode where averaging of samples
             * will be done based on above set bandwidth and ODR.
             * Note : By default accel is disabled. The accel will get enable by selecting the mode.
             */
            config[0].cfg.acc.acc_mode = BMI3_ACC_MODE_NORMAL;

            /* Output Data Rate. By default ODR is set as 100Hz for gyro. */
            config[1].cfg.gyr.odr = BMI3_GYR_ODR_50HZ;

            /* Gyroscope Angular Rate Measurement Range. By default the range is 2000dps. */
            config[1].cfg.gyr.range = BMI3_GYR_RANGE_2000DPS;

            /*  The Gyroscope bandwidth coefficient defines the 3 dB cutoff frequency in relation to the ODR
             *  Value   Name      Description
             *    0   odr_half   BW = gyr_odr/2
             *    1  odr_quarter BW = gyr_odr/4
             */
            config[1].cfg.gyr.bwp = BMI3_GYR_BW_ODR_HALF;

            /* By default the gyro is disabled. Gyro is enabled by selecting the mode. */
            config[1].cfg.gyr.gyr_mode = BMI3_GYR_MODE_NORMAL;

            /* Value    Name    Description
             *  0b000     avg_1   No averaging; pass sample without filtering
             *  0b001     avg_2   Averaging of 2 samples
             *  0b010     avg_4   Averaging of 4 samples
             *  0b011     avg_8   Averaging of 8 samples
             *  0b100     avg_16  Averaging of 16 samples
             *  0b101     avg_32  Averaging of 32 samples
             *  0b110     avg_64  Averaging of 64 samples
             */
            config[1].cfg.gyr.avg_num = BMI3_GYR_AVG1;

            /* Set the accel and gyro configurations. */
            rslt = bmi330_set_sensor_config(config, BMI3_N_SENSE_COUNT_2, dev);
            bmi3_error_codes_print_result("bmi330_set_sensor_config", rslt);

            printf("*************************************\n");
            printf("Accel configurations\n");
            printf("ODR:\t %s\n", enum_to_string(BMI3_ACC_ODR_50HZ));
            printf("Range:\t %s\n", enum_to_string(BMI3_ACC_RANGE_2G));
            printf("Bandwidth:\t %s\n", enum_to_string(BMI3_ACC_BW_ODR_QUARTER));
            printf("Average samples:\t %s\n", enum_to_string(BMI3_ACC_AVG64));
            printf("Accel Mode:\t %s\n", enum_to_string(BMI3_ACC_MODE_NORMAL));
            printf("Resolution:%u\n", dev->resolution);

            printf("*************************************\n");
            printf("Gyro configurations\n");
            printf("ODR:\t %s\n", enum_to_string(BMI3_GYR_ODR_50HZ));
            printf("Range:\t %s\n", enum_to_string(BMI3_GYR_RANGE_2000DPS));
            printf("Bandwidth:\t %s\n", enum_to_string(BMI3_GYR_BW_ODR_HALF));
            printf("Average samples:\t %s\n", enum_to_string(BMI3_GYR_AVG1));
            printf("Gyro Mode:\t %s\n", enum_to_string(BMI3_GYR_MODE_NORMAL));
            printf("Resolution:%u\n", dev->resolution);

        }
    }

    return rslt;
}

/*! @brief Converts raw sensor values(LSB) to G value
 *
 *  @param[in] val        : Raw sensor value.
 *  @param[in] g_range    : Accel Range selected (4G).
 *  @param[in] bit_width  : Resolution of the sensor.
 *
 *  @return Accel values in Gravity(G)
 *
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

/******************************************************************************/
/*!            Functions                                                      */

/* This function starts the execution of program. */
int main(void)
{
    /* Sensor initialization configuration. */
    struct bmi3_dev dev = { 0 };

    /* Status of API are returned to this variable. */
    int8_t rslt;

    /* Variable to define limit to print accel data. */
    uint16_t limit = 100;

    /* Create an instance of sensor data structure. */
    struct bmi3_sensor_data sensor_data[3] = { 0 };

    /* Initialize the interrupt status of accel. */
    uint16_t int_status = 0;

    /* Variable to store temperature */
    float temperature_value;

    uint8_t indx = 0;
    float acc_x = 0, acc_y = 0, acc_z = 0;
    float gyr_x = 0, gyr_y = 0, gyr_z = 0;

    /* Select accel and gyro sensor. */
    sensor_data[0].type = BMI330_ACCEL;
    sensor_data[1].type = BMI330_GYRO;
    sensor_data[2].type = BMI330_TEMP;

    /* Function to select interface between SPI and I2C, according to that the device structure gets updated.
     * Interface reference is given as a parameter
     * For I2C : BMI3_I2C_INTF
     * For SPI : BMI3_SPI_INTF
     */
    rslt = bmi3_interface_init(&dev, BMI3_I2C_INTF);
    bmi3_error_codes_print_result("bmi3_interface_init", rslt);

    /* Initialize bmi330. */
    printf("Uploading configuration file\n");
    rslt = bmi330_init(&dev);
    bmi3_error_codes_print_result("bmi330_init", rslt);

    printf("Configuration file uploaded\n");
    printf("Chip ID :0x%x\n", dev.chip_id);

    if (rslt == BMI330_OK)
    {
        /* Accel and Gyro configuration settings. */
        rslt = set_accel_gyro_config(&dev);

        if (rslt == BMI330_OK)
        {
            printf("Aquisition Iteration Count : %d\n", limit);
            printf("\n%8s %10s %10s %10s %10s %10s %10s %10s %10s %10s %10s %10s %10s %10s %16s\n",
                   "Data_set",
                   "Acc_Raw_X",
                   "Acc_Raw_Y",
                   "Acc_Raw_Z",
                   "Acc_G_X",
                   "Acc_G_Y",
                   "Acc_G_Z",
                   "Gyr_Raw_X",
                   "Gyr_Raw_Y",
                   "Gyr_Raw_Z",
                   "Gyr_dps_X",
                   "Gyr_dps_Y",
                   "Gyr_dps_Z",
                   "Temp(C)",
                   "SensorTime(s)");

            while (indx <= limit)
            {
                /* To get the status of accel data ready interrupt. */
                rslt = bmi330_get_int1_status(&int_status, &dev);
                bmi3_error_codes_print_result("bmi330_get_int1_status", rslt);

                /* To check the accel data ready interrupt status and print the status for 100 samples. */
                if ((int_status & BMI3_INT_STATUS_ACC_DRDY) && (int_status & BMI3_INT_STATUS_GYR_DRDY) &&
                    (int_status & BMI3_INT_STATUS_TEMP_DRDY))
                {
                    /* Get accelerometer data for x, y and z axis. */
                    rslt = bmi330_get_sensor_data(sensor_data, BMI3_N_SENSE_COUNT_3, &dev);
                    bmi3_error_codes_print_result("bmi330_get_sensor_data", rslt);

                    /* Converting lsb to gravity for 16 bit accelerometer at 2G range. */
                    acc_x = lsb_to_g(sensor_data[0].sens_data.acc.x,
                                     BMI3_GET_RANGE_VAL(BMI3_ACC_RANGE_2G),
                                     dev.resolution);
                    acc_y = lsb_to_g(sensor_data[0].sens_data.acc.y,
                                     BMI3_GET_RANGE_VAL(BMI3_ACC_RANGE_2G),
                                     dev.resolution);
                    acc_z = lsb_to_g(sensor_data[0].sens_data.acc.z,
                                     BMI3_GET_RANGE_VAL(BMI3_ACC_RANGE_2G),
                                     dev.resolution);

                    /* Converting lsb to degree per second for 16 bit gyro at 2000dps range. */
                    gyr_x = lsb_to_dps(sensor_data[1].sens_data.gyr.x,
                                       BMI3_GET_DPS_VAL(BMI3_GYR_RANGE_2000DPS),
                                       dev.resolution);
                    gyr_y = lsb_to_dps(sensor_data[1].sens_data.gyr.y,
                                       BMI3_GET_DPS_VAL(BMI3_GYR_RANGE_2000DPS),
                                       dev.resolution);
                    gyr_z = lsb_to_dps(sensor_data[1].sens_data.gyr.z,
                                       BMI3_GET_DPS_VAL(BMI3_GYR_RANGE_2000DPS),
                                       dev.resolution);

                    temperature_value =
                        (float)((((float)((int16_t)sensor_data[2].sens_data.temp.temp_data)) / 256.0) + 23.0);

                    /* Print the accel data in gravity and gyro data in dps. */
                    /* Print the data */
                    printf(
                        "%8d %10d %10d %10d %10.2f %10.2f %10.2f %10d %10d %10d %10.2f %10.2f %10.2f %9.2f %10.4lf\n",
                        indx,
                        sensor_data[0].sens_data.acc.x,
                        sensor_data[0].sens_data.acc.y,
                        sensor_data[0].sens_data.acc.z,
                        acc_x,
                        acc_y,
                        acc_z,
                        sensor_data[1].sens_data.gyr.x,
                        sensor_data[1].sens_data.gyr.y,
                        sensor_data[1].sens_data.gyr.z,
                        gyr_x,
                        gyr_y,
                        gyr_z,
                        temperature_value,
                        (sensor_data[1].sens_data.temp.sens_time * BMI3_SENSORTIME_RESOLUTION));

                    indx++;
                }
            }
        }
    }

    bmi3_coines_deinit();

    return rslt;
}

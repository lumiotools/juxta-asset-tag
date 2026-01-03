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

/*! Earth's gravity in m/s^2 */
#define GRAVITY_EARTH  (9.80665f)

/******************************************************************************/
/*!           Static Function Declaration                                     */

/*!
 *  @brief This internal API is used to set configurations for accel.
 *
 *  @param[in] dev       : Structure instance of bmi3_dev.
 *
 *  @return Status of execution.
 */
static int8_t set_accel_config(struct bmi3_dev *dev);

/*!
 *  @brief This internal API is used to read the accel data before and after axis remap.
 *
 *  @param[in] sensor_data       : Structure instance of bmi3_sensor_data.
 *  @param[in] dev               : Structure instance of bmi3_dev.
 *
 *  @return Status of execution.
 */
static int8_t axis_remap_accel_data(struct bmi3_sensor_data *sensor_data, struct bmi3_dev *dev);

/*!
 *  @brief This internal API converts raw sensor values(LSB) to meters per seconds square.
 *
 *  @param[in] accel_lsb  : lsb sensor value.
 *  @param[in] g_range    : Accel Range selected (2G, 4G, 8G, 16G).
 *  @param[in] resolution : Resolution of the sensor.
 *  @param[in] accel_data : Structure instance of accel_Data to store the converted data .
 *
 *  @return void.
 *
 */
static void lsb_to_ms2(int16_t accel_lsb,
                       float g_range,
                       uint8_t resolution,
                       struct bmi3_accel_processed_data *accel_data);

/******************************************************************************/
/*!            Functions                                                      */

/* This function starts the execution of program. */
int main(void)
{
    /* Sensor initialization configuration. */
    struct bmi3_dev dev = { 0 };

    /* Strusture instance of axes remap */
    struct bmi3_axes_remap remap = { 0 };

    /* Status of API are returned to this variable. */
    int8_t rslt;

    /* Create an instance of sensor data structure. */
    struct bmi3_sensor_data sensor_data = { 0 };

    /* Select accel sensor. */
    sensor_data.type = BMI330_ACCEL;

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

        if (rslt == BMI330_OK)
        {
            printf("\nACCEL DATA BEFORE PERFORMING AXIS REMAP\n\n");

            rslt = axis_remap_accel_data(&sensor_data, &dev);
            bmi3_error_codes_print_result("axis_remap_accel_data", rslt);

            if (rslt == BMI330_OK)
            {
                rslt = bmi330_get_remap_axes(&remap, &dev);
                bmi3_error_codes_print_result("bmi330_get_remap_axes", rslt);

                /* @note: XYZ axis denotes x = x, y = y, z = z
                 * Similarly,
                 *    ZYX, x = z, y = y, z = x
                 */
                remap.axis_map = BMI3_MAP_ZYX_AXIS;

                /* Invert the z axis of accelerometer and gyroscope */
                remap.invert_z = BMI3_MAP_NEGATIVE;

                if (rslt == BMI330_OK)
                {
                    rslt = bmi330_set_remap_axes(remap, &dev);
                    bmi3_error_codes_print_result("set_remap_axes", rslt);

                    if (rslt == BMI330_OK)
                    {
                        printf("\nACCEL DATA AFTER PERFORMING AXIS REMAP\n");

                        rslt = axis_remap_accel_data(&sensor_data, &dev);
                        bmi3_error_codes_print_result("axis_remap_accel_data", rslt);
                    }
                }
            }
        }
    }

    bmi3_coines_deinit();

    return rslt;
}

/*!
 * @brief This internal API is used to set configurations for accel.
 */
static int8_t set_accel_config(struct bmi3_dev *dev)
{
    /* Status of API are returned to this variable. */
    int8_t rslt;

    /* Structure to define accelerometer configuration. */
    struct bmi3_sens_config config;

    /* Structure to map interrupt */
    struct bmi3_map_int map_int = { 0 };

    /* Configure the type of feature. */
    config.type = BMI330_ACCEL;

    /* Get default configurations for the type of feature selected. */
    rslt = bmi330_get_sensor_config(&config, BMI3_N_SENSE_COUNT_1, dev);
    bmi3_error_codes_print_result("bmi330_get_sensor_config", rslt);

    if (rslt == BMI330_OK)
    {
        map_int.acc_drdy_int = BMI3_INT1;

        /* Map data ready interrupt to interrupt pin. */
        rslt = bmi330_map_interrupt(map_int, dev);
        bmi3_error_codes_print_result("bmi330_map_interrupt", rslt);

        if (rslt == BMI330_OK)
        {
            /* NOTE: The user can change the following configuration parameters according to their requirement. */
            /* Output Data Rate. By default ODR is set as 100Hz for accel. */
            config.cfg.acc.odr = BMI3_ACC_ODR_100HZ;

            /* Gravity range of the sensor (+/- 2G, 4G, 8G, 16G). */
            config.cfg.acc.range = BMI3_ACC_RANGE_2G;

            /* The Accel bandwidth coefficient defines the 3 dB cutoff frequency in relation to the ODR. */
            config.cfg.acc.bwp = BMI3_ACC_BW_ODR_QUARTER;

            /* Set number of average samples for accel. */
            config.cfg.acc.avg_num = BMI3_ACC_AVG64;

            /* Enable the accel mode where averaging of samples
             * will be done based on above set bandwidth and ODR.
             * Note : By default accel is disabled. The accel will get enable by selecting the mode.
             */
            config.cfg.acc.acc_mode = BMI3_ACC_MODE_NORMAL;

            /* Set the accel configurations. */
            rslt = bmi330_set_sensor_config(&config, BMI3_N_SENSE_COUNT_1, dev);
            bmi3_error_codes_print_result("bmi330_set_sensor_config", rslt);

            printf("*************************************\n");
            printf("Accel Configurations:\n");
            printf("ODR:\t %s\n", enum_to_string(BMI3_ACC_ODR_100HZ));
            printf("Range:\t %s\n", enum_to_string(BMI3_ACC_RANGE_2G));
            printf("Bandwidth:\t %s\n", enum_to_string(BMI3_ACC_BW_ODR_QUARTER));
            printf("Average samples:\t %s\n", enum_to_string(BMI3_ACC_AVG64));
            printf("Accel Mode:\t %s\n", enum_to_string(BMI3_ACC_MODE_NORMAL));
            printf("Resolution:%u\n", dev->resolution);
            printf("\n");

        }
    }

    return rslt;
}

/*!
 * @brief This internal API converts raw sensor register content (LSB) to corresponding accel raw value, g value and m/s2 value
 */
static void lsb_to_ms2(int16_t accel_lsb,
                       float g_range,
                       uint8_t resolution,
                       struct bmi3_accel_processed_data *accel_data)
{
    double power = 2;

    float half_scale = (float)((pow((double)power, (double)resolution) / 2.0f));

    accel_data->accel_raw = (float)accel_lsb / half_scale;
    accel_data->accel_g = accel_data->accel_raw * g_range;
    accel_data->accel_ms2 = accel_data->accel_g * GRAVITY_EARTH;
}

/*!
 *@brief This internal API is used to read the accel data before and after axis remap.
 */
static int8_t axis_remap_accel_data(struct bmi3_sensor_data *sensor_data, struct bmi3_dev *dev)
{
    /* Variable to define error */
    int8_t rslt;

    /* Variable to define the number of iterations */
    uint16_t indx = 1, limit = 20;

    /* Variable that store converted Sensor Data from LSB to  (Raw, G-value and m/s2 )  */
    struct bmi3_accel_processed_data accel_data_x;
    struct bmi3_accel_processed_data accel_data_y;
    struct bmi3_accel_processed_data accel_data_z;

    /* Initialize the interrupt status of accel. */
    uint16_t int_status;

    rslt = set_accel_config(dev);
    bmi3_error_codes_print_result("set_accel_config", rslt);

    printf("Acquisition Iteration Count :%d\n", limit);
    printf(
        "SENS_TIME_LSB\t ACC_LSB_X\t ACC_LSB_Y\t ACC_LSB_Z\t ACC_RAW_X\t ACC_RAW_Y\t ACC_RAW_Z\t ACC_G_X\t ACC_G_Y\t ACC_G_Z\t ACC_MS2_X\t ACC_MS2_Y\t ACC_MS2_Z\n");

    while ((rslt == BMI330_OK) && (indx <= limit))
    {
        /* To get the status of accel data ready interrupt. */
        rslt = bmi330_get_int1_status(&int_status, dev);
        bmi3_error_codes_print_result("bmi330_get_int1_status", rslt);

        /* To check the accel data ready interrupt status and print the status for 20 samples. */
        if (int_status & BMI3_INT_STATUS_ACC_DRDY)
        {
            /* Get accelerometer data for x, y and z axis. */
            rslt = bmi330_get_sensor_data(sensor_data, BMI3_N_SENSE_COUNT_1, dev);
            bmi3_error_codes_print_result("bmi330_get_sensor_data", rslt);

            if (rslt == BMI3_OK)
            {
                /* Converting lsb to G force for 16-bit accelerometer at 2G range. */
                lsb_to_ms2(sensor_data->sens_data.acc.x,
                           BMI3_GET_RANGE_VAL(BMI3_ACC_RANGE_2G),
                           dev->resolution,
                           &accel_data_x);
                lsb_to_ms2(sensor_data->sens_data.acc.y,
                           BMI3_GET_RANGE_VAL(BMI3_ACC_RANGE_2G),
                           dev->resolution,
                           &accel_data_y);
                lsb_to_ms2(sensor_data->sens_data.acc.z,
                           BMI3_GET_RANGE_VAL(BMI3_ACC_RANGE_2G),
                           dev->resolution,
                           &accel_data_z);

                /* Print the Sensor data */
                printf(
                    #ifndef PC
                    "%8lu\t %8d\t %8d\t %8d\t %+9.3f\t %+9.3f\t %+9.3f\t %+9.3f\t %9.3f\t %+9.3f\t %+9.3f\t %+9.3f\t %+9.3f\n",
                    #else
                    "%8u\t %8d\t %8d\t %8d\t %+9.3f\t %+9.3f\t %+9.3f\t %+8.3f\t %7.3f\t %+7.3f\t %+9.3f\t %+9.3f\t %+9.3f\n",
                    #endif
                    sensor_data->sens_data.acc.sens_time,
                    sensor_data->sens_data.acc.x,
                    sensor_data->sens_data.acc.y,
                    sensor_data->sens_data.acc.z,
                    accel_data_x.accel_raw,
                    accel_data_y.accel_raw,
                    accel_data_z.accel_raw,
                    accel_data_x.accel_g,
                    accel_data_y.accel_g,
                    accel_data_z.accel_g,
                    accel_data_x.accel_ms2,
                    accel_data_y.accel_ms2,
                    accel_data_z.accel_ms2);
            }

            indx++;
        }
    }

    return rslt;
}

/**\
 * Copyright (c) 2024 Bosch Sensortec GmbH. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 **/

/******************************************************************************/
/*!                 Header Files                                              */
#include <stdio.h>
#include "bmi323.h"
#include "common.h"

/******************************************************************************/
/*!            Functions                                                      */

/* This function starts the execution of program. */
int main(void)
{
    /* Status of API are returned to this variable. */
    int8_t rslt;

    /* Sensor initialization configuration. */
    struct bmi3_dev dev = { 0 };

    /* Variable to define limit to print temperature data. */
    uint16_t limit = 50;

    /* Structure to define accelerometer configuration. */
    struct bmi3_sens_config config = { 0 };

    /* Structure to define interrupt with its feature */
    struct bmi3_map_int map_int = { 0 };

    /* Create an instance of sensor data structure. */
    struct bmi3_sensor_data sensor_data = { 0 };

    /* Initialize the interrupt status of temperature */
    uint16_t int_status;

    /* Variable to store temperature */
    float temperature_value;

    uint8_t indx = 0;

    sensor_data.type = BMI323_TEMP;

    /* Function to select interface between SPI and I2C, according to that the device structure gets updated.
     * Interface reference is given as a parameter
     * For I2C : BMI3_I2C_INTF
     * For SPI : BMI3_SPI_INTF
     */
    rslt = bmi3_interface_init(&dev, BMI3_SPI_INTF);
    bmi3_error_codes_print_result("bmi3_interface_init", rslt);

    if (rslt == BMI323_OK)
    {
        /* Initialize bmi323. */
        rslt = bmi323_init(&dev);
        bmi3_error_codes_print_result("bmi323_init", rslt);

        if (rslt == BMI323_OK)
        {
            /* Get default configurations for the type of feature selected. */
            rslt = bmi323_get_sensor_config(&config, 1, &dev);
            bmi3_error_codes_print_result("bmi323_get_sensor_config", rslt);

            /* Configure the type of feature. */
            config.type = BMI323_ACCEL;

            /* Enable accel by selecting mode. */
            config.cfg.acc.acc_mode = BMI3_ACC_MODE_NORMAL;

            if (rslt == BMI323_OK)
            {
                rslt = bmi323_set_sensor_config(&config, 1, &dev);
                bmi3_error_codes_print_result("bmi323_set_sensor_config", rslt);

                if (rslt == BMI323_OK)
                {
                    /* Select the feature and map the interrupt to pin BMI323_INT1 or BMI323_INT2. */
                    map_int.temp_drdy_int = BMI3_INT1;

                    /* Map temperature data ready interrupt to interrupt pin. */
                    rslt = bmi323_map_interrupt(map_int, &dev);
                    bmi3_error_codes_print_result("bmi323_map_interrupt", rslt);

                    printf("\nTEMP_DATA_SET, Temperature data (Degree celsius), SensorTime(secs)\n");

                    while (indx <= limit)
                    {
                        /* To get the status of temperature data ready interrupt. */
                        rslt = bmi323_get_int1_status(&int_status, &dev);
                        bmi3_error_codes_print_result("bmi323_map_interrupt", rslt);

                        if (int_status & BMI3_INT_STATUS_TEMP_DRDY)
                        {
                            /* Get temperature data. */
                            rslt = bmi323_get_sensor_data(&sensor_data, 1, &dev);
                            bmi3_error_codes_print_result("bmi323_get_sensor_data", rslt);

                            temperature_value =
                                (float)((((float)((int16_t)sensor_data.sens_data.temp.temp_data)) / 512.0) + 23.0);

                            printf("%d, %f, %.4lf\n", indx, temperature_value,
                                   (sensor_data.sens_data.temp.sens_time * BMI3_SENSORTIME_RESOLUTION));

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

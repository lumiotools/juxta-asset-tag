/**\
 * Copyright (c) 2024 Bosch Sensortec GmbH. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 **/

/******************************************************************************/
/*!                 Header Files                                              */
#include <stdio.h>
#include "bmi330.h"
#include "common.h"

/******************************************************************************/
/*!         Static Function Declaration                                       */

/*!
 *  @brief This internal API is used to set configurations for tilt interrupt.
 *
 *  @param[in] dev       : Structure instance of bmi3_dev.
 *
 *  @return Status of execution.
 */
static int8_t set_feature_config(struct bmi3_dev *dev);

/******************************************************************************/
/*!            Functions                                                      */

/* This function starts the execution of program. */
int main(void)
{
    /* Status of API are returned to this variable. */
    int8_t rslt;

    /* Variable to get tilt interrupt status. */
    uint16_t int_status = 0;

    /* Sensor initialization configuration. */
    struct bmi3_dev dev = { 0 };

    /* Feature enable initialization. */
    struct bmi3_feature_enable feature = { 0 };

    /* Interrupt mapping structure. */
    struct bmi3_map_int map_int = { 0 };

    /* Function to select interface between SPI and I2C, according to that the device structure gets updated.
     * Interface reference is given as a parameter
     * For I2C : BMI3_I2C_INTF
     * For SPI : BMI3_SPI_INTF
     */
    rslt = bmi3_interface_init(&dev, BMI3_I2C_INTF);
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
            /* Set feature configurations for tilt interrupt. */
            rslt = set_feature_config(&dev);

            if (rslt == BMI330_OK)
            {
                feature.tilt_en = BMI330_ENABLE;

                /* Enable the selected sensors. */
                rslt = bmi330_select_sensor(&feature, &dev);
                bmi3_error_codes_print_result("Sensor enable", rslt);

                if (rslt == BMI330_OK)
                {
                    printf("Tilt feature is enabled\n");
                    map_int.tilt_out = BMI3_INT1;

                    /* Map the feature interrupt for tilt feature. */
                    printf("Interrupt configuration\n");
                    rslt = bmi330_map_interrupt(map_int, &dev);
                    bmi3_error_codes_print_result("Map interrupt", rslt);

                    if (rslt == BMI330_OK)
                    {
                        printf("Interrupt Enabled: \t %s\n", enum_to_string(BMI3_TILT));
                        printf("Interrupt Mapped to: \t %s\n", enum_to_string(BMI3_INT1));
                    }

                    printf("\nTilt the board to trigger tilt event\n");

                    /* Loop to get tilt interrupt. */
                    do
                    {
                        /* Clear buffer. */
                        int_status = 0;

                        /* To get the interrupt status of tilt. */
                        rslt = bmi330_get_int1_status(&int_status, &dev);
                        bmi3_error_codes_print_result("Get interrupt status", rslt);

                        /* To check the interrupt status of tilt. */
                        if (int_status & BMI3_INT_STATUS_TILT)
                        {
                            printf("Tilt interrupt is generated\n");
                            break;
                        }
                    } while (rslt == BMI330_OK);
                }
            }
        }
    }

    bmi3_coines_deinit();

    return rslt;
}

/*!
 * @brief This internal API is used to set configurations for tilt interrupt.
 */
static int8_t set_feature_config(struct bmi3_dev *dev)
{
    /* Status of API are returned to this variable. */
    int8_t rslt;

    /* Structure to define the type of sensor and its configurations. */
    struct bmi3_sens_config config[2] = { { 0 } };

    /* Configure the type of feature. */
    config[0].type = BMI330_ACCEL;
    config[1].type = BMI330_TILT;

    /* Get default configurations for the type of feature selected. */
    rslt = bmi330_get_sensor_config(config, BMI3_N_SENSE_COUNT_2, dev);
    bmi3_error_codes_print_result("Get sensor config", rslt);

    if (rslt == BMI330_OK)
    {
        /* Enable accel by selecting the mode. */
        config[0].cfg.acc.acc_mode = BMI3_ACC_MODE_NORMAL;

        /* Set tilt configuration settings. */
        /* Duration for which the acceleration vector is averaged to be reference vector. Range = 0 to 255. */
        config[1].cfg.tilt.segment_size = 90;

        /* Minimum angle by which the device shall be tilted for event detection. Angle is computed as 256 *
         * cos(angle). Range = 0 to 255. */
        config[1].cfg.tilt.min_tilt_angle = 200;

        /* Exponential smoothing coefficient for computing low-pass mean of acceleration vector.
         * Range = 0 to 65535 */
        config[1].cfg.tilt.beta_acc_mean = 0x00FF;

        /* Set new configurations. */
        rslt = bmi330_set_sensor_config(config, BMI3_N_SENSE_COUNT_2, dev);
        bmi3_error_codes_print_result("Set sensor config", rslt);

        printf("\nAccel Configuration\n");
        printf("Accel Mode : %s\t\n", enum_to_string(BMI3_ACC_MODE_NORMAL));

        /* Set tilt configuration settings. */
        printf("*************************************\n");
        printf("Tilt Configuration:\n");
        printf("beta_acc_mean: %u\n", config[1].cfg.tilt.beta_acc_mean);
        printf("min_tilt_angle: %u\n", config[1].cfg.tilt.min_tilt_angle);
        printf("segment_size: %u\n", config[1].cfg.tilt.segment_size);
        printf("\n");

    }

    return rslt;
}

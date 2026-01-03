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
 *  @brief This internal API is used to set configurations for flat interrupt.
 *
 *  @param[in] dev       : Structure instance of bmi3_dev.
 *
 *  @return Status of execution.
 */
static int8_t set_feature_config(struct bmi3_dev *dev);

/******************************************************************************/
/*!               Functions                                                   */

/* This function starts the execution of program. */
int main(void)
{
    /* Status of API are returned to this variable. */
    int8_t rslt;

    /* Variable to get flat interrupt status. */
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

        rslt = bmi330_configure_enhanced_flexibility(&dev);
        bmi3_error_codes_print_result("bmi330_configure_enhanced_flexibility", rslt);

        if (rslt == BMI330_OK)
        {
            /* Set feature configurations for flat interrupt. */
            rslt = set_feature_config(&dev);

            if (rslt == BMI330_OK)
            {
                feature.flat_en = BMI330_ENABLE;

                /* Enable the selected sensors. */
                rslt = bmi330_select_sensor(&feature, &dev);
                bmi3_error_codes_print_result("Sensor enable", rslt);

                if (rslt == BMI330_OK)
                {
                    printf("Flat feature is enabled\n");
                    map_int.flat_out = BMI3_INT1;

                    /* Map the feature interrupt for flat feature. */
                    printf("Interrupt configuration\n");
                    rslt = bmi330_map_interrupt(map_int, &dev);
                    bmi3_error_codes_print_result("Map interrupt", rslt);

                    if (rslt == BMI330_OK)
                    {
                        printf("Interrupt Enabled: \t %s\n", enum_to_string(BMI3_FLAT));
                        printf("Interrupt Mapped to: \t %s\n", enum_to_string(BMI3_INT1));

                    }

                    printf("\nKeep the board on a flat surface\n");

                    /* Loop to get flat interrupt. */
                    do
                    {
                        /* Clear buffer. */
                        int_status = 0;

                        /* To get the interrupt status of flat. */
                        rslt = bmi330_get_int1_status(&int_status, &dev);
                        bmi3_error_codes_print_result("Get interrupt status", rslt);

                        /* To check the interrupt status of flat. */
                        if (int_status & BMI3_INT_STATUS_FLAT)
                        {
                            printf("Flat interrupt is generated\n");
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
 * @brief This internal API is used to set configurations for flat interrupt.
 */
static int8_t set_feature_config(struct bmi3_dev *dev)
{
    /* Status of API are returned to this variable. */
    int8_t rslt;

    /* Structure to define the type of sensor and its configurations. */
    struct bmi3_sens_config config[2] = { { 0 } };

    /* Configure the type of feature. */
    config[0].type = BMI330_ACCEL;
    config[1].type = BMI330_FLAT;

    /* Get default configurations for the type of feature selected. */
    rslt = bmi330_get_sensor_config(config, BMI3_N_SENSE_COUNT_2, dev);
    bmi3_error_codes_print_result("Get sensor config", rslt);

    if (rslt == BMI330_OK)
    {
        /* Enable accel by selecting the mode. */
        config[0].cfg.acc.acc_mode = BMI3_ACC_MODE_NORMAL;

        /* Maximum allowed tilt angle for device to be in flat state. Angle is computed as 64 * (tan(angle)^2). Range =
         * 0 to 63. */
        config[1].cfg.flat.theta = 9;

        /* Blocking mode to prevent change of flat status during large movement of device.
         * Value    Name        Description
         *  0b00     MODE_0   Blocking is disabled
         *  0b01     MODE_1   Block if acceleration on any axis is greater than 1.5g
         *  0b10     MODE_2   Block if acceleration on any axis is greater than 1.5g or slope is greater than half of
         *                  slope_thres
         *  0b11     MODE_3   Block if acceleration on any axis is greater than 1.5g or slope is greater than slope_thres
         */
        config[1].cfg.flat.blocking = 3;

        /* Minimum duration the device shall be in flat position for status to be asserted. Range = 0 to 255 */
        config[1].cfg.flat.hold_time = 50;

        /* Angle of hysteresis for flat detection. Range = 0 to 255 */
        config[1].cfg.flat.hysteresis = 9;

        /* Minimum slope between consecutive acceleration samples to pervent the change of flat status during large
         * movement. Range = 0 to 255 */
        config[1].cfg.flat.slope_thres = 0xCD;

        /* Set new configurations. */
        rslt = bmi330_set_sensor_config(config, BMI3_N_SENSE_COUNT_2, dev);
        bmi3_error_codes_print_result("Set sensor config", rslt);

        printf("\nAccel Configuration\n");
        printf("Accel Mode : %s\t\n", enum_to_string(BMI3_ACC_MODE_NORMAL));

        /* Set Flat configuration settings. */
        printf("*************************************\n");
        printf("\nFlat Configuration:\n");
        printf("Theta: %u\n", config[1].cfg.flat.theta);
        printf("Blocking: %u\n", config[1].cfg.flat.blocking);
        printf("Hold Time: %u\n", config[1].cfg.flat.hold_time);
        printf("Hysteresis: %u\n", config[1].cfg.flat.hysteresis);
        printf("Threshold: %u\n", config[1].cfg.flat.slope_thres);
        printf("\n");
    }

    return rslt;
}

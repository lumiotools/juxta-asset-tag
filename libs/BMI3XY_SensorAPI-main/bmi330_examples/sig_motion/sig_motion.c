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
 *  @brief This internal API is used to set configurations for sig-motion.
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

    /* Variable to get sig-motion interrupt status. */
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
            /* Set feature configurations for sig-motion. */
            rslt = set_feature_config(&dev);

            if (rslt == BMI330_OK)
            {
                feature.sig_motion_en = BMI330_ENABLE;

                /* Enable the selected sensors. */
                rslt = bmi330_select_sensor(&feature, &dev);
                bmi3_error_codes_print_result("Sensor enable", rslt);

                if (rslt == BMI330_OK)
                {
                    map_int.sig_motion_out = BMI3_INT1;

                    /* Map the feature interrupt for sig-motion. */
                    printf("Interrupt configuration\n");
                    rslt = bmi330_map_interrupt(map_int, &dev);
                    bmi3_error_codes_print_result("Map interrupt", rslt);

                    if (rslt == BMI330_OK)
                    {
                        printf("Interrupt Enabled: \t %s\n", enum_to_string(BMI3_SIG_MOTION));
                        printf("Interrupt Mapped to: \t %s\n", enum_to_string(BMI3_INT1));
                    }

                    printf("\nMove the board in any direction for 3-5s, for triggering significant motion event\n");

                    /* Loop to get sig-motion interrupt. */
                    do
                    {
                        /* Clear buffer. */
                        int_status = 0;

                        /* To get the interrupt status of sig-motion. */
                        rslt = bmi330_get_int1_status(&int_status, &dev);
                        bmi3_error_codes_print_result("Get interrupt status", rslt);

                        /* To check the interrupt status of sig-motion. */
                        if (int_status & BMI3_INT_STATUS_SIG_MOTION)
                        {
                            printf("Significant motion interrupt is generated\n");
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
 * @brief This internal API is used to set configurations for sig-motion.
 */
static int8_t set_feature_config(struct bmi3_dev *dev)
{
    /* Status of API are returned to this variable. */
    int8_t rslt;

    /* Structure to define the type of sensor and its configurations. */
    struct bmi3_sens_config config[2] = { { 0 } };

    /* Configure the type of feature. */
    config[0].type = BMI330_ACCEL;
    config[1].type = BMI330_SIG_MOTION;

    /* Get default configurations for the type of feature selected. */
    rslt = bmi330_get_sensor_config(config, BMI3_N_SENSE_COUNT_2, dev);
    bmi3_error_codes_print_result("Get sensor config", rslt);

    if (rslt == BMI330_OK)
    {
        /* Enable accel by selecting the mode. */
        config[0].cfg.acc.acc_mode = BMI3_ACC_MODE_NORMAL;

        /* Size of the segment for detection of significant motion of the device. Range = 0 to 65535. */
        config[1].cfg.sig_motion.block_size = 200;

        /* Maximum value of the peak to peak acceleration magnitude. Range = 0 to 1023. */
        config[1].cfg.sig_motion.peak_2_peak_max = 30;

        /* Minimum value of the peak to peak acceleration magnitude. Range = 0 to 1023. */
        config[1].cfg.sig_motion.peak_2_peak_min = 30;

        /* Maximum number of mean crossing per second in acceleration magnitude. Range = 0 to 62 */
        config[1].cfg.sig_motion.mcr_max = 0x10;

        /* Minimum number of mean crossing per second in acceleration magnitude. Range = 0 to 62 */
        config[1].cfg.sig_motion.mcr_min = 0x10;

        /* Set new configurations. */
        rslt = bmi330_set_sensor_config(config, BMI3_N_SENSE_COUNT_2, dev);
        bmi3_error_codes_print_result("Set sensor config", rslt);

        printf("\nAccel Configuration\n");
        printf("Accel Mode : %s\t\n", enum_to_string(BMI3_ACC_MODE_NORMAL));

        /* Set Sig motion configuration settings. */
        printf("*************************************\n");
        printf("Sig motion Configuration:\n");
        printf("Block size: %u\n", config[1].cfg.sig_motion.block_size);
        printf("Maximum peak to peak acceleration magnitude: %u\n", config[1].cfg.sig_motion.peak_2_peak_max);
        printf("Minimum peak to peak acceleration magnitude: %u\n", config[1].cfg.sig_motion.peak_2_peak_min);
        printf("Maximum Mean crossing in acceleration magnitude: %u\n", config[1].cfg.sig_motion.mcr_max);
        printf("Minimum Mean crossing in acceleration magnitude: %u\n", config[1].cfg.sig_motion.mcr_min);
        printf("\n");
    }

    return rslt;
}

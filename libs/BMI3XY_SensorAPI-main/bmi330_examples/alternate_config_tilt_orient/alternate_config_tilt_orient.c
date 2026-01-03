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
 *  @brief This internal API is used to set configurations for tilt, orientation and alternate configuration feature.
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

    /* Variable to get interrupt status. */
    uint16_t int_status = 0;

    /* Structure to store alternate status */
    struct bmi3_alt_status alt_status = { 0 };

    /* Sensor initialization configuration. */
    struct bmi3_dev dev = { 0 };

    /* Feature enable initialization. */
    struct bmi3_feature_enable feature = { 0 };

    /* Interrupt mapping structure. */
    struct bmi3_map_int map_int = { 0 };

    uint8_t tilt_count = 0, orient_count = 0;

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
            /* Set feature configurations. */
            rslt = set_feature_config(&dev);

            if (rslt == BMI330_OK)
            {
                /* Enable tilt and orientation feature */
                feature.tilt_en = BMI330_ENABLE;
                feature.orientation_en = BMI330_ENABLE;

                /* Enable the selected sensors. */
                rslt = bmi330_select_sensor(&feature, &dev);
                bmi3_error_codes_print_result("Sensor select", rslt);

                if (rslt == BMI330_OK)
                {
                    /* Select the feature and map the interrupt to pin BMI3_INT1 or BMI3_INT2 */
                    map_int.tilt_out = BMI3_INT1;
                    map_int.orientation_out = BMI3_INT1;

                    /* Map the feature interrupt. */
                    printf("Interrupt configuration\n");
                    rslt = bmi330_map_interrupt(map_int, &dev);
                    bmi3_error_codes_print_result("Map interrupt", rslt);

                    if (rslt == BMI330_OK)
                    {
                        printf("Interrupt Enabled: \t %s\n", enum_to_string(BMI3_TILT));
                        printf("Interrupt Mapped to: \t %s\n", enum_to_string(BMI3_INT1));

                        printf("Interrupt Enabled: \t %s\n", enum_to_string(BMI3_ORIENTATION));
                        printf("Interrupt Mapped to: \t %s\n", enum_to_string(BMI3_INT1));
                    }

                    printf("\nTILT/ORIENTATION ALTERNATE CONFIGURATION TEST\n\n");

                    printf("Tilt the board for tilt interrupt which runs in alternate config\n");
                    printf("Move the board at different orientations which runs in user config\n");

                    /* Delay to make sure for the user to see the current test scenario */
                    dev.delay_us(2000000, dev.intf_ptr);

                    for (;;)
                    {
                        /* Clear buffer */
                        int_status = 0;
                        alt_status.alt_accel_status = 0;
                        alt_status.alt_gyro_status = 0;

                        dev.delay_us(5000, dev.intf_ptr);

                        /* Read the interrupt status from int 1 pin */
                        rslt = bmi330_get_int1_status(&int_status, &dev);
                        bmi3_error_codes_print_result("Read interrupt status", rslt);

                        /* Check the interrupt status of tilt */
                        if (int_status & BMI3_INT_STATUS_TILT)
                        {
                            printf("\nTilt detected\n");

                            rslt = bmi330_read_alternate_status(&alt_status, &dev);
                            printf("Alternate accel status %d\n", alt_status.alt_accel_status);
                            printf("Alternate gyro status %d\n", alt_status.alt_gyro_status);

                            tilt_count++;
                        }

                        /* To check the interrupt status of orientation interrupt. */
                        if (int_status & BMI3_INT_STATUS_ORIENTATION)
                        {
                            printf("\nOrientation interrupt is generated\n");

                            rslt = bmi330_read_alternate_status(&alt_status, &dev);
                            printf("Alternate accel status %d\n", alt_status.alt_accel_status);
                            printf("Alternate gyro status %d\n", alt_status.alt_gyro_status);

                            orient_count++;
                        }

                        if ((tilt_count > 0) && (orient_count > 0))
                        {
                            break;
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
 * @brief This internal API is used to set configurations for tilt, orientation and alternate configuration feature.
 */
static int8_t set_feature_config(struct bmi3_dev *dev)
{
    /* Status of API are returned to this variable. */
    int8_t rslt;

    /* Structure to define the type of sensor and its configurations. */
    struct bmi3_sens_config config[7] = { 0 };

    /* Configure type of feature */
    config[0].type = BMI330_ACCEL;
    config[1].type = BMI330_TILT;
    config[2].type = BMI330_ORIENTATION;
    config[3].type = BMI330_ALT_AUTO_CONFIG;
    config[4].type = BMI330_ALT_ACCEL;
    config[5].type = BMI330_GYRO;
    config[6].type = BMI330_ALT_GYRO;

    /* Get default configurations for the type of feature selected. */
    rslt = bmi330_get_sensor_config(config, BMI3_N_SENSE_COUNT_7, dev);
    bmi3_error_codes_print_result("Get sensor config", rslt);

    if (rslt == BMI330_OK)
    {
        /* Sensor configuration settings */
        /* Enable accel by selecting the mode */
        config[0].cfg.acc.acc_mode = BMI3_ACC_MODE_NORMAL;

        config[0].cfg.acc.odr = BMI3_ACC_ODR_50HZ;

        printf("\nAccel Configurations:\n");
        printf("ODR : %s\t\n", enum_to_string(BMI3_ACC_ODR_50HZ));
        printf("Mode : %s\t\n", enum_to_string(BMI3_ACC_MODE_NORMAL));

        /* Set tilt configuration settings */
        config[1].cfg.tilt.segment_size = 90;
        config[1].cfg.tilt.min_tilt_angle = 200;
        config[1].cfg.tilt.beta_acc_mean = 0xF069;

        printf("\nTilt Configurations:\n");
        printf("Segment size : %d\t\n", config[1].cfg.tilt.segment_size);
        printf("Min tilt angle : %d\t\n", config[1].cfg.tilt.min_tilt_angle);
        printf("Beta acc mean : %d\t\n", config[1].cfg.tilt.beta_acc_mean);

        /* Set orientation configuration settings */
        config[2].cfg.orientation.hold_time = 4;
        config[2].cfg.orientation.hysteresis = 5;
        config[2].cfg.orientation.theta = 16;
        config[2].cfg.orientation.mode = 1;
        config[2].cfg.orientation.slope_thres = 30;
        config[2].cfg.orientation.ud_en = 1;
        config[2].cfg.orientation.blocking = 3;

        printf("\nOrientation Configurations:\n");
        printf("Hold time : %d\t\n", config[2].cfg.orientation.hold_time);
        printf("Hysteresis : %d\t\n", config[2].cfg.orientation.hysteresis);
        printf("Theta : %d\t\n", config[2].cfg.orientation.theta);
        printf("Mode : %d\t\n", config[2].cfg.orientation.mode);
        printf("Slope thres : %d\t\n", config[2].cfg.orientation.slope_thres);
        printf("Upside down : %d\t\n", config[2].cfg.orientation.ud_en);
        printf("Blocking : %d\t\n", config[2].cfg.orientation.blocking);

        /* Assign the features to user and alternate switch
         * NOTE: Any of one the feature (either tilt or orient) can be assigned to alternate configuration.
         * Eg: If tilt is assigned to alternate configuration, then orient can be assigned to user configuration and vice versa. */
        config[3].cfg.alt_auto_cfg.alt_conf_alt_switch_src_select = BMI3_ALT_TILT;
        config[3].cfg.alt_auto_cfg.alt_conf_user_switch_src_select = BMI3_ALT_ORIENT;

        printf("\nAlternate Configuration Settings:\n");
        printf("Alternate switch source select : %s\t\n", enum_to_string(BMI3_ALT_TILT));
        printf("User switch source select : %s\t\n", enum_to_string(BMI3_ALT_ORIENT));

        /* Alternate configuration settings for accel */
        config[4].cfg.alt_acc.alt_acc_mode = BMI3_ALT_ACC_MODE_NORMAL;
        config[4].cfg.alt_acc.alt_acc_odr = BMI3_ALT_ACC_ODR_400HZ;
        config[4].cfg.alt_acc.alt_acc_avg_num = BMI3_ALT_ACC_AVG4;

        printf("\nAlternate Accel Configurations:\n");
        printf("Alternate accel mode : %s\t\n", enum_to_string(BMI3_ALT_ACC_MODE_NORMAL));
        printf("Alternate accel ODR : %s\t\n", enum_to_string(BMI3_ALT_ACC_ODR_400HZ));
        printf("Alternate accel average number : %s\t\n", enum_to_string(BMI3_ALT_ACC_AVG4));

        /* Enable gyro by selecting the mode */
        config[5].cfg.gyr.gyr_mode = BMI3_GYR_MODE_NORMAL;

        config[5].cfg.gyr.odr = BMI3_GYR_ODR_50HZ;

        printf("\nGyro Configurations:\n");
        printf("Gyro mode : %s\t\n", enum_to_string(BMI3_GYR_MODE_NORMAL));
        printf("Gyro ODR : %s\t\n", enum_to_string(BMI3_GYR_ODR_50HZ));

        /* Alternate configuration settings for gyro */
        config[6].cfg.alt_gyr.alt_gyro_mode = BMI3_ALT_GYR_MODE_NORMAL;
        config[6].cfg.alt_gyr.alt_gyro_odr = BMI3_ALT_GYR_ODR_400HZ;
        config[6].cfg.alt_gyr.alt_gyro_avg_num = BMI3_ALT_GYR_AVG4;

        /* Set new configurations. */
        rslt = bmi330_set_sensor_config(config, BMI3_N_SENSE_COUNT_7, dev);
        bmi3_error_codes_print_result("Set sensor config", rslt);

        printf("\nAlternate Gyro Configurations:\n");
        printf("Alternate gyro mode : %s\t\n", enum_to_string(BMI3_ALT_GYR_MODE_NORMAL));
        printf("Alternate gyro ODR : %s\t\n", enum_to_string(BMI3_ALT_GYR_ODR_400HZ));
        printf("Alternate gyro average number : %s\t\n", enum_to_string(BMI3_ALT_GYR_AVG4));

        if (rslt == BMI330_OK)
        {
            rslt = bmi330_alternate_config_ctrl((BMI3_ALT_ACC_ENABLE | BMI3_ALT_GYR_ENABLE),
                                                BMI3_ALT_CONF_RESET_OFF,
                                                dev);
            bmi3_error_codes_print_result("Enable alternate config control", rslt);
        }
    }

    return rslt;
}

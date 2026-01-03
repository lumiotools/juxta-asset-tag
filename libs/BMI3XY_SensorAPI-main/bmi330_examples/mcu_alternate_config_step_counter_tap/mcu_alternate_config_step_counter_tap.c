/**\
 * Copyright (c) 2024 Bosch Sensortec GmbH. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 **/

#include <stdio.h>
#include "coines.h"
#include <stdlib.h>
#include "bmi330.h"
#include "common.h"

/*********************************************************************/
/*          Global variable declaration                              */
/*********************************************************************/

volatile uint8_t feat_int_status = 0;

struct bmi3_dev dev;

/* Structure to define the type of sensor and its configurations. */
struct bmi3_sens_config config[7];

/*********************************************************************/
/*          Function declaration                                     */
/*********************************************************************/

/*!
 *  @brief This internal API is used to initialize the bmi330 sensor
 *
 *  @param[in] void
 *
 *  @return void
 *
 */
static void init_bmi330(void);

/*!
 * @brief This internal API is used to set the feature interrupt status
 *
 *  @param[in] param1, param2
 *
 *  @return void
 */
static void feat_int_callback(uint32_t param1, uint32_t param2);

/*!
 * @brief This internal API is used to set the configurations for step counter, tap and alternate configuration feature.
 *
 *  @param[in] void
 *
 *  @return void
 */
static void set_feature_config(void);

/*********************************************************************/
/*           Functions                                               */
/*********************************************************************/

/*!
 *  @brief This internal API is used to initialize the bmi330 sensor
 */
static void init_bmi330(void)
{
    int8_t rslt;

    /* Initialize bmi330 sensor */
    printf("Uploading configuration file\n");
    rslt = bmi330_init(&dev);
    bmi3_error_codes_print_result("bmi330_init", rslt);

    if (rslt == BMI330_OK)
    {
        printf("BMI330 initialization success!\n");
        printf("Chip ID - 0x%x\n", dev.chip_id);
    }
    else
    {
        printf("BMI330 initialization failure!\n");
        exit(COINES_E_FAILURE);
    }
}

/*!
 * @brief This internal API is used to set the configurations for step counter, tap and alternate configuration feature.
 */
static void set_feature_config(void)
{
    /* Status of API are returned to this variable. */
    int8_t rslt;

    /* Configure the type of feature */
    config[0].type = BMI330_ACCEL;
    config[1].type = BMI330_STEP_COUNTER;
    config[2].type = BMI330_TAP;
    config[3].type = BMI330_ALT_AUTO_CONFIG;
    config[4].type = BMI330_ALT_ACCEL;
    config[5].type = BMI330_GYRO;
    config[6].type = BMI330_ALT_GYRO;

    /* Get default configurations for the type of feature selected. */
    rslt = bmi330_get_sensor_config(config, BMI3_N_SENSE_COUNT_7, &dev);
    bmi3_error_codes_print_result("Get sensor config", rslt);

    if (rslt == BMI330_OK)
    {
        /* Sensor configuration settings */
        /* Enable accel by selecting the mode */
        config[0].cfg.acc.acc_mode = BMI3_ACC_MODE_NORMAL;
        config[0].cfg.acc.odr = BMI3_ACC_ODR_100HZ;

        printf("\nAccel Configurations:\n");
        printf("Accel mode : %s\t\n", enum_to_string(BMI3_ACC_MODE_NORMAL));
        printf("Accel ODR : %s\t\n", enum_to_string(BMI3_ACC_ODR_100HZ));

        /* Enable water-mark level for to get interrupt after 20 step counts */
        config[1].cfg.step_counter.watermark_level = 1;

        config[1].cfg.step_counter.activity_detection_factor = 4;
        config[1].cfg.step_counter.activity_detection_thres = 2;
        config[1].cfg.step_counter.env_coef_down = 0xD939;
        config[1].cfg.step_counter.env_coef_up = 0xF1CC;
        config[1].cfg.step_counter.env_min_dist_down = 0x85;
        config[1].cfg.step_counter.env_min_dist_up = 0x131;
        config[1].cfg.step_counter.filter_cascade_enabled = 1;
        config[1].cfg.step_counter.mcr_threshold = 5;
        config[1].cfg.step_counter.mean_crossing_pp_enabled = 0;
        config[1].cfg.step_counter.mean_step_dur = 0xFD54;
        config[1].cfg.step_counter.mean_val_decay = 0xEAC8;
        config[1].cfg.step_counter.peak_duration_min_running = 0x0C;
        config[1].cfg.step_counter.peak_duration_min_walking = 0x0C;
        config[1].cfg.step_counter.reset_counter = 0;
        config[1].cfg.step_counter.step_buffer_size = 5;
        config[1].cfg.step_counter.step_counter_increment = 0x100;
        config[1].cfg.step_counter.step_duration_max = 0x40;
        config[1].cfg.step_counter.step_duration_pp_enabled = 1;
        config[1].cfg.step_counter.step_duration_thres = 1;
        config[1].cfg.step_counter.step_duration_window = 0x0A;

        printf("\nStep Counter Configurations:\n");
        printf("Watermark level : %d\n", config[1].cfg.step_counter.watermark_level);
        printf("Activity detection factor : %d\n", config[1].cfg.step_counter.activity_detection_factor);
        printf("Activity detection threshold : %d\n", config[1].cfg.step_counter.activity_detection_thres);
        printf("Environment coefficient down : 0x%x\n", config[1].cfg.step_counter.env_coef_down);
        printf("Environment coefficient up : 0x%x\n", config[1].cfg.step_counter.env_coef_up);
        printf("Environment minimum distance down : 0x%x\n", config[1].cfg.step_counter.env_min_dist_down);
        printf("Environment minimum distance up : 0x%x\n", config[1].cfg.step_counter.env_min_dist_up);
        printf("Filter cascade enabled : %d\n", config[1].cfg.step_counter.filter_cascade_enabled);
        printf("MCR threshold : %d\n", config[1].cfg.step_counter.mcr_threshold);
        printf("Mean crossing peak to peak enabled : %d\n", config[1].cfg.step_counter.mean_crossing_pp_enabled);
        printf("Mean step duration : 0x%x\n", config[1].cfg.step_counter.mean_step_dur);
        printf("Mean value decay : 0x%x\n", config[1].cfg.step_counter.mean_val_decay);
        printf("Peak duration minimum running : 0x%x\n", config[1].cfg.step_counter.peak_duration_min_running);
        printf("Peak duration minimum walking : 0x%x\n", config[1].cfg.step_counter.peak_duration_min_walking);
        printf("Reset counter : %d\n", config[1].cfg.step_counter.reset_counter);
        printf("Step buffer size : %d\n", config[1].cfg.step_counter.step_buffer_size);
        printf("Step counter increment : 0x%x\n", config[1].cfg.step_counter.step_counter_increment);
        printf("Step duration maximum : 0x%x\n", config[1].cfg.step_counter.step_duration_max);
        printf("Step duration peak to peak enabled : %d\n", config[1].cfg.step_counter.step_duration_pp_enabled);
        printf("Step duration threshold : %d\n", config[1].cfg.step_counter.step_duration_thres);
        printf("Step duration window : 0x%x\n", config[1].cfg.step_counter.step_duration_window);

        /* Set tap configuration settings. */

        /* Accelerometer sensing axis selection for tap detection.
         * Value    Name          Description
         * 0b00    axis_x     Use x-axis for tap detection
         * 0b01    axis_y     Use y-axis for tap detection
         * 0b10    axis_z     Use z-axis for tap detection
         * 0b11   reserved    Use z-axis for tap detection
         */
        config[2].cfg.tap.axis_sel = 1;

        /* Maximum duration between positive and negative peaks to tap */
        config[2].cfg.tap.max_dur_between_peaks = 5;

        /* Maximum duration from first tap within the second and/or third tap is expected to happen */
        config[2].cfg.tap.max_gest_dur = 0x11;

        /* Maximum number of threshold crossing expected around a tap */
        config[2].cfg.tap.max_peaks_for_tap = 5;

        /* Mimimum duration between two consecutive tap impact */
        config[2].cfg.tap.min_quite_dur_between_taps = 7;

        /* Mode for detection of tap gesture
         * Value    Name          Description
         * 0        Sensitive   Sensitive detection mode
         * 1        Normal      Normal detection mode
         * 2        Robust      Robust detection mode
         */
        config[2].cfg.tap.mode = 1;

        /* Minimum quite duration between two gestures */
        config[2].cfg.tap.quite_time_after_gest = 5;

        /* Minimum threshold for peak resulting from the tap */
        config[2].cfg.tap.tap_peak_thres = 0x2C;

        /* Maximum duration for which tap impact is observed */
        config[2].cfg.tap.tap_shock_settling_dur = 5;

        /* Perform gesture confirmation with wait time set by maximum gesture duration
         * Value    Name          Description
         * 0        Disable     Report the gesture when detected
         * 1        Enable      Report the gesture after confirmation
         */
        config[2].cfg.tap.wait_for_timeout = 1;

        printf("\nTap Configurations:\n");
        printf("Axis selection : %d\n", config[2].cfg.tap.axis_sel);
        printf("Maximum duration between peaks : %d\n", config[2].cfg.tap.max_dur_between_peaks);
        printf("Maximum gesture duration : 0x%x\n", config[2].cfg.tap.max_gest_dur);
        printf("Maximum peaks for tap : %d\n", config[2].cfg.tap.max_peaks_for_tap);
        printf("Minimum quite duration between taps : %d\n", config[2].cfg.tap.min_quite_dur_between_taps);
        printf("Mode for detection of tap gesture : %d\n", config[2].cfg.tap.mode);
        printf("Minimum quite duration between gestures : %d\n", config[2].cfg.tap.quite_time_after_gest);
        printf("Minimum threshold for peak resulting from the tap : 0x%x\n", config[2].cfg.tap.tap_peak_thres);
        printf("Maximum duration for which tap impact : observed : %d\n", config[2].cfg.tap.tap_shock_settling_dur);
        printf("Perform gesture confirmation with wait time set by maximum gesture duration : %d\n",
               config[2].cfg.tap.wait_for_timeout);

        /* Assign the features to user and alternate switch
         * NOTE: Any of one the feature (either step counter or tap) can be assigned to alternate configuration.
         * Eg: If step counter is assigned to alternate configuration, then tap can be assigned to user configuration and vice versa. */
        config[3].cfg.alt_auto_cfg.alt_conf_alt_switch_src_select = BMI3_ALT_STEP_COUNTER;
        config[3].cfg.alt_auto_cfg.alt_conf_user_switch_src_select = BMI3_ALT_TAP;

        printf("\nAlternate Configurations:\n");
        printf("Alternate switch source select : %s\t\n", enum_to_string(BMI3_ALT_STEP_COUNTER));
        printf("User switch source select : %s\t\n", enum_to_string(BMI3_ALT_TAP));

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
        config[5].cfg.gyr.odr = BMI3_GYR_ODR_100HZ;

        printf("\nGyro Configurations:\n");
        printf("Gyro mode : %s\t\n", enum_to_string(BMI3_GYR_MODE_NORMAL));
        printf("Gyro ODR : %s\t\n", enum_to_string(BMI3_GYR_ODR_100HZ));

        /* Alternate configuration settings for gyro */
        config[6].cfg.alt_gyr.alt_gyro_mode = BMI3_ALT_GYR_MODE_NORMAL;
        config[6].cfg.alt_gyr.alt_gyro_odr = BMI3_ALT_GYR_ODR_400HZ;
        config[6].cfg.alt_gyr.alt_gyro_avg_num = BMI3_ALT_GYR_AVG4;

        printf("\nAlternate Gyro Configurations:\n");
        printf("Alternate gyro mode : %s\t\n", enum_to_string(BMI3_ALT_GYR_MODE_NORMAL));
        printf("Alternate gyro ODR : %s\t\n", enum_to_string(BMI3_ALT_GYR_ODR_400HZ));
        printf("Alternate gyro average number : %s\t\n", enum_to_string(BMI3_ALT_GYR_AVG4));

        /* Set new configurations. */
        rslt = bmi330_set_sensor_config(config, BMI3_N_SENSE_COUNT_7, &dev);
        bmi3_error_codes_print_result("Set sensor config", rslt);

        if (rslt == BMI330_OK)
        {
            rslt = bmi330_alternate_config_ctrl((BMI3_ALT_ACC_ENABLE | BMI3_ALT_GYR_ENABLE),
                                                BMI3_ALT_CONF_RESET_OFF,
                                                &dev);
            bmi3_error_codes_print_result("Enable alternate config control", rslt);
        }
    }
}

/*!
 * @brief This internal API is used to set the feature interrupt status
 */
static void feat_int_callback(uint32_t param1, uint32_t param2)
{
    (void)param1;
    (void)param2;
    feat_int_status = 1;
}

/******************************************************************************/
/*!               Functions                                                   */

/* This function starts the execution of program. */
int main(void)
{
    /* Variable to define result */
    int8_t rslt;

    /* Create an instance of sensor data structure */
    struct bmi3_sensor_data sensor_data = { 0 };

    /* Interrupt mapping structure */
    struct bmi3_map_int map_int = { 0 };

    /* Structure to store alternate configuration status */
    struct bmi3_alt_status alt_status = { 0 };

    /* Variable to get feature interrupt status */
    uint16_t feat_int;

    uint8_t tap_count = 0, sc_count = 0;

    /* Feature enable initialization. */
    struct bmi3_feature_enable feature = { 0 };

    /* Structure to define interrupt pin type, mode and configurations */
    struct bmi3_int_pin_config int_cfg = { 0 };

    /* Select step counter */
    sensor_data.type = BMI330_STEP_COUNTER;

    /* Function to select interface between SPI and I2C, according to that the device structure gets updated.
     * Interface reference is given as a parameter
     * For I2C : BMI3_I2C_INTF
     * For SPI : BMI3_SPI_INTF
     */
    rslt = bmi3_interface_init(&dev, BMI3_SPI_INTF);
    bmi3_error_codes_print_result("bmi3 interface init", rslt);

    /* After sensor init introduce 200 msec sleep */
    coines_delay_msec(200);

    /* Initialize the sensor */
    init_bmi330();

    rslt = bmi330_configure_enhanced_flexibility(&dev);
    bmi3_error_codes_print_result("bmi330_configure_enhanced_flexibility", rslt);

    /* Set the configurations for step counter, tap and alternate configuration feature */
    set_feature_config();

    /* Enable step counter and tap feature */
    feature.step_counter_en = BMI330_ENABLE;
    feature.tap_detector_s_tap_en = BMI330_ENABLE;

    /* Enable the selected sensors. */
    rslt = bmi330_select_sensor(&feature, &dev);
    bmi3_error_codes_print_result("bmi330_select_sensor", rslt);

    /* Get the pin configurations */
    rslt = bmi330_get_int_pin_config(&int_cfg, &dev);
    bmi3_error_codes_print_result("bmi330_get_int_pin_config", rslt);

    /* Assign pin type */
    int_cfg.pin_type = BMI3_INT1;
    int_cfg.pin_cfg[0].output_en = BMI3_INT_OUTPUT_ENABLE;
    int_cfg.pin_cfg[0].lvl = BMI3_INT_ACTIVE_HIGH;

    rslt = bmi330_set_int_pin_config(&int_cfg, &dev);
    bmi3_error_codes_print_result("bmi330_set_int_pin_config", rslt);

    int_cfg.pin_type = BMI3_INT2;
    int_cfg.pin_cfg[1].output_en = BMI3_INT_OUTPUT_ENABLE;
    int_cfg.pin_cfg[1].lvl = BMI3_INT_ACTIVE_HIGH;

    /* Set the pin configurations */
    rslt = bmi330_set_int_pin_config(&int_cfg, &dev);
    bmi3_error_codes_print_result("bmi330_set_int_pin_config", rslt);

    /* Select the feature and map the interrupt to pin BMI330_INT1 or BMI330_INT2 */
    map_int.step_counter_out = BMI3_INT2;
    map_int.tap_out = BMI3_INT2;

    /* Map the feature interrupt. */
    rslt = bmi330_map_interrupt(map_int, &dev);
    bmi3_error_codes_print_result("Map interrupt", rslt);

    printf("\nMove the board in steps for step counter interrupt which runs in alternate config\n");
    printf("Tap the board for tap interrupt which runs in user config\n");

    coines_attach_interrupt(COINES_SHUTTLE_PIN_21, feat_int_callback, COINES_PIN_INTERRUPT_FALLING_EDGE);

    for (;;)
    {
        if (feat_int_status == 1)
        {
            feat_int_status = 0;
            alt_status.alt_accel_status = 0;
            alt_status.alt_gyro_status = 0;

            rslt = bmi330_get_int2_status(&feat_int, &dev);
            bmi3_error_codes_print_result("Read interrupt status", rslt);

            /* Check the interrupt status of the step counter */
            if (feat_int & BMI3_INT_STATUS_STEP_COUNTER)
            {
                printf("\nStep detected\n");

                /* Get step counter output */
                rslt = bmi330_get_sensor_data(&sensor_data, BMI3_N_SENSE_COUNT_1, &dev);
                bmi3_error_codes_print_result("Get sensor data", rslt);

                /* Print the step counter output */
                printf("No of steps counted  = %ld\n", (long int)sensor_data.sens_data.step_counter_output);

                rslt = bmi330_read_alternate_status(&alt_status, &dev);
                bmi3_error_codes_print_result("bmi330_read_alternate_status", rslt);

                printf("Alternate accel status %d\n", alt_status.alt_accel_status);
                printf("Alternate gyro status %d\n", alt_status.alt_gyro_status);

                config[1].cfg.step_counter.reset_counter = 1;

                /* Set new configurations. */
                rslt = bmi330_set_sensor_config(&config[1], 1, &dev);
                bmi3_error_codes_print_result("Set sensor config", rslt);

                sc_count++;
            }

            /* Check the interrupt status of the tap */
            if (feat_int & BMI3_INT_STATUS_TAP)
            {
                printf("\nTap interrupt is generated\n");

                rslt = bmi330_read_alternate_status(&alt_status, &dev);
                printf("Alternate accel status %d\n", alt_status.alt_accel_status);
                printf("Alternate gyro status %d\n", alt_status.alt_gyro_status);

                tap_count++;
            }
        }

        if ((tap_count > 0) && (sc_count > 0))
        {
            break;
        }
    }

    bmi3_coines_deinit();

    return rslt;
}

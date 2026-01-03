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
 *  @brief This internal API is used to set configurations for step counter interrupt.
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

    /* Variable to get step counter interrupt status. */
    uint16_t int_status = 0;

    /* Sensor initialization configuration. */
    struct bmi3_dev dev = { 0 };

    /* Feature enable initialization. */
    struct bmi3_feature_enable feature = { 0 };

    /* Interrupt mapping structure. */
    struct bmi3_map_int map_int = { 0 };

    /* Structure to store sensor data */
    struct bmi3_sensor_data sensor_data = { 0 };

    /* Sensor type of sensor to get data. */
    sensor_data.type = BMI330_STEP_COUNTER;

    /* Function to select interface between SPI and I2C, according to that the device structure gets updated.
     * Interface reference is given as a parameter
     * For I2C : BMI3_I2C_INTF
     * For SPI : BMI3_SPI_INTF
     */
    rslt = bmi3_interface_init(&dev, BMI3_I2C_INTF);
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
                /* Set feature configurations for step counter interrupt. */
                rslt = set_feature_config(&dev);

                if (rslt == BMI330_OK)
                {
                    feature.step_counter_en = BMI330_ENABLE;

                    /* Enable the selected sensors. */
                    rslt = bmi330_select_sensor(&feature, &dev);
                    bmi3_error_codes_print_result("Sensor enable", rslt);

                    if (rslt == BMI330_OK)
                    {
                        map_int.step_counter_out = BMI3_INT1;

                        /* Map the feature interrupt for step counter. */
                        printf("\nInterrupt configuration\n");
                        rslt = bmi330_map_interrupt(map_int, &dev);
                        bmi3_error_codes_print_result("Map interrupt", rslt);

                        if (rslt == BMI330_OK)
                        {
                            printf("Interrupt Enabled: \t %s\n", enum_to_string(BMI3_STEP_COUNTER));
                            printf("Interrupt Mapped to: \t %s\n", enum_to_string(BMI3_INT1));
                        }

                        printf(
                            "\nEmulate the step activity by -\n" "Moving the board up and down slowly, for walking,\n" "Moving the board up and down fastly, for running,\n"
                            "Keeping the board still, for stationary\n");

                        /* Loop to get step counter interrupt. */
                        do
                        {
                            /* Clear buffer. */
                            int_status = 0;

                            /* To get the interrupt status of step counter. */
                            rslt = bmi330_get_int1_status(&int_status, &dev);
                            bmi3_error_codes_print_result("Get interrupt status", rslt);

                            /* To check the interrupt status of step counter. */
                            if (int_status & BMI3_INT_STATUS_STEP_COUNTER)
                            {
                                printf("Step counter interrupt is generated\n");

                                /* Get step counter output. */
                                rslt = bmi330_get_sensor_data(&sensor_data, BMI3_N_SENSE_COUNT_1, &dev);
                                bmi3_error_codes_print_result("Get interrupt status", rslt);

                                /* Print the step counter output. */
                                printf("No of steps counted  = %ld\n",
                                       (long int)sensor_data.sens_data.step_counter_output);
                                break;
                            }
                        } while (rslt == BMI330_OK);
                    }
                }
            }
        }
    }

    bmi3_coines_deinit();

    return rslt;
}

/*!
 * @brief This internal API is used to set configurations for step counter interrupt.
 */
static int8_t set_feature_config(struct bmi3_dev *dev)
{
    /* Status of API are returned to this variable. */
    int8_t rslt;

    /* Structure to define the type of sensor and its configurations. */
    struct bmi3_sens_config config[2] = { { 0 } };

    /* Configure the type of feature. */
    config[0].type = BMI330_ACCEL;
    config[1].type = BMI330_STEP_COUNTER;

    /* Get default configurations for the type of feature selected. */
    rslt = bmi330_get_sensor_config(config, 2, dev);
    bmi3_error_codes_print_result("Get sensor config", rslt);

    if (rslt == BMI330_OK)
    {
        /* Enable accel by selecting the mode. */
        config[0].cfg.acc.acc_mode = BMI3_ACC_MODE_NORMAL;

        /* Enable water-mark level for to get interrupt after 20 step counts. */
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

        /* Set new configurations. */
        rslt = bmi330_set_sensor_config(config, BMI3_N_SENSE_COUNT_2, dev);
        bmi3_error_codes_print_result("Set sensor config", rslt);

        printf("\nAccel Configuration\n");
        printf("Accel Mode : %s\t\n", enum_to_string(BMI3_ACC_MODE_NORMAL));

        printf("\nStep Counter Configuration\n");
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
    }

    return rslt;
}

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
#define GRAVITY_EARTH                     (9.80665f)

/* Pi value used in converting gyro degrees per second to radian per second */
#define PI                                (3.14f)

/* Defines the size of the raw data buffer in the FIFO */
#define BMI330_FIFO_RAW_DATA_BUFFER_SIZE  UINT16_C(2048)

/*Defines the maximum user-defined length of raw data to be read from the FIFO */
#define BMI330_FIFO_RAW_DATA_USER_LENGTH  UINT16_C(2048)

/* Compute the maximum frame count based on the FIFO buffer size and the length of FIFO frame,
 * accounting for all possible FIFO Frame Configurations
 */
#define BMI330_MAX_ACCEL_FRAME_COUNT      UINT16_C(BMI330_FIFO_RAW_DATA_BUFFER_SIZE / BMI3_LENGTH_FIFO_ACC)
#define BMI330_MAX_GYRO_FRAME_COUNT       UINT16_C(BMI330_FIFO_RAW_DATA_BUFFER_SIZE / BMI3_LENGTH_FIFO_GYR)

/*Since temperature data is triggered in sync with the accelerometer ODR,
 * the number of instances for each would be equal to the number of accelerometer data instances.
 */
#define BMI330_MAX_TEMP_DATA_FRAME_COUNT  UINT16_C(BMI330_FIFO_RAW_DATA_BUFFER_SIZE / BMI3_LENGTH_FIFO_ACC)

/******************************************************************************/
/*!          Structure declaration                                            */

/*! Structure to define accelerometer and gyroscope configuration. */
struct bmi3_sens_config config[2];

/******************************************************************************/
/*!         Static Function Declaration                                       */

/*!
 *  @brief This internal API is used to set configurations for FIFO, accelerometer and gyroscope.
 *
 *  @param[in] dev       : Structure instance of bmi3_dev.
 *
 *  @return Status of execution.
 */
static int8_t set_sensor_fifo_config(struct bmi3_dev *dev);

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
/*!               Functions                                                   */

/* This function read accelerometer, gyroscope and temperature data from the FIFO */
int main(void)
{
    /* Sensor initialization configuration. */
    struct bmi3_dev dev;

    /* Status of API are returned to this variable. */
    int8_t rslt;

    /* Variable to index bytes. */
    uint8_t idx;

    /* Number of bytes of FIFO data */
    uint8_t fifo_data[BMI330_FIFO_RAW_DATA_BUFFER_SIZE] = { 0 };

    /* Variable that store converted Sensor Data from LSB to  (Raw, G-value and m/s2 )  */
    struct bmi3_accel_processed_data accel_data_x;
    struct bmi3_accel_processed_data accel_data_y;
    struct bmi3_accel_processed_data accel_data_z;

    /* Variable that stores converted gyro data*/
    struct bmi3_gyro_processed_data gyro_data_x;
    struct bmi3_gyro_processed_data gyro_data_y;
    struct bmi3_gyro_processed_data gyro_data_z;

    /* Array to store accelerometer data frames extracted from FIFO */
    struct bmi3_fifo_sens_axes_data fifo_accel_data[BMI330_MAX_ACCEL_FRAME_COUNT];

    /*Array to store gyroscope data frames extracted from FIFO */
    struct bmi3_fifo_sens_axes_data fifo_gyro_data[BMI330_MAX_GYRO_FRAME_COUNT];

    /* Array to store temperature data frames extracted from FIFO */
    struct bmi3_fifo_temperature_data fifo_temp_data[BMI330_MAX_TEMP_DATA_FRAME_COUNT];

    /* Initialize FIFO frame structure */
    struct bmi3_fifo_frame fifoframe = { 0 };

    /* Variable that contains interrupt status value */
    uint16_t int_status = 0;

    uint16_t fifo_length = 0;

    /* Calculate fifo_frame_length by summing up the total lengths */
    uint16_t fifo_frame_length = BMI3_LENGTH_FIFO_ACC + BMI3_LENGTH_FIFO_GYR + BMI3_LENGTH_TEMPERATURE +
                                 BMI3_LENGTH_SENSOR_TIME;

    /* Calculate fifo_frame_count */
    uint16_t fifo_frame_count = BMI330_FIFO_RAW_DATA_BUFFER_SIZE / fifo_frame_length;

    /* Variable to store temperature */
    float temperature_value;

    uint8_t count = 1;

    /* Function to select interface between SPI and I2C, according to that the device structure gets updated.
     * Interface reference is given as a parameter
     * For I2C : BMI3_I2C_INTF
     * For SPI : BMI3_SPI_INTF
     */
    rslt = bmi3_interface_init(&dev, BMI3_I2C_INTF);
    bmi3_error_codes_print_result("bmi3_interface_init", rslt);

    /* Initialize BMI330 */
    printf("Uploading configuration file\n");
    rslt = bmi330_init(&dev);
    bmi3_error_codes_print_result("bmi330_init", rslt);

    printf("Configuration file uploaded\n");
    printf("Chip ID :0x%x\n", dev.chip_id);

    /* Set the accelerometer, gyroscope and FIFO configurations */
    rslt = set_sensor_fifo_config(&dev);

    /* Update FIFO structure */
    /* Mapping the buffer to store the FIFO data. */
    fifoframe.data = fifo_data;

    /* Length of FIFO frame. */
    fifoframe.length = BMI330_FIFO_RAW_DATA_USER_LENGTH;

    while (count <= 3)
    {
        /* Read FIFO data on interrupt. */
        rslt = bmi330_get_int1_status(&int_status, &dev);
        bmi3_error_codes_print_result("bmi330_get_int1_status", rslt);

        /* To check the status of FIFO full interrupt. */
        if ((rslt == BMI330_OK) && (int_status & BMI3_INT_STATUS_FFULL))
        {
            printf("\nIteration : %d", count);
            printf("\nFIFO full interrupt occurred \n");

            rslt = bmi330_get_fifo_length(&fifoframe.available_fifo_len, &dev);
            bmi3_error_codes_print_result("bmi330_get_fifo_length", rslt);

            /* Convert available fifo length from word to byte */
            fifo_length = (uint16_t)(fifoframe.available_fifo_len * BMI3_N_SENSE_COUNT_2);

            fifoframe.length = fifo_length + dev.dummy_byte;

            printf("FIFO length in words : %d\n", fifoframe.available_fifo_len);
            printf("FIFO data bytes available : %d \n", fifo_length);
            printf("FIFO data bytes requested : %d \n", fifoframe.length);

            /* Read FIFO data */
            rslt = bmi330_read_fifo_data(&fifoframe, &dev);
            bmi3_error_codes_print_result("bmi330_read_fifo_data", rslt);

            if (rslt == BMI330_OK)
            {
                printf("\nRequested accelerometer data frames before parsing: %d\n", fifo_frame_count);

                /* Parse the FIFO data to extract accelerometer data from the FIFO buffer */
                (void)bmi330_extract_accel(fifo_accel_data, &fifoframe, &dev);
                printf("Parsed accelerometer data frames: %d\n", fifoframe.avail_fifo_accel_frames);

                printf("\nAccel data in LSB units and in Gravity\n");

                printf(
                    "SENS_TIME_LSB\t ACC_LSB_X\t ACC_LSB_Y\t ACC_LSB_Z\t ACC_RAW_X\t ACC_RAW_Y\t ACC_RAW_Z\t ACC_G_X\t ACC_G_Y\t ACC_G_Z\t ACC_MS2_X\t ACC_MS2_Y\t ACC_MS2_Z\n");

                /* Print the parsed accelerometer data from the FIFO buffer */
                for (idx = 0; idx < fifoframe.avail_fifo_accel_frames; idx++)
                {
                    /* Converting lsb to G force for 16-bit accelerometer at 2G range. */
                    lsb_to_ms2(fifo_accel_data[idx].x,
                               BMI3_GET_RANGE_VAL(BMI3_ACC_RANGE_2G),
                               dev.resolution,
                               &accel_data_x);
                    lsb_to_ms2(fifo_accel_data[idx].y,
                               BMI3_GET_RANGE_VAL(BMI3_ACC_RANGE_2G),
                               dev.resolution,
                               &accel_data_y);
                    lsb_to_ms2(fifo_accel_data[idx].z,
                               BMI3_GET_RANGE_VAL(BMI3_ACC_RANGE_2G),
                               dev.resolution,
                               &accel_data_z);

                    /* Print the data in Gravity. */
                    printf(
                        "%8u\t %8d\t %8d\t %8d\t %+9.3f\t %+9.3f\t %+9.3f\t %+8.3f\t %7.3f\t %+7.3f\t %+9.3f\t %+9.3f\t %+9.3f\n",
                        fifo_accel_data[idx].sensor_time,
                        fifo_accel_data[idx].x,
                        fifo_accel_data[idx].y,
                        fifo_accel_data[idx].z,
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

                printf("\nRequested gyro data frames before parsing: %d\n", fifo_frame_count);

                /* Parse the FIFO data to extract gyroscope data from the FIFO buffer */
                (void)bmi330_extract_gyro(fifo_gyro_data, &fifoframe, &dev);
                printf("Parsed gyroscope data frames: %d\n", fifoframe.avail_fifo_gyro_frames);

                printf("Gyro data in LSB units and degrees per second\n");
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

                /* Print the parsed gyroscope data from the FIFO buffer */
                for (idx = 0; idx < fifoframe.avail_fifo_gyro_frames; idx++)
                {
                    /* Converting lsb to degree per second for 16 bit gyro at 2000dps range */
                    lsb_to_dps(fifo_gyro_data[idx].x,
                               BMI3_GET_DPS_VAL(BMI3_GYR_RANGE_2000DPS),
                               dev.resolution,
                               &gyro_data_x);
                    lsb_to_dps(fifo_gyro_data[idx].y,
                               BMI3_GET_DPS_VAL(BMI3_GYR_RANGE_2000DPS),
                               dev.resolution,
                               &gyro_data_y);
                    lsb_to_dps(fifo_gyro_data[idx].z,
                               BMI3_GET_DPS_VAL(BMI3_GYR_RANGE_2000DPS),
                               dev.resolution,
                               &gyro_data_z);

                    printf("%10u %12d %12d %12d %12.3f %12.3f %12.3f %12.3f %12.3f %12.3f %12.3f %12.3f %12.3f\n",
                           fifo_gyro_data[idx].sensor_time,
                           fifo_gyro_data[idx].x,
                           fifo_gyro_data[idx].y,
                           fifo_gyro_data[idx].z,
                           gyro_data_x.gyro_raw,
                           gyro_data_y.gyro_raw,
                           gyro_data_z.gyro_raw,
                           gyro_data_x.gyro_dps,
                           gyro_data_y.gyro_dps,
                           gyro_data_z.gyro_dps,
                           gyro_data_x.gyro_rps,
                           gyro_data_y.gyro_rps,
                           gyro_data_z.gyro_rps);
                }

                printf("\nRequested temperature data frames before parsing: %d\n", fifo_frame_count);

                /* Parse the FIFO data to extract temperature data from the FIFO buffer */
                (void)bmi330_extract_temperature(fifo_temp_data, &fifoframe, &dev);
                printf("Parsed temperature data frames: %d\n", fifoframe.avail_fifo_temp_frames);

                printf("%-14s  %-12s  %-22s  %-6s\n", "TEMP_DATA_SET", "TEMP_DATA_LSB", "Temp(C)", "Sens_Time(lsb)");

                /* Print the parsed temperature data from the FIFO buffer */
                for (idx = 0; idx < fifoframe.avail_fifo_temp_frames; idx++)
                {
                    temperature_value = (float)((((float)((int16_t)fifo_temp_data[idx].temp_data)) / 256.0) + 23.0);
                    printf("%-14d  %-13d  %-22.2f  %-6d\n",
                           idx,
                           fifo_temp_data[idx].temp_data,
                           temperature_value,
                           fifo_temp_data[idx].sensor_time);
                }
            }

            count++;
        }
    }

    bmi3_coines_deinit();

    return rslt;
}

/*!
 * @brief This internal API is used to set configurations for accelerometer, gyroscope and FIFO.
 */
static int8_t set_sensor_fifo_config(struct bmi3_dev *dev)
{
    /* Status of API are returned to this variable. */
    int8_t rslt;

    struct bmi3_map_int map_int = { 0 };

    /* Array to define set FIFO flush */
    uint8_t data[2] = { BMI330_ENABLE, 0 };

    /* Configure the type of feature. */
    config[0].type = BMI330_ACCEL;
    config[1].type = BMI330_GYRO;

    /* NOTE: The user can change the following configuration parameters according to their requirement. */
    /* Accel configuration settings. */
    /* Output Data Rate. By default ODR is set as 100Hz for accelerometer. */
    config[0].cfg.acc.odr = BMI3_ACC_ODR_100HZ;

    /* The Accel bandwidth coefficient defines the 3 dB cutoff frequency in relation to the ODR. */
    config[0].cfg.acc.bwp = BMI3_ACC_BW_ODR_QUARTER;

    /* Set number of average samples for accel. */
    config[0].cfg.acc.avg_num = BMI3_ACC_AVG64;

    /* Gravity range of the sensor (+/- 2G, 4G, 8G, 16G). */
    config[0].cfg.acc.range = BMI3_ACC_RANGE_2G;

    /* To enable the accelerometer set the power mode to normal mode */
    config[0].cfg.acc.acc_mode = BMI3_ACC_MODE_NORMAL;

    /* Gyro configuration settings. */
    /* Output data Rate. Default ODR is 100Hz, setting to 100Hz. */
    config[1].cfg.gyr.odr = BMI3_GYR_ODR_100HZ;

    /*  The Gyroscope bandwidth coefficient defines the 3 dB cutoff frequency in relation to the ODR
     *  Value   Name      Description
     *    0   odr_half   BW = gyr_odr/2
     *    1  odr_quarter BW = gyr_odr/4
     */
    config[1].cfg.gyr.bwp = BMI3_GYR_BW_ODR_HALF;

    /* Value    Name    Description
     *  0b000     avg_1   No averaging; pass sample without filtering
     *  0b001     avg_2   Averaging of 2 samples
     *  0b010     avg_4   Averaging of 4 samples
     *  0b011     avg_8   Averaging of 8 samples
     *  0b100     avg_16  Averaging of 16 samples
     *  0b101     avg_32  Averaging of 32 samples
     *  0b110     avg_64  Averaging of 64 samples
     */
    config[1].cfg.gyr.avg_num = BMI3_GYR_AVG4;

    /* Gyroscope Angular Rate Measurement Range.By default the range is 2000dps. */
    config[1].cfg.gyr.range = BMI3_GYR_RANGE_125DPS;

    /* To enable the gyroscope set the power mode to normal mode */
    config[1].cfg.gyr.gyr_mode = BMI3_GYR_MODE_NORMAL;

    /* Set new configurations */
    rslt = bmi330_set_sensor_config(config, BMI3_N_SENSE_COUNT_2, dev);
    bmi3_error_codes_print_result("bmi330_set_sensor_config", rslt);

    printf("\nAccel Configuration\n");
    printf("ODR : %s\t\n", enum_to_string(BMI3_ACC_ODR_100HZ));
    printf("Range : %s\t\n", enum_to_string(BMI3_ACC_RANGE_2G));
    printf("Bandwidth : %s\t\n", enum_to_string(BMI3_ACC_BW_ODR_QUARTER));
    printf("Average : %s\t\n", enum_to_string(BMI3_ACC_AVG64));
    printf("Mode : %s\t\n", enum_to_string(BMI3_ACC_MODE_NORMAL));

    printf("\nGyro Configuration\n");
    printf("ODR : %s\t\n", enum_to_string(BMI3_GYR_ODR_100HZ));
    printf("Range : %s\t\n", enum_to_string(BMI3_GYR_RANGE_125DPS));
    printf("Bandwidth : %s\t\n", enum_to_string(BMI3_GYR_BW_ODR_HALF));
    printf("Average : %s\t\n", enum_to_string(BMI3_GYR_AVG4));
    printf("Mode : %s\t\n", enum_to_string(BMI3_GYR_MODE_NORMAL));

    /* To enable the accelerometer, gyroscope, temperature and sensor time in FIFO conf addr */
    rslt = bmi330_set_fifo_config(BMI3_FIFO_ALL_EN, BMI330_ENABLE, dev);
    bmi3_error_codes_print_result("bmi330_set_fifo_config", rslt);

    /* Set the FIFO flush in FIFO control register to clear the FIFO data */
    rslt = bmi330_set_regs(BMI3_REG_FIFO_CTRL, data, BMI3_N_SENSE_COUNT_2, dev);
    bmi3_error_codes_print_result("bmi330_set_regs", rslt);

    /* Map the FIFO full interrupt to INT1 */
    /* Note: User can map the interrupt to INT1 or INT2 */
    map_int.fifo_full_int = BMI3_INT1;

    /* Map the interrupt configuration */
    rslt = bmi330_map_interrupt(map_int, dev);
    bmi3_error_codes_print_result("bmi330_map_interrupt", rslt);

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

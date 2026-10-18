/*
* Copyright (c) 2021-2021 Honor Technologies Co., Ltd. All Rights Reserved.
* Description: gc5035 customsized driver,
*    otp read function is samed as it's kernel driver.
* Author: jiangkai
* Create: 2021-03-12
*/

#include "camkit_driver_impl.h"
#include "camkit_sensor_i2c.h"
#include <securec.h>
#include <linux/delay.h>

#define RETRY_TIMES 3

#define OTP_CHECK_SUCCESSED 1
#define OTP_CHECK_FAILED 0

/* SENSOR PRIVATE INFO FOR OTP SETTINGS */
#define C9910KEH_OTP_DEBUG                   0

/* DEBUG */
#if C9910KEH_OTP_DEBUG
#define C9910KEH_OTP_START_ADDR              0x0
#endif

#define C9910KEH_OTP_DATA_LENGTH             1024

/* OTP FLAG TYPE */
#define C9910KEH_OTP_FLAG_EMPTY              0x00
#define C9910KEH_OTP_FLAG_VALID              0x01
#define C9910KEH_OTP_FLAG_INVALID            0x02
#define c9910keh_otp_get_2bit_flag(flag, bit)   ((flag >> bit) & 0x03)

/* 0~4:af_data 5:af_flag 6~25:sn_data 26:sn_flag */
#define C9910KEH_OTP_BUF_SIZE                27

#define C9910KEH_OTP_ID_SIZE                 9
#define C9910KEH_OTP_ID_DATA_OFFSET          0x0020

/* OTP DPC PARAMETERS */
#define C9910KEH_OTP_DPC_FLAG_OFFSET         0x0068
#define C9910KEH_OTP_DPC_TOTAL_NUMBER_OFFSET 0x0070
#define C9910KEH_OTP_DPC_ERROR_NUMBER_OFFSET 0x0078

/* DPC STRUCTURE */
struct c9910keh_dpc_t {
    uint8 flag;
    uint16 total_num;
};

/* OTP STRUCTURE */
struct c9910keh_otp_t {
    uint8 otp_id[C9910KEH_OTP_ID_SIZE];
    uint8 otp_buf[C9910KEH_OTP_BUF_SIZE];
    struct c9910keh_dpc_t dpc;
    uint8 have_read;
};

struct c9910keh_otp_t c9910keh_otp_data;

static uint16 read_cmos_sensor(struct camkit_sensor_ctrl_t *sensor, uint32 addr)
{
    uint16 get_byte = 0;
    int rc = camkit_sensor_i2c_read(sensor, addr, &get_byte, CAMKIT_I2C_BYTE_DATA);
    if (rc != ERR_NONE)
        log_err("read failed, addr: 0x%x\n", addr);

    return get_byte;
}

static void write_cmos_sensor(struct camkit_sensor_ctrl_t *sensor,
    uint32 addr, uint32 para)
{
    int rc = camkit_sensor_i2c_write(sensor, addr, para, CAMKIT_I2C_BYTE_DATA);
    if (rc != ERR_NONE)
        log_err("write failed, addr: 0x%x, 0x%x\n", addr, para);
}

static uint8 c9910keh_otp_read_byte(struct camkit_sensor_ctrl_t *sensor, uint16 addr)
{
    write_cmos_sensor(sensor, 0xfe, 0x02);
    write_cmos_sensor(sensor, 0x69, (addr >> 8) & 0x1f);
    write_cmos_sensor(sensor, 0x6a, addr & 0xff);
    write_cmos_sensor(sensor, 0xf3, 0x20);
    return read_cmos_sensor(sensor, 0x6c);
}

static void c9910keh_otp_read_group(struct camkit_sensor_ctrl_t *sensor,
    uint16 addr, uint8 *data, uint16 length)
{
    uint16 i = 0;

    if ((((addr & 0x1fff) >> 3) + length) > C9910KEH_OTP_DATA_LENGTH) {
        log_info("out of range, start addr: 0x%.4x, length = %d\n",
            addr & 0x1fff, length);
        return;
    }

    write_cmos_sensor(sensor, 0xfe, 0x02);
    write_cmos_sensor(sensor, 0x69, (addr >> 8) & 0x1f);
    write_cmos_sensor(sensor, 0x6a, addr & 0xff);
    write_cmos_sensor(sensor, 0xf3, 0x20);
    write_cmos_sensor(sensor, 0xf3, 0x12);

    for (i = 0; i < length; i++)
        data[i] = read_cmos_sensor(sensor, 0x6c);

    write_cmos_sensor(sensor, 0xf3, 0x00);
}

static void c9910keh_gcore_read_dpc(struct camkit_sensor_ctrl_t *sensor)
{
    uint8 dpcflag = 0;
    struct c9910keh_dpc_t *pdpc = &c9910keh_otp_data.dpc;

    dpcflag = c9910keh_otp_read_byte(sensor, C9910KEH_OTP_DPC_FLAG_OFFSET);
    log_info("dpc flag = 0x%x\n", dpcflag);
    switch (c9910keh_otp_get_2bit_flag(dpcflag, 0)) {
    case C9910KEH_OTP_FLAG_EMPTY: {
        log_err("dpc info is empty!!\n");
        pdpc->flag = C9910KEH_OTP_FLAG_EMPTY;
        break;
    }
    case C9910KEH_OTP_FLAG_VALID: {
        log_info("dpc info is valid!\n");
        pdpc->total_num =
            c9910keh_otp_read_byte(sensor,
            C9910KEH_OTP_DPC_TOTAL_NUMBER_OFFSET)
            + c9910keh_otp_read_byte(sensor,
            C9910KEH_OTP_DPC_ERROR_NUMBER_OFFSET);
        pdpc->flag = C9910KEH_OTP_FLAG_VALID;
        log_info("total_num = %d\n", pdpc->total_num);
        break;
    }
    default:
        pdpc->flag = C9910KEH_OTP_FLAG_INVALID;
        break;
    }
}

static uint8 c9910keh_otp_read_sensor_info(struct camkit_sensor_ctrl_t *sensor)
{
    uint8 moduleid = 0;
#if C9910KEH_OTP_DEBUG
    uint16 i = 0;
    uint8 debug[C9910KEH_OTP_DATA_LENGTH];
#endif

    c9910keh_gcore_read_dpc(sensor);

#if C9910KEH_OTP_DEBUG
    (void)memset_s(&debug[0], C9910KEH_OTP_DATA_LENGTH, 0, C9910KEH_OTP_DATA_LENGTH);
    c9910keh_otp_read_group(sensor, C9910KEH_OTP_START_ADDR, &debug[0], C9910KEH_OTP_DATA_LENGTH);
    for (i = 0; i < C9910KEH_OTP_DATA_LENGTH; i++)
        log_info("addr = 0x%x, data = 0x%x\n", C9910KEH_OTP_START_ADDR + i * 8, debug[i]);
#endif

    return moduleid;
}

static void c9910keh_otp_update_dd(struct camkit_sensor_ctrl_t *sensor)
{
    uint8 state = 0;
    uint8 n = 0;
    struct c9910keh_dpc_t *pdpc = &c9910keh_otp_data.dpc;

    if (pdpc->flag == C9910KEH_OTP_FLAG_VALID) {
        log_info("DD auto load start!\n");
        write_cmos_sensor(sensor, 0xfe, 0x02);
        write_cmos_sensor(sensor, 0xbe, 0x00);
        write_cmos_sensor(sensor, 0xa9, 0x01);
        write_cmos_sensor(sensor, 0x09, 0x33);
        write_cmos_sensor(sensor, 0x01, (pdpc->total_num >> 8) & 0x07);
        write_cmos_sensor(sensor, 0x02, pdpc->total_num & 0xff);
        write_cmos_sensor(sensor, 0x03, 0x00);
        write_cmos_sensor(sensor, 0x04, 0x80);
        write_cmos_sensor(sensor, 0x95, 0x0a);
        write_cmos_sensor(sensor, 0x96, 0x30);
        write_cmos_sensor(sensor, 0x97, 0x0a);
        write_cmos_sensor(sensor, 0x98, 0x32);
        write_cmos_sensor(sensor, 0x99, 0x07);
        write_cmos_sensor(sensor, 0x9a, 0xa9);
        write_cmos_sensor(sensor, 0xf3, 0x80);
        while (n < 3) {
            state = read_cmos_sensor(sensor, 0x06);
            if ((state | 0xfe) == 0xff)
                mdelay(10);
            else
                n = 3;
            n++;
        }
        write_cmos_sensor(sensor, 0xbe, 0x01);
        write_cmos_sensor(sensor, 0x09, 0x00);
        write_cmos_sensor(sensor, 0xfe, 0x01);
        write_cmos_sensor(sensor, 0x80, 0x02);
        write_cmos_sensor(sensor, 0xfe, 0x00);
    }
}

static void c9910keh_otp_update(struct camkit_sensor_ctrl_t *sensor)
{
    c9910keh_otp_update_dd(sensor);
}

static struct camkit_i2c_reg enable_otp[] = {
    { 0xfc, 0x01, 0x00 },
    { 0xf4, 0x40, 0x00 },
    { 0xf5, 0xe9, 0x00 },
    { 0xf6, 0x14, 0x00 },
    { 0xf8, 0x40, 0x00 },
    { 0xf9, 0x82, 0x00 },
    { 0xfa, 0x00, 0x00 },
    { 0xfc, 0x81, 0x00 },
    { 0xfe, 0x00, 0x00 },
    { 0x36, 0x01, 0x00 },
    { 0xd3, 0x87, 0x00 },
    { 0x36, 0x00, 0x00 },
    { 0x33, 0x00, 0x00 },
    { 0xf7, 0x01, 0x00 },
    { 0xfc, 0x8e, 0x00 },
    { 0xfe, 0x00, 0x00 },
    { 0xee, 0x30, 0x00 },
    { 0xfa, 0x10, 0x00 },
    { 0xf5, 0xe9, 0x00 },
    { 0xfe, 0x02, 0x00 },
    { 0x67, 0xc0, 0x00 },
    { 0x59, 0x3f, 0x00 },
    { 0x55, 0x80, 0x00 },
    { 0x65, 0x80, 0x00 },
    { 0x66, 0x03, 0x00 },
    { 0xfe, 0x00, 0x00 },
};

static struct camkit_i2c_reg disable_otp[] = {
    { 0xfe, 0x02, 0x00 },
    { 0x67, 0x00, 0x00 },
    { 0xfe, 0x00, 0x00 },
    { 0xfa, 0x00, 0x00 },
};

static void c9910keh_otp_identify(struct camkit_sensor_ctrl_t *sensor)
{
    int32 rc;

    if (c9910keh_otp_data.have_read)
        return;

    /* Enable otp read */
    rc = camkit_sensor_write_table(sensor, enable_otp,
            camkit_array_size(enable_otp), CAMKIT_I2C_BYTE_DATA);
    if (rc != ERR_NONE) {
        log_info("enable otp read failed\n");
        return;
    }

    /* read otp group id */
    c9910keh_otp_read_group(sensor, C9910KEH_OTP_ID_DATA_OFFSET,
        &c9910keh_otp_data.otp_id[0],
        C9910KEH_OTP_ID_SIZE);

    c9910keh_otp_read_sensor_info(sensor);

    /* Disable otp read */
    rc = camkit_sensor_write_table(sensor, disable_otp,
            camkit_array_size(disable_otp), CAMKIT_I2C_BYTE_DATA);
    if (rc != ERR_NONE) {
        log_err("disable otp read failed\n");
        return;
    }
    c9910keh_otp_data.have_read = 1;
}

static void c9910keh_otp_function(struct camkit_sensor_ctrl_t *sensor)
{
    uint8 i = 0, flag = 0;
    uint8 otp_id[C9910KEH_OTP_ID_SIZE];

    (void)memset_s(&otp_id, C9910KEH_OTP_ID_SIZE, 0, C9910KEH_OTP_ID_SIZE);

    write_cmos_sensor(sensor, 0xfa, 0x10);
    write_cmos_sensor(sensor, 0xf5, 0xe9);
    write_cmos_sensor(sensor, 0xfe, 0x02);
    write_cmos_sensor(sensor, 0x67, 0xc0);
    write_cmos_sensor(sensor, 0x59, 0x3f);
    write_cmos_sensor(sensor, 0x55, 0x80);
    write_cmos_sensor(sensor, 0x65, 0x80);
    write_cmos_sensor(sensor, 0x66, 0x03);
    write_cmos_sensor(sensor, 0xfe, 0x00);

    c9910keh_otp_read_group(sensor, C9910KEH_OTP_ID_DATA_OFFSET,
        &otp_id[0], C9910KEH_OTP_ID_SIZE);
    for (i = 0; i < C9910KEH_OTP_ID_SIZE; i++)
        if (otp_id[i] != c9910keh_otp_data.otp_id[i]) {
            flag = 1;
            break;
        }

    if (flag == 1) {
        log_err("otp id mismatch, read again");
        (void)memset_s(&c9910keh_otp_data, sizeof(struct c9910keh_otp_t), 0, sizeof(c9910keh_otp_data));
        for (i = 0; i < C9910KEH_OTP_ID_SIZE; i++)
            c9910keh_otp_data.otp_id[i] = otp_id[i];
        c9910keh_otp_read_sensor_info(sensor);
    }

    c9910keh_otp_update(sensor);

    write_cmos_sensor(sensor, 0xfe, 0x02);
    write_cmos_sensor(sensor, 0x67, 0x00);
    write_cmos_sensor(sensor, 0xfe, 0x00);
    write_cmos_sensor(sensor, 0xfa, 0x00);
}

static uint32 c9910keh_m100_brt_open(struct camkit_sensor *sensor)
{
    uint32 rc;
    struct camkit_params *kit_params = NULL;
    struct camkit_sensor_params *sensor_params = NULL;
    log_info("Enter");
    if (!sensor) {
        log_err("Invalid ptr\n");
        return ERR_IO;
    }
    kit_params = sensor->kit_params;
    return_err_if_null(kit_params);
    sensor_params = kit_params->sensor_params;
    return_err_if_null(sensor_params);

    rc = camkit_open(sensor);
    if (rc != ERR_NONE) {
        log_err("Camkit open failed\n");
        return rc;
    }

    c9910keh_otp_function(&sensor_params->sensor_ctrl);
    log_info("EXIT");

    return ERR_NONE;
}

static uint32 match_sensor_id(struct camkit_params *params)
{
    int32 rc;
    uint8 i = 0;
    uint8 retry = RETRY_TIMES;
    uint8 size;
    uint16 sensor_id = 0;
    struct camkit_sensor_params *sensor_params = params->sensor_params;
    struct camkit_module_params *module_params = params->module_params;
    struct camkit_sensor_info_t *sensor_info = &sensor_params->sensor_info;
    uint16 expect_id = sensor_info->sensor_id;
    enum camkit_i2c_data_type data_type = sensor_info->sensor_id_dt;

    spin_lock(&camkit_lock);
    /* init i2c config */
    sensor_params->sensor_ctrl.i2c_speed = sensor_info->i2c_speed;
    sensor_params->sensor_ctrl.addr_type = sensor_info->addr_type;
    spin_unlock(&camkit_lock);

    size = camkit_array_size(sensor_info->i2c_addr_table);
    log_info("sensor i2c addr num = %u", size);

    while ((i < size) && (sensor_info->i2c_addr_table[i] != 0xff)) {
        spin_lock(&camkit_lock);
        sensor_params->sensor_ctrl.i2c_write_id =
            sensor_info->i2c_addr_table[i];
        spin_unlock(&camkit_lock);
        do {
            log_info("to match sensor: %s", module_params->sensor_name);

            if (!data_type)
                rc = camkit_sensor_i2c_read(&sensor_params->sensor_ctrl,
                    sensor_info->sensor_id_reg,
                    &sensor_id, CAMKIT_I2C_WORD_DATA);
            else
                rc = camkit_sensor_i2c_read(&sensor_params->sensor_ctrl,
                    sensor_info->sensor_id_reg,
                    &sensor_id, data_type);
            if (rc == ERR_NONE && sensor_id == expect_id) {
                log_info("sensor id: 0x%x matched", sensor_id);
                return ERR_NONE;
            }
            retry--;
        } while (retry > 0);
        i++;
        retry = RETRY_TIMES;
    }

    log_info("sensor id mismatch, expect:0x%x, real:0x%x",
        expect_id, sensor_id);

    return ERR_IO;
}

static uint32 match_module_id(struct camkit_module_params *params)
{
    int32 rc;
    uint16 module_code = 0;
    uint16 expect_code = params->module_code;
    uint8 i2c_addr = params->eeprom_i2c_addr;
    uint16 module_addr = params->module_code_addr;
    uint16 lens_addr = params->lens_type_addr;
    uint16 expect_lens = params->lens_type;
    uint16 lens_type = 0;
    uint8 retry = RETRY_TIMES;

    if (params->skip_module_id) {
        log_info("not to match module code");
        return ERR_NONE;
    }

    do {
        rc = camkit_i2c_read(i2c_addr, module_addr, params->addr_type,
            &module_code, params->data_type);
        if (rc == ERR_NONE && module_code == expect_code) {
            log_info("module code: 0x%x matched", module_code);
                if (params->lens_type_addr) {
                rc = camkit_i2c_read(i2c_addr, lens_addr, params->addr_type,
                    &lens_type, params->data_type);
                if (rc == ERR_NONE && lens_type == expect_lens) {
                    log_info("lens type: 0x%x matched", lens_type);
                    return ERR_NONE;
                }
            } else {
                return ERR_NONE;
            }
        }
        retry--;
    } while (retry > 0);

    log_info("module code, expect:0x%x, real:0x%x",
        expect_code, module_code);
    log_info("lens type, expect:0x%x, real:0x%x",
        expect_lens, lens_type);

    return ERR_IO;
}

uint32 c9910keh_m100_brt_match_id(struct camkit_sensor *sensor,
    uint32 *match_id)
{
    uint32 rc;
    struct camkit_params *kit_params = NULL;
    struct camkit_sensor_params *sensor_params = NULL;

    return_err_if_null(sensor);
    kit_params = sensor->kit_params;

    return_err_if_null(kit_params);
    return_err_if_null(kit_params->sensor_params);
    return_err_if_null(kit_params->module_params);

    rc = match_sensor_id(kit_params);
    if (rc != ERR_NONE) {
        *match_id = 0xFFFFFFFF;
        return rc;
    }

    rc = match_module_id(kit_params->module_params);
    if (rc != ERR_NONE) {
        *match_id = 0xFFFFFFFF;
        return rc;
    }

    sensor_params = kit_params->sensor_params;
    c9910keh_otp_identify(&sensor_params->sensor_ctrl); /* read otp */

    *match_id = kit_params->module_params->match_id;
    log_info("match id ok, sensor id: 0x%x, module: 0x%x, match id: 0x%x",
        kit_params->sensor_params->sensor_info.sensor_id,
        kit_params->module_params->module_code,
        kit_params->module_params->match_id);

    return ERR_NONE;
}

static struct sensor_kit_ops c9910keh_m100_brt_ops = {
    .sensor_open = c9910keh_m100_brt_open,
    .sensor_close = camkit_close,
    .match_id = c9910keh_m100_brt_match_id,
    .sensor_init = camkit_sensor_init,
    .get_sensor_info = camkit_get_sensor_info,
    .control = camkit_control,
    .get_scenario_pclk = camkit_get_scenario_pclk,
    .get_scenario_period = camkit_get_scenario_period,
    .set_test_pattern = camkit_set_test_pattern,
    .dump_reg = camkit_dump_reg,
    .set_auto_flicker = camkit_set_auto_flicker,
    .get_default_framerate = camkit_get_default_framerate,
    .get_crop_info = camkit_get_crop_info,
    .streaming_control = camkit_streaming_control,
    .get_mipi_pixel_rate = camkit_get_mipi_pixel_rate,
    .get_mipi_trail_val = camkit_get_mipi_trail_val,
    .get_sensor_pixel_rate = camkit_get_sensor_pixel_rate,
    .get_pdaf_capacity = camkit_get_pdaf_capacity,
    .get_binning_ratio = camkit_get_binning_ratio,
    .get_pdaf_info = camkit_get_pdaf_info,
    .get_vc_info = camkit_get_vc_info,
    .get_pdaf_regs_data = camkit_get_pdaf_regs_data,
    .set_pdaf_setting = camkit_set_pdaf_setting,
    .set_video_mode = camkit_set_video_mode,
    .set_shutter = camkit_set_shutter,
    .set_gain = camkit_set_gain,
    .set_dummy = camkit_set_dummy,
    .set_max_framerate = camkit_set_max_framerate,
    .set_scenario_framerate = camkit_set_scenario_framerate,
    .set_shutter_frame_length = camkit_set_shutter_frame_length,
    .set_current_fps = camkit_set_current_fps,
    .set_pdaf_mode = camkit_set_pdaf_mode,
};

uint32 get_c9910keh_m100_brt_ops(struct sensor_kit_ops **ops)
{
    if (ops != NULL) {
        *ops = &c9910keh_m100_brt_ops;
    } else {
        log_err("get c9910keh_m100_brt_ops operators fail");
        return ERR_INVAL;
    }
    (void)memset_s(&c9910keh_otp_data, sizeof(struct c9910keh_otp_t), 0, sizeof(c9910keh_otp_data));
    log_info("get c9910keh_m100_brt operators OK");
    return ERR_NONE;
}

register_customized_driver(
    c9910keh_m100_brt,
    CAMKIT_SENSOR_IDX_SUB,
    C9910KEH_M100_BRT_SENSOR_ID,
    get_c9910keh_m100_brt_ops);

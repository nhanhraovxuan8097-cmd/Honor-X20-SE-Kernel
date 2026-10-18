/*
 * brt_cal_list.h
 *
 * Copyright (c) 2021-2021 Hihonor Technologies Co., Ltd.
 *
 * brt_cal_list
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#ifndef _BRT_CAL_LIST_H_
#define _BRT_CAL_LIST_H_

// main
static struct eeprom_hw_i2c_reg brt_main_eeprom_map[] = {
	{ 0x00, EEPROM_I2C_WORD_ADDR, 4498,
		EEPROM_I2C_BYTE_DATA, EEPROM_I2C_READ, 0x00 },
};

static struct eeprom_hw_map brt_main_eeprom_map_table = {
	.map = brt_main_eeprom_map,
	.map_size = ARRAY_SIZE(brt_main_eeprom_map),
};

// front
static struct eeprom_hw_i2c_reg brt_front_eeprom_map[] = {
	{ 0x00, EEPROM_I2C_WORD_ADDR, 2431,
		EEPROM_I2C_BYTE_DATA, EEPROM_I2C_READ, 0x00 },
};

static struct eeprom_hw_map brt_front_eeprom_map_table = {
	.map = brt_front_eeprom_map,
	.map_size = ARRAY_SIZE(brt_front_eeprom_map),
};

static struct eeprom_hw_i2c_reg brt_micro_eeprom_map[] = {
	{ 0x00, EEPROM_I2C_WORD_ADDR, 2431,
		EEPROM_I2C_BYTE_DATA, EEPROM_I2C_READ, 0x00 },
};

static struct eeprom_hw_map brt_micro_eeprom_map_table = {
	.map = brt_micro_eeprom_map,
	.map_size = ARRAY_SIZE(brt_micro_eeprom_map),
};

struct stCAM_CAL_LIST_STRUCT brt_camCalList[] = {
	{ // main-13m
		.sensorID = C9908UAI_M190_BRT_SENSOR_ID,
		.slaveID = 0xA0,
		.maxEepromSize = 0x1192,
		.eeprom_hw_map = &brt_main_eeprom_map_table,
		.getCamCalData = eeprom_hw_get_data,
	}, {
		.sensorID = C9908XBA_M180_BRT_SENSOR_ID,
		.slaveID = 0xA2,
		.maxEepromSize = 0x1192,
		.eeprom_hw_map = &brt_main_eeprom_map_table,
		.getCamCalData = eeprom_hw_get_data,
	}, { // front-5m
		.sensorID = C9910HQF_M180_BRT_SENSOR_ID,
		.slaveID = 0xA0,
		.maxEepromSize = 0x097F,
		.eeprom_hw_map = &brt_front_eeprom_map_table,
		.getCamCalData = eeprom_hw_get_data,
	}, {
		.sensorID = C9910KEH_M100_BRT_SENSOR_ID,
		.slaveID = 0xA0,
		.maxEepromSize = 0x097F,
		.eeprom_hw_map = &brt_front_eeprom_map_table,
		.getCamCalData = eeprom_hw_get_data,
	}, { // macro
		.sensorID = C9909RWP_M320_BRT_SENSOR_ID,
		.slaveID = 0xA4,
		.maxEepromSize = 0x097F,
		.eeprom_hw_map = &brt_micro_eeprom_map_table,
		.getCamCalData = eeprom_hw_get_data,
	}, {
		.sensorID = C9909QUV_M180_BRT_SENSOR_ID,
		.slaveID = 0xA0,
		.maxEepromSize = 0x097F,
		.eeprom_hw_map = &brt_micro_eeprom_map_table,
		.getCamCalData = eeprom_hw_get_data,
	},

	// ADD before this line
	{ 0, 0, 0, 0 } // end of list
};

#endif // _BRT_CAL_LIST_H_

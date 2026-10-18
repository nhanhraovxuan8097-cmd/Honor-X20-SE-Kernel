/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2020-2020. All rights reserved.
 * Description: als route header file
 * Author: wangsiwen
 * Create: 2020-10-29
 */

#ifndef __ALS_ROUTE_H__
#define __ALS_ROUTE_H__

#define MAX_STR_SIZE 1024
#define ALS_UNDER_TP_CALDATA_SIZE 59
#define OEMINFO_ALS_UNDER_TP_CALIDATA 16
#define HALF_LENGTH 45

struct bright_data {
	uint32_t mipi_data;
	uint32_t bright_data;
	uint64_t time_stamp;
};

ssize_t als_under_tp_calidata_store(int32_t tag, struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size);
ssize_t als_under_tp_calidata_show(int32_t sensor_type, struct device *dev,
	struct device_attribute *attr, char *buf);
ssize_t als_ud_rgbl_status_show(int32_t tag, struct device *dev,
	struct device_attribute *attr, char *buf);
ssize_t als_rgb_status_store(int32_t tag, struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size);
int als_oeminfo_write(uint32_t id, const void *buf, uint32_t buf_size);
int als_oeminfo_read(uint32_t id, void *buf, uint32_t buf_size);
int store_data_to_share_mem(uint8_t *buf, size_t len);
void save_light_to_sensorhub(uint32_t mipi_level, uint32_t bl_level);
#endif

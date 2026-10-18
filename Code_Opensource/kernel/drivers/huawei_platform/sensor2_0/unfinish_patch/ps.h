/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2012-2018. All rights reserved.
 * Description: ps APP header file
 */

#ifndef __PS_H__
#define __PS_H__

#include <app.h>
#include <basic_sensors.h>

#define PS_SOUND_INTERVAL 20
#define LOOP_THRESHOLD 32
#define SENSOR_DATA_FACTOR 100
#define FLAT_ANGLE 180
#define PEI 3.14
#define ACC_RATIO (1.0f / 1000 / 1024 * 9.8)
#define DYN_THRESHOLD 90
#define HANDUP_TIME 10
#define SH_PHONECALL_COUNT 5
#define FORCE_SCREEN_OFF_COUNT 3
#define SCREEN_ON_OUT_COUNT 4
#define STRUCT_DATA_NUM 3
#define PHC_NUM_MAX 11
#define LOOP_THRESHOLD 32

/*
 * Discription: phonecall gesture angle limits
 * limits: flat_gesture -30 <= pitch <= 10 and -15 <= roll <= 15
 *         left_face_gesture -145 < pitch < -15 and -90 < roll < -20
 *         right_face_gesture -145 < pitch < -15 and 2- < roll < 90
 * return value: none
 */
#define flat_gesture(g_pitch, g_roll) \
	((g_pitch >= -30) && (g_pitch <= 10) && (g_roll >= -15) && (g_roll <= 15))
#define left_face_gesture(g_pitch, g_roll) \
	((g_pitch > -145) && (g_pitch < -15) && (g_roll > -90) && (g_roll < -20))
#define right_face_gesture(g_pitch, g_roll) \
	((g_pitch > -145) && (g_pitch < -15) && (g_roll > 20) && (g_roll < 90))
#define no_left_face_gesture(g_pitch, g_roll) \
	((g_pitch <= -145) || (g_pitch >= -15) || (g_roll <= -90) || (g_roll >= -20))
#define no_right_face_gesture(g_pitch, g_roll) \
	((g_pitch <= -145) || (g_pitch >= -15) || (g_roll >= 90) || (g_roll <= 20))

enum phonecall_dyn_detection {
	DYN_DET_DATA_0,
	DYN_DET_DATA_1,
	DYN_DET_DATA_2,
	DYN_DET_DATA_3,
	DYN_DET_DATA_4,
	DYN_DET_DATA_5,
	DYN_DET_DATA_6,
	DYN_DET_DATA_7,
	DYN_DET_DATA_8,
	DYN_DET_DATA_9,
	DYN_DET_DATA_MAX,
};

struct ps_data_t {
	UINT32 data[STRUCT_DATA_NUM];
};

enum ps_nearby_type {
	PS_NEAR_BY,
	PS_FAR_AWAY,
	PS_NEAR_BY_UNKNOWN
};

enum ps_data_number_type {
	PS_DATA_X,
	PS_DATA_Y,
	PS_DATA_Z
};

enum ps_rcv_type {
    PS_RCV_OFF,
    PS_RCV_ON,
    PS_RCV_UNKNOWN,
};

enum ps_event_t {
	PS_EVENT_START = APP_PRIVATE_EVENT,
	PS_ABNORMAL_EVENT = APP_ABNORMAL_EVENT,
	PS_SOUND_INTERRUPT,
	PS_SOUND_TIMER_TIMEOUT,
	PS_SOUND_RECEIVE_DATA,
	PS_SOUND_SENSOR_BROKE,
	PS_MTP_NOTIFY_HIFI,
	MTP_PIKEUP_EVENT,
	PS_FINGER_IDENTIFY_SUCCESS,
	PS_GET_TEMP_TIMER
};

struct accel_data_ps_t {
	INT32 data[STRUCT_DATA_NUM];
};

struct ultra_axis_t {
	float x;
	float y;
	float z;
};

struct app_ps_priv {
	int (*p_ps_notify)(enum ps_event_t event, void *body, UINT32 timeout);
	int (*p_is_single_sound)(void);
	struct ps_data_t ultrasonic_immediate_data;
	UINT8 channel_type;
	UINT8 ps_status;
};

struct ps_gesture_data_t {
	UINT8 ps_gesture_data_old;
	UINT8 ps_gesture_count;
	UINT8 acc_data_ready;
	UINT8 ps_data_old;
	UINT8 ps_data_once;
	UINT8 ps_upload_aod_old;
};

struct phonecall_mutex_data_t {
	UINT8 enter_mutex_count;
	UINT8 left_phonecallmutex;
	UINT8 right_phonecallmutex;
	UINT8 out_left_mutex_count;
	UINT8 out_right_mutex_count;
};

 struct phonecall_act_data_t {
	INT32 dyn_detection[DYN_DET_DATA_MAX];
	INT32 temp_value[PS_DATA_Z];
	INT32 phonecall_det_res;
	UINT8 dyn_data_num;
	UINT8 handup_judge;
	UINT8 phc_start_count;
	UINT8 ps_top_shelter;
	UINT8 shelter_count;
	UINT8 shelter_down_count;
};

#endif


#ifndef _CAM_DMD_UTILS_H_
#define _CAM_DMD_UTILS_H_

#include "../cam_hiview/cam_hiview.h"

typedef enum {
	DRIVER_ERROR_TYPE_START = 0,
	SET_INIT_SETTING_FAILED = DRIVER_ERROR_TYPE_START,
	SET_CONFIG_SETTING_FAILED,
	SET_STREAMON_SETTING_FAILED,
	SET_STREAMOFF_SETTING_FAILED,
	SET_READ_SETTING_FAILED,
	BTB_GPIO_CHECK_FAILED,
	EEPROM_READ_MEMORY_FAILED,
	DRIVER_ERROR_TYPE_END,
} driver_error_type;

void camkit_hiview_report(int error_no, const char *ic_name, driver_error_type error_type);
void camkit_hiview_report_id(int error_no, uint16_t ic_id, driver_error_type error_type);

#endif /* _CAM_DMD_UTILS_H_ */

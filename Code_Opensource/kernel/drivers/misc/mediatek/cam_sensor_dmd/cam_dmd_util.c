

#include "cam_dmd_util.h"
#include "securec.h"

/* Ic name max length */
#define CAM_IC_NAME_LENGTH 32

const char* g_driver_error_info[DRIVER_ERROR_TYPE_END] = {
	"(set init setting fail)",
	"(set resolution config setting fail)",
	"(set stream on setting fail)",
	"(set stream off setting fail)",
	"(set read setting fail)",
	"(btb gpio check fail)",
	"(eeprom read memory fail)",
};

void camkit_hiview_report(int error_no, const char *ic_name,
	driver_error_type error_type)
{
	errno_t ret;
	struct cam_hievent_info driver_info;
	const char* error_info = NULL;
	ret = memset_s(&driver_info, sizeof(driver_info), 0, sizeof(driver_info));
	if (ret != EOK) {
		//CAM_ERR(CAM_DMD, "memset_s fail");
		return;
	}

	if (error_type >= DRIVER_ERROR_TYPE_START &&
		error_type < DRIVER_ERROR_TYPE_END)
		error_info = g_driver_error_info[error_type];

	driver_info.error_no = error_no;
	cam_hiview_get_module_name(ic_name, &driver_info);
	cam_hiview_get_content(error_no, error_info, &driver_info);
	cam_hiview_report(&driver_info);
}

void camkit_hiview_report_id(int error_no, uint16_t ic_id,
	driver_error_type error_type)
{
	char ic_name[CAM_IC_NAME_LENGTH];
	sprintf(ic_name, "0x%x", ic_id);
	camkit_hiview_report(error_no, ic_name, error_type);
}

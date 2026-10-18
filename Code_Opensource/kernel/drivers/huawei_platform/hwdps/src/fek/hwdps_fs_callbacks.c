/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2020-2020. All rights reserved.
 * Description: This file contains the function required for operations
 *              about map.
 * Create: 2020-06-16
 */

#include "inc/fek/hwdps_fs_callbacks.h"
#include <linux/cred.h>
#include <linux/dcache.h>
#include <linux/file.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/printk.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <securec.h>
#include <huawei_platform/hwdps/hwdps_ioctl.h>
#include <huawei_platform/hwdps/hwdps_limits.h>
#include "inc/base/hwdps_defines.h"
#include "inc/base/hwdps_utils.h"
#include "inc/data/hwdps_data.h"
#include "inc/data/hwdps_packages.h"
#include "inc/fek/hwdps_fs_callbacks.h"
#include "inc/policy/hwdps_policy.h"
#include "inc/tee/hwdps_alg.h"
#include "inc/tee/hwdps_keyinfo.h"

#define VERSION_LENGTH 1
#define AES_IV_LENGTH 16
#define FEK_LENGTH 64
#define APP_UID_LENGTH 4
#define PATH_NO_EXT_SDCARD 2
#define PATH_OF_INSTALLD "/system/bin/installd"
#define PATH_LEN_OF_INSTALLD 20

void safe_free(void **pointer)
{
	if (!pointer || !(*pointer))
		return;
	kzfree(*pointer);
	*pointer = NULL;
}

static hwdps_result_t get_path(const struct dentry *dentry, u8 *path)
{
	u8 *path_tmp = NULL;
	hwdps_result_t ret = HWDPS_SUCCESS;
	u8 *buf = kzalloc(PATH_MAX, GFP_NOFS);

	if (!buf)
		goto err;
	path_tmp = dentry_path_raw((struct dentry *)dentry, buf, PATH_MAX);
	if (IS_ERR(path_tmp) || (strlen(path_tmp) >= PATH_MAX))
		goto err;
	if (memcpy_s(path, PATH_MAX, path_tmp, strlen(path_tmp)) != EOK)
		goto err;
	goto out;
err:
	ret = HWDPS_ERR_GET_PATH;
out:
	safe_free((void **)&buf);
	return ret;
}

static hwdps_result_t precheck_new_key_data(const struct dentry *dentry,
	uid_t uid, const s8 *fsname)
{
	struct package_hashnode_t *package_node = NULL;
	struct package_info_listnode_t *package_info_node = NULL;
	struct package_info_listnode_t *tmp = NULL;
	hwdps_result_t res;
	u8 *path = NULL;

	hwdps_data_read_lock();
	package_node = get_hwdps_package_hashnode(uid);
	if (!package_node) {
		res = -HWDPS_ERR_NOT_SUPPORTED;
		goto out;
	}
	path = kzalloc(PATH_MAX, GFP_NOFS);
	if (!path) {
		res = -HWDPS_ERR_INTERNAL;
		hwdps_pr_err("path alloc failed\n");
		goto out;
	}

	res = get_path(dentry, path);
	if (res != HWDPS_SUCCESS)
		goto out;
	list_for_each_entry_safe(package_info_node, tmp,
		&package_node->pinfo_list, list) {
		if (hwdps_evaluate_policies(package_info_node, fsname,
			path)) {
			res = HWDPS_SUCCESS; /* success case */
			goto out;
		}
	}
	hwdps_pr_err("hwdps %s failed\n", __func__);
	res = -HWDPS_ERR_NOT_SUPPORTED; /* policy never matched */
out:
	safe_free((void **)&path);
	hwdps_data_read_unlock();
	return res;
}

static hwdps_result_t precheck_existing_key_data(const encrypt_id *id)
{
	if (id->task_uid == id->uid)
		return HWDPS_SUCCESS;
	hwdps_pr_err("%s uid and task uid error\n", __func__);
	return -HWDPS_ERR_NOT_OWNER;
}

static bool is_create_fek_param_invalid(u8 *desc, const struct dentry *dentry,
	secondary_buffer_t *encoded_wfek, secondary_buffer_t *fek)
{
	return (!desc || !dentry || !encoded_wfek || !encoded_wfek->data ||
		!encoded_wfek->len || !fek || !fek->data || !fek->len);
}

static hwdps_result_t create_fek_inner(u8 *desc, uid_t uid,
	secondary_buffer_t *fek, secondary_buffer_t *encoded_wfek)
{
	buffer_t fek_temp = { NULL, 0 };
	hwdps_result_t res = HWDPS_SUCCESS;

	hwdps_pr_debug("%s enter\n", __func__);
	*encoded_wfek->len = sizeof(struct xattribs_t);
	fek_temp.data = kzalloc(sizeof(s8) * HWDPS_FEK_SIZE, GFP_NOFS);
	fek_temp.len = HWDPS_FEK_SIZE;
	*encoded_wfek->data = kzalloc(sizeof(u8) * (*encoded_wfek->len),
		GFP_NOFS);
	if ((*encoded_wfek->data == NULL) || (fek_temp.data == NULL)) {
		res = -HWDPS_ERR_NO_MEMORY;
		goto err;
	}

	if (kernel_new_fek(desc, uid, encoded_wfek, &fek_temp)
		!= ERR_MSG_SUCCESS) {
		hwdps_pr_err("kernel_new_fek err\n");
		res = -HWDPS_ERR_INTERNAL;
		goto err;
	}
	*fek->data = fek_temp.data;
	fek_temp.data = NULL;
	*fek->len = HWDPS_FEK_SIZE;
	*encoded_wfek->len = sizeof(struct xattribs_t);
	goto done;
err:
	safe_free((void **)encoded_wfek->data);
done:
	safe_free((void **)&fek_temp.data);
	return res;
}

/* This function is called when a new inode is created. */
static hwdps_result_t handle_create_fek(u8 *desc, const u8 *fsname,
	const struct dentry *dentry, secondary_buffer_t *encoded_wfek,
	secondary_buffer_t *fek)
{
	hwdps_result_t res;
	const struct cred *cred = NULL;
	uid_t uid;

	if (is_create_fek_param_invalid(desc, dentry, encoded_wfek, fek))
		return HWDPS_ERR_INVALID_ARGS;
	cred = get_current_cred();
	if (!cred)
		return HWDPS_ERR_INVALID_ARGS;
	uid = cred->uid.val; /* task uid */
	put_cred(cred);
	res = precheck_new_key_data(dentry, uid, fsname);
	if (res != HWDPS_SUCCESS)
		return res;

	res = create_fek_inner(desc, uid, fek, encoded_wfek);
	if (res != HWDPS_SUCCESS)
		hwdps_pr_err("create_fek_inner returned %d\n", res);
	else
		hwdps_pr_debug("create_fek_inner returned success\n");
	return res;
}

#ifdef CONFIG_HWDPS_ENG_DEBUG
static bool is_pid_privilege(pid_t pid)
{
	s8 task_name[TASK_COMM_LEN] = {0};
	size_t priv_task_len;
	s32 priv_num;

	/*
	 * lists cmds have privilege
	 * echo --> "sh"
	 * adb pull --> "sync svc 44", "sync svc 66"
	 */
	static const s8 * const priv_cmds[] = {
		"cat", "sh", "sync svc"
	};
	get_task_comm(task_name, current);
	priv_num = ARRAY_SIZE(priv_cmds);
	for (s32 i = 0; i < priv_num; ++i) {
		priv_task_len = strlen(priv_cmds[i]);
		if (!strncmp(priv_cmds[i], task_name, priv_task_len))
			return true;
	}
	hwdps_pr_info("task is not privilege\n");
	return false;
}
#endif

static bool is_pid_installd(pid_t pid)
{
	return hwdps_utils_exe_check(pid, PATH_OF_INSTALLD,
		PATH_LEN_OF_INSTALLD);
}

static hwdps_result_t handle_hwdps_has_access(encrypt_id *id,
	buffer_t *encoded_wfek)
{
	hwdps_result_t res;

	hwdps_pr_debug("%s enter\n", __func__);

	if (!id || !encoded_wfek || !encoded_wfek->data ||
		(encoded_wfek->len != HWDPS_ENCODED_WFEK_SIZE)) {
		hwdps_pr_err("HWDPS_ERR_INVALID_ARGS error\n");
		return -HWDPS_ERR_INVALID_ARGS;
	}
	res = precheck_existing_key_data(id);
	if (res != HWDPS_SUCCESS) {
		hwdps_pr_err("precheck_existing_key_data error\n");
		goto priv;
	}
	res = HWDPS_SUCCESS;
	goto out;
priv:
#ifdef CONFIG_HWDPS_ENG_DEBUG
	if (is_pid_privilege(id->pid) || is_pid_installd(id->pid))
#else
	if (is_pid_installd(id->pid))
#endif
		res = HWDPS_SUCCESS;
out:
	return res;
}

static void check_ret_values(hwdps_result_t *res_inout,
	const secondary_buffer_t *fek)
{
	if (*res_inout != 0 || !fek || (*fek->len != HWDPS_FEK_SIZE)) {
		if (*res_inout > 0) {
			*res_inout = -*res_inout;
			hwdps_pr_err("res %d fek len %u\n", *res_inout,
				*fek->len);
		}
	}
}

static hwdps_result_t handle_get_fek(u8 *desc, encrypt_id *id,
	buffer_t *encoded_wfek, secondary_buffer_t *fek)
{
	hwdps_result_t res;

	hwdps_pr_debug("%s enter\n", __func__);
	if (!id) {
		hwdps_pr_err("id is null\n");
		return HWDPS_ERR_INVALID_ARGS;
	}

	res = precheck_existing_key_data(id);
	if (res != HWDPS_SUCCESS) {
		hwdps_pr_err("precheck_existing_key_data res %d\n", -res);
		goto priv;
	}

	res = kernel_get_fek(desc, id->task_uid, encoded_wfek, fek);
	check_ret_values(&res, fek);
	if (res != HWDPS_SUCCESS)
		goto err;

	res = HWDPS_SUCCESS;
	goto out;

priv:
#ifdef CONFIG_HWDPS_ENG_DEBUG
	if (((*fek->data == NULL) || (*fek->len == 0)) &&
		(is_pid_privilege(id->pid) || is_pid_installd(id->pid))) {
#else
	if (((*fek->data == NULL) || (*fek->len == 0)) &&
		is_pid_installd(id->pid)) {
#endif
		res = kernel_get_fek(desc, id->uid, encoded_wfek, fek);
		if (res == HWDPS_SUCCESS)
			goto out;
	}
err:
	safe_free((void **)fek->data);
	*fek->len = 0;
out:
	return (res > 0) ? -res : res;
}

static hwdps_result_t handle_update_fek(u8 *desc, buffer_t *encoded_wfek,
		secondary_buffer_t *fek, uid_t new_uid, uid_t old_uid)
{
	hwdps_result_t res;

	hwdps_pr_debug("%s new uid %lld old uid %lld\n", __func__,
		new_uid, old_uid);
	res = kernel_update_fek(desc, encoded_wfek, fek, new_uid,
		old_uid);
	check_ret_values(&res, fek);
	if (res != HWDPS_SUCCESS)
		goto err;

	res = HWDPS_SUCCESS;
	goto out;

err:
	safe_free((void **)fek);
	*fek->len = 0;
out:
	return (res > 0) ? -res : res;
}

struct hwdps_fs_callbacks_t g_fs_callbacks = {
	.create_fek = handle_create_fek,
	.hwdps_has_access = handle_hwdps_has_access,
	.get_fek = handle_get_fek,
	.update_fek = handle_update_fek,
};

void hwdps_register_fs_callbacks_proxy(void)
{
	hwdps_register_fs_callbacks(&g_fs_callbacks);
}

void hwdps_unregister_fs_callbacks_proxy(void)
{
	hwdps_unregister_fs_callbacks();
}

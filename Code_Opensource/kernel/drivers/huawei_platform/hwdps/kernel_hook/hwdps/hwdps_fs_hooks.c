/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2020-2020. All rights reserved.
 * Description: Hwdps hooks for file system(f2fs).
 * Create: 2020-06-16
 */

#include "huawei_platform/hwdps/hwdps_fs_hooks.h"
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/rwsem.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <log/hiview_hievent.h>
#include <securec.h>
#include <uapi/linux/stat.h>
#include <huawei_platform/hwdps/fscrypt_private.h>
#include <huawei_platform/hwdps/hwdps_limits.h>

#ifdef CONFIG_HWDPS
static DECLARE_RWSEM(g_fs_callbacks_lock);

#define HWDPS_XATTR_NAME "hwdps"
#define HWDPS_KEY_DESC_STANDARD_FLAG 0x42

#define HWDPS_HIVIEW_ID 940016001
#define HWDPS_HIVIEW_CREATE 1000
#define HWDPS_HIVIEW_OPEN 1001
#define HWDPS_HIVIEW_UPDATE 1002
#define HWDPS_HIVIEW_ACCESS 1003

static hwdps_result_t default_create_fek(u8 *desc, const u8 *fsname,
	const struct dentry *dentry, secondary_buffer_t *encoded_wfek,
	secondary_buffer_t *fek)
{
	(void)desc;
	(void)fsname;
	(void)dentry;
	(void)encoded_wfek;
	(void)fek;
	return -HWDPS_ERR_NO_FS_CALLBACKS;
}

static hwdps_result_t default_hwdps_has_access(encrypt_id *id,
	buffer_t *encoded_wfek)
{
	(void)id;
	(void)encoded_wfek;
	return -HWDPS_ERR_NO_FS_CALLBACKS;
}

static hwdps_result_t default_get_fek(u8 *desc, encrypt_id *id,
	buffer_t *encoded_wfek, secondary_buffer_t *fek)
{
	(void)desc;
	(void)id;
	(void)encoded_wfek;
	(void)fek;
	return -HWDPS_ERR_NO_FS_CALLBACKS;
}

static hwdps_result_t default_update_fek(u8 *desc, buffer_t *encoded_wfek,
	secondary_buffer_t *fek, uid_t new_uid, uid_t old_uid)
{
	(void)desc;
	(void)encoded_wfek;
	(void)fek;
	(void)new_uid;
	(void)old_uid;
	return -HWDPS_ERR_NO_FS_CALLBACKS;
}

static struct hwdps_fs_callbacks_t g_fs_callbacks = {
	.create_fek = default_create_fek,
	.hwdps_has_access = default_hwdps_has_access,
	.get_fek = default_get_fek,
	.update_fek = default_update_fek,
};

static hwdps_result_t hwdps_create_fek(u8 *desc, struct inode *inode,
	const struct dentry *dentry, secondary_buffer_t *encoded_wfek,
	secondary_buffer_t *fek)
{
	hwdps_result_t res;
	const u8 *fsname = NULL;

	if (!inode || !inode->i_sb || !inode->i_sb->s_type)
		return -HWDPS_ERR_INVALID_ARGS;

	fsname = inode->i_sb->s_type->name;
	down_read(&g_fs_callbacks_lock);
	res = g_fs_callbacks.create_fek(desc, fsname, dentry,
		encoded_wfek, fek);
	up_read(&g_fs_callbacks_lock);

	return res;
}

static void hiview_for_hwdps(int type, hwdps_result_t result, encrypt_id *id)
{
	struct hiview_hievent *event = hiview_hievent_create(HWDPS_HIVIEW_ID);

	if (!event) {
		pr_err("hwdps hiview event null");
		return;
	}

	pr_info("%s result %d, %d, %lld, %lld\n", __func__,
		type, result, id->uid, id->task_uid);
	hiview_hievent_put_integral(event, "type", type);
	hiview_hievent_put_integral(event, "result", result);
	hiview_hievent_put_integral(event, "task_uid", id->task_uid);
	hiview_hievent_put_integral(event, "uid", id->uid);

	hiview_hievent_report(event);
	hiview_hievent_destroy(event);
}

static encrypt_id get_create_task_uid()
{
	encrypt_id id = {0};
	const struct cred *cred = get_current_cred();

	if (!cred) {
		pr_err("%s cred error\n", __func__);
		return id;
	}
	id.task_uid = cred->uid.val; /* task uid */
	put_cred(cred);
	id.uid = id.task_uid;
	return id;
}

hwdps_result_t hwdps_has_access(struct inode *inode, buffer_t *encoded_wfek)
{
	hwdps_result_t res;
	const struct cred *cred = NULL;
	encrypt_id id;

	if (!inode)
		return -HWDPS_ERR_INVALID_ARGS;

	id.pid = task_tgid_nr(current);
	cred = get_current_cred();
	if (!cred)
		return -HWDPS_ERR_INVALID_ARGS;

	id.task_uid = cred->uid.val;
	put_cred(cred);
	id.uid = inode->i_uid.val;

	down_read(&g_fs_callbacks_lock);
	res = g_fs_callbacks.hwdps_has_access(&id, encoded_wfek);
	up_read(&g_fs_callbacks_lock);

	if (res != 0)
		hiview_for_hwdps(HWDPS_HIVIEW_ACCESS, res, &id);
	return res;
}

hwdps_result_t hwdps_get_fek(u8 *desc, struct inode *inode,
	buffer_t *encoded_wfek, secondary_buffer_t *fek)
{
	encrypt_id ids;
	const struct cred *cred = NULL;
	hwdps_result_t res = -HWDPS_ERR_INVALID_ARGS;

	if (!inode)
		goto out;

	ids.pid = task_tgid_nr(current);
	cred = get_current_cred();
	if (!cred)
		goto out;

	ids.task_uid = cred->uid.val; /* task uid */
	ids.uid = inode->i_uid.val; /* file uid */
	put_cred(cred);

	down_read(&g_fs_callbacks_lock);
	res = g_fs_callbacks.get_fek(desc, &ids, encoded_wfek, fek);
	up_read(&g_fs_callbacks_lock);

out:
	if (res != 0)
		hiview_for_hwdps(HWDPS_HIVIEW_OPEN, res, &ids);
	return res;
}

s32 hwdps_check_support(struct inode *inode)
{
	s32 err = 0;
	u32 flags;

	if (!inode || !inode->i_sb->s_cop->get_hwdps_flags)
		return -EOPNOTSUPP;

	/*
	 * The inode->i_crypt_info->ci_hw_enc_flag keeps sync with the
	 * flags in xattr_header. And it can not be changed once the
	 * file is opened.
	 */
	if (!inode->i_crypt_info)
		err = inode->i_sb->s_cop->get_hwdps_flags(inode, NULL, &flags);
	else
		flags = (u32)(inode->i_crypt_info->ci_hw_enc_flag);
	if (err != 0)
		pr_err("hwdps ino %lu get flags err %d\n", inode->i_ino, err);
	else if (flags != HWDPS_XATTR_ENABLE_FLAG)
		err = -EOPNOTSUPP;

	return err;
}

uint8_t *hwdps_do_get_attr(struct inode *inode, size_t size)
{
	s32 err;
	uint8_t *wfek = NULL;

	if (size != HWDPS_ENCODED_WFEK_SIZE) {
		pr_err("%s size err %d\n", __func__, size);
		return NULL;
	}

	if (!inode || !inode->i_sb || !inode->i_sb->s_cop ||
		!inode->i_sb->s_cop->get_hwdps_attr)
		return NULL;

	wfek = kmalloc(size, GFP_NOFS);
	if (!wfek)
		return NULL;

	err = inode->i_sb->s_cop->get_hwdps_attr(inode, wfek, size);
	if (err == -ENODATA) {
		pr_err("hwdps ino %lu hwdps xattr is null\n", inode->i_ino);
		goto free_out;
	} else if (err != HWDPS_ENCODED_WFEK_SIZE) {
		pr_err("hwdps ino %lu wrong encoded_wfek size %d\n",
			inode->i_ino, err);
		goto free_out;
	}
	return wfek;

free_out:
	kzfree(wfek);
	return NULL;
}

static s32 f2fs_set_hwdps_enable_flags(struct inode *inode, void *fs_data)
{
	u32 flags = 0;
	s32 res;

	res = inode->i_sb->s_cop->get_hwdps_flags(inode, fs_data, &flags);
	if (res != 0) {
		pr_err("%s get inode %lu hwdps flags res %d\n",
			__func__, inode->i_ino, res);
		return -EINVAL;
	}
	flags |= HWDPS_XATTR_ENABLE_FLAG;
	res = inode->i_sb->s_cop->set_hwdps_flags(inode, fs_data, &flags);
	if (res != 0) {
		pr_err("%s set inode %lu hwdps flag res %d\n",
			__func__, inode->i_ino, res);
		return -EINVAL;
	}

	return res;
}

s32 hwdps_inherit_context(struct inode *parent, struct inode *inode,
	const struct dentry *dentry, void *fs_data)
{
	uint8_t *encoded_wfek = NULL;
	uint8_t *fek = NULL;
	uint32_t encoded_len = 0;
	uint32_t fek_len = 0;
	encrypt_id id;
	secondary_buffer_t buffer_fek = { &fek, &fek_len };
	secondary_buffer_t buffer_wfek = { &encoded_wfek, &encoded_len };
	s32 err;
	struct fscrypt_info *ci = NULL;

	if (!dentry || !inode || !fs_data || !parent)
		return -EAGAIN;
	ci = parent->i_crypt_info;
	if (ci == NULL) {
		pr_err("hwdps parent ci is null error\n");
		return -ENOKEY;
	}
	if (!S_ISREG(inode->i_mode))
		return 0;
	err = hwdps_create_fek(ci->ci_master_key_descriptor,
		inode, dentry, &buffer_wfek, &buffer_fek);
	if (err == -HWDPS_ERR_NOT_SUPPORTED) {
		pr_info_once("hwdps ino %lu not protected\n", inode->i_ino);
		err = 0;
		goto free_buf;
	}
	if (err != HWDPS_SUCCESS) {
		pr_err("hwdps ino %lu create fek err %d\n", inode->i_ino, err);
		id = get_create_task_uid();
		hiview_for_hwdps(HWDPS_HIVIEW_CREATE, err, &id);
		goto free_buf;
	}
	if (parent->i_sb->s_cop->set_hwdps_attr) {
		err = parent->i_sb->s_cop->set_hwdps_attr(inode, encoded_wfek,
			encoded_len, fs_data);
	} else {
		pr_info("hwdps ino %lu no setxattr\n", inode->i_ino);
		err = 0;
		goto free_hwdps;
	}
	if (err != 0) {
		pr_err("hwdps ino %lu setxattr err %d\n", inode->i_ino, err);
		goto free_hwdps;
	}
	err = f2fs_set_hwdps_enable_flags(inode, fs_data);
	if (err != 0)
		pr_err("hwdps ino %lu set hwdps enable flags err %d\n",
			inode->i_ino, err);
free_hwdps:
	kzfree(encoded_wfek);
free_buf:
	kzfree(fek);
	return err;
}

int hwdps_update_fek(u8 *desc, buffer_t *encoded_wfek,
	secondary_buffer_t *fek, uid_t new_uid, uid_t old_uid)
{
	hwdps_result_t res;

	if (!desc || !encoded_wfek || !fek)
		return HWDPS_ERR_INVALID_ARGS;

	down_read(&g_fs_callbacks_lock);
	res = g_fs_callbacks.update_fek(desc, encoded_wfek, fek,
		new_uid, old_uid);
	up_read(&g_fs_callbacks_lock);

	return res;
}

void hiview_for_hwdps_update(struct inode *inode, int err, uid_t new_uid)
{
	encrypt_id id;

	if (err == 0)
		return;
	if (!inode)
		return;
	id.uid = new_uid;
	id.task_uid = inode->i_uid.val;
	hiview_for_hwdps(HWDPS_HIVIEW_UPDATE, err, &id);
}

void hwdps_register_fs_callbacks(struct hwdps_fs_callbacks_t *callbacks)
{
	down_write(&g_fs_callbacks_lock);
	if (callbacks) {
		if (callbacks->create_fek)
			g_fs_callbacks.create_fek = callbacks->create_fek;
		if (callbacks->hwdps_has_access)
			g_fs_callbacks.hwdps_has_access =
				callbacks->hwdps_has_access;
		if (callbacks->get_fek)
			g_fs_callbacks.get_fek = callbacks->get_fek;
		if (callbacks->update_fek)
			g_fs_callbacks.update_fek = callbacks->update_fek;
	}
	up_write(&g_fs_callbacks_lock);
}
EXPORT_SYMBOL(hwdps_register_fs_callbacks);

void hwdps_unregister_fs_callbacks(void)
{
	down_write(&g_fs_callbacks_lock);
	g_fs_callbacks.create_fek = default_create_fek;
	g_fs_callbacks.hwdps_has_access = default_hwdps_has_access;
	g_fs_callbacks.get_fek = default_get_fek;
	g_fs_callbacks.update_fek = default_update_fek;
	up_write(&g_fs_callbacks_lock);
}
EXPORT_SYMBOL(hwdps_unregister_fs_callbacks);

#endif

/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2020-2020. All rights reserved.
 * Description: This file contains the function required for generate key and
 *              encrypt key management.
 * Create: 2020-06-16
 */

#include "inc/tee/hwdps_keyinfo.h"
#include <keys/user-type.h>
#include <linux/key.h>
#include <linux/uaccess.h>
#include <linux/init.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <securec.h>
#include "inc/base/hwdps_defines.h"
#include "inc/base/hwdps_utils.h"
#include "inc/tee/base_alg.h"
#include "inc/tee/hwdps_alg.h"

#define FS_KEY_DESCRIPTOR_LEN ((FS_KEY_DESCRIPTOR_SIZE * 2) + 1)

extern struct key_type key_type_logon;

static struct key *fscrypt_request_key(const u8 *descriptor, const u8 *prefix,
	s32 prefix_size)
{
	u8 *full_key_descriptor = NULL;
	struct key *keyring_key = NULL;
	s32 full_key_len = prefix_size + FS_KEY_DESCRIPTOR_LEN;

	if (full_key_len <= 0)
		return NULL;

	full_key_descriptor = kmalloc(full_key_len, GFP_NOFS);
	if (!full_key_descriptor)
		return (struct key *)ERR_PTR(-ENOMEM);

	if (memcpy_s(full_key_descriptor, full_key_len,
		prefix, prefix_size) != EOK) {
		kzfree(full_key_descriptor);
		return (struct key *)ERR_PTR(-EINVAL);
	}

	sprintf_s(full_key_descriptor + prefix_size, FS_KEY_DESCRIPTOR_LEN,
		"%*phN", FS_KEY_DESCRIPTOR_SIZE, descriptor);

	full_key_descriptor[full_key_len - 1] = '\0';
	keyring_key = request_key(&key_type_logon, full_key_descriptor, NULL);
	kzfree(full_key_descriptor);
	return keyring_key;
}

static s32 hwdps_get_key(const u8 *descriptor,
	buffer_t *aes_key, uid_t uid)
{
	struct key *keyring_key = NULL;
	const struct user_key_payload *ukp = NULL;
	struct fscrypt_key *master_key = NULL;
	s32 res = 0;
	u8 tag[AES256_KEY_LEN] = {0};
	buffer_t tag_buffer = { tag, AES256_KEY_LEN };
	buffer_t msg_buffer = { (u8 *)&uid, sizeof(uid) };

	hwdps_pr_debug("%s enter!\n", __func__);
	keyring_key = fscrypt_request_key(descriptor, FS_KEY_DESC_PREFIX,
		FS_KEY_DESC_PREFIX_SIZE);
	if (IS_ERR(keyring_key)) {
		hwdps_pr_err("hwdps request_key failed!\n");
		return PTR_ERR(keyring_key);
	}

	down_read(&keyring_key->sem);
	if (keyring_key->type != &key_type_logon) {
		hwdps_pr_err("hwdps key type must be logon\n");
		res = -ENOKEY;
		goto out;
	}

	ukp = user_key_payload_locked(keyring_key);
	if (!ukp) {
		/* key was revoked before we acquired its semaphore */
		hwdps_pr_err("hwdps key was revoked\n");
		res = -EKEYREVOKED;
		goto out;
	}
	if (ukp->datalen != sizeof(struct fscrypt_key)) {
		hwdps_pr_err("hwdps fscrypt key size err %d\n", ukp->datalen);
		res = -EINVAL;
		goto out;
	}
	master_key = (struct fscrypt_key *)ukp->data;

	/* check master_key size failed on mtk size is 64 */
	if (memcpy_s(tag, AES256_KEY_LEN,
		master_key->raw, AES256_KEY_LEN) != EOK) {
		hwdps_pr_err("master key error size %d\n", master_key->size);
		res = -ENOKEY;
		goto out;
	}

	res = hash_generate_mac(&tag_buffer, &msg_buffer, aes_key);
	if (res != 0) {
		hwdps_pr_err("hash_generate_mac res %d\n", res);
		res = -ENOKEY;
		goto out;
	}

out:
	up_read(&keyring_key->sem);
	key_put(keyring_key);
	hwdps_pr_debug("%s end %d!\n", __func__, res);
	return res;
}

static void clear_new_key(buffer_t *fek,
	buffer_t *encoded_buffer, buffer_t *plaintext_fek_buffer)
{
	(void)memset_s(fek->data, fek->len, 0, fek->len);
	(void)memset_s(encoded_buffer->data, encoded_buffer->len,
		0, encoded_buffer->len);
	(void)memset_s(plaintext_fek_buffer->data, plaintext_fek_buffer->len,
		0, plaintext_fek_buffer->len);
}

s32 kernel_new_fek(u8 *desc, uid_t uid,
	secondary_buffer_t *enc_data_buf, buffer_t *fek)
{
	s32 err_code;
	u8 aes_key[AES256_KEY_LEN] = {0};
	u8 encoded_ciphertext[PHASE3_CIPHERTEXT_LENGTH] = {0};
	u8 plaintext_fek[FEK_LENGTH] = {0};
	buffer_t aes_key_buffer = { aes_key, AES256_KEY_LEN };
	buffer_t encoded_ciphertext_buffer = {
		encoded_ciphertext, PHASE3_CIPHERTEXT_LENGTH
	};
	buffer_t plaintext_fek_buffer = { plaintext_fek, FEK_LENGTH };

	if (!desc || !enc_data_buf ||
		!enc_data_buf->data || !fek || !fek->data) {
		hwdps_pr_err("invalid feks\n");
		err_code = ERR_MSG_NULL_PTR;
		goto cleanup;
	} else if (fek->len <= 0) {
		hwdps_pr_err("invalid length\n");
		err_code = ERR_MSG_LENGTH_ERROR;
		goto cleanup;
	}

	err_code = hwdps_get_key(desc, &aes_key_buffer, uid);
	if (err_code < 0) {
		hwdps_pr_err("hwdps_get_key failed\n");
		err_code = ERR_MSG_GENERATE_FAIL;
		goto cleanup;
	}

	err_code = hwdps_generate_enc(aes_key_buffer, encoded_ciphertext_buffer,
		plaintext_fek_buffer, false);
	if (err_code != 0) {
		hwdps_pr_err("hwdps_generate_enc failed\n");
		err_code = ERR_MSG_GENERATE_FAIL;
		goto cleanup;
	}

	if (memcpy_s(fek->data, fek->len, plaintext_fek,
		FEK_LENGTH) != EOK) {
		err_code = ERR_MSG_GENERATE_FAIL;
		goto cleanup;
	}
	if (memcpy_s(*enc_data_buf->data, *enc_data_buf->len,
		encoded_ciphertext_buffer.data,
		encoded_ciphertext_buffer.len) != EOK) {
		err_code = ERR_MSG_GENERATE_FAIL;
		goto cleanup;
	}
	hwdps_pr_info("%s success\n", __func__);
cleanup:
	clear_new_key(&aes_key_buffer, &encoded_ciphertext_buffer,
		&plaintext_fek_buffer);
	return err_code;
}

static void clear_exist_key(buffer_t *fek, buffer_t *plaintext_fek_buffer)
{
	(void)memset_s(fek->data, fek->len, 0, fek->len);
	(void)memset_s(plaintext_fek_buffer->data, plaintext_fek_buffer->len,
		0, plaintext_fek_buffer->len);
}

s32 kernel_get_fek(u8 *desc, uid_t uid,
	buffer_t *enc_buf, secondary_buffer_t *fek)
{
	s32 err_code;
	u8 plaintext_fek[FEK_LENGTH] = {0};
	u32 plaintext_fek_len = FEK_LENGTH;
	u8 aes_key[AES256_KEY_LEN] = {0};
	buffer_t aes_key_buffer = { aes_key, AES256_KEY_LEN };
	buffer_t plaintext_fek_buffer = { plaintext_fek, FEK_LENGTH };

	hwdps_pr_info("%s enter\n", __func__);
	if (!desc || !fek || !enc_buf || !enc_buf->data ||
		!fek->len || !fek->data) {
		hwdps_pr_err("invalid fek params\n");
		err_code = ERR_MSG_NULL_PTR;
		return err_code;
	} else if (enc_buf->len != PHASE3_CIPHERTEXT_LENGTH) {
		hwdps_pr_err("invalid material len\n");
		err_code = ERR_MSG_LENGTH_ERROR;
		return err_code;
	}

	err_code = hwdps_get_key(desc, &aes_key_buffer, uid);
	if (err_code < 0) {
		hwdps_pr_err("hwdps_get_key in get key failed\n");
		err_code = ERR_MSG_GENERATE_FAIL;
		goto cleanup;
	}

	err_code = hwdps_generate_dec(aes_key_buffer,
		*enc_buf, plaintext_fek_buffer);
	if (err_code != 0) {
		hwdps_pr_err("hwdps_generate_dec failed\n");
		err_code = ERR_MSG_GENERATE_FAIL;
		goto cleanup;
	}

	*fek->data = kzalloc(plaintext_fek_len, GFP_KERNEL);
	if (*fek->data == NULL) {
		hwdps_pr_err("bp_file_key malloc\n");
		err_code = ERR_MSG_OUT_OF_MEMORY;
		goto cleanup;
	}
	if (memcpy_s(*fek->data, plaintext_fek_len,
		plaintext_fek, FEK_LENGTH) != EOK) {
		err_code = ERR_MSG_GENERATE_FAIL;
		goto cleanup;
	}
	*fek->len = plaintext_fek_len;
	hwdps_pr_info("%s success\n", __func__);
cleanup:
	clear_exist_key(&aes_key_buffer, &plaintext_fek_buffer);
	return err_code;
}

static bool check_update_params(u8 *desc, buffer_t *enc_buf,
	secondary_buffer_t *fek)
{
	return (!desc || !fek || !enc_buf || !fek->len ||
		!fek->data || !enc_buf->data ||
		enc_buf->len != PHASE3_CIPHERTEXT_LENGTH);
}

s32 kernel_update_fek(u8 *desc, buffer_t *enc_buf,
	secondary_buffer_t *fek, uid_t new_uid, uid_t old_uid)
{
	s32 err_code;
	u8 plaintext_fek[FEK_LENGTH] = {0};
	u8 aes_key[AES256_KEY_LEN] = {0};
	buffer_t aes_key_buffer = { aes_key, AES256_KEY_LEN };
	buffer_t plaintext_fek_buffer = { plaintext_fek, FEK_LENGTH };

	if (check_update_params(desc, enc_buf, fek)) {
		hwdps_pr_err("invalid fek or enc_buf\n");
		err_code = ERR_MSG_NULL_PTR;
		return err_code;
	}

	err_code = hwdps_get_key(desc, &aes_key_buffer, old_uid);
	if (err_code < 0) {
		hwdps_pr_err("hwdps_get_key in get key failed\n");
		err_code = ERR_MSG_GENERATE_FAIL;
		goto cleanup;
	}

	err_code = hwdps_generate_dec(aes_key_buffer,
		*enc_buf, plaintext_fek_buffer);
	if (err_code != 0) {
		hwdps_pr_err("hwdps_generate_dec failed\n");
		err_code = ERR_MSG_GENERATE_FAIL;
		goto cleanup;
	}

	err_code = hwdps_get_key(desc, &aes_key_buffer, new_uid);
	if (err_code < 0) {
		hwdps_pr_err("hwdps_get_key in get key failed\n");
		err_code = ERR_MSG_GENERATE_FAIL;
		goto cleanup;
	}

	err_code = hwdps_generate_enc(aes_key_buffer,
		*enc_buf, plaintext_fek_buffer, true);
	if (err_code != 0) {
		hwdps_pr_err("hwdps_generate_enc failed\n");
		err_code = ERR_MSG_GENERATE_FAIL;
		goto cleanup;
	}

	*fek->data = kzalloc(FEK_LENGTH, GFP_KERNEL);
	if (*fek->data == NULL) {
		err_code = ERR_MSG_OUT_OF_MEMORY;
		goto cleanup;
	}
	if (memcpy_s(*fek->data, FEK_LENGTH,
		plaintext_fek, FEK_LENGTH) != EOK) {
		err_code = ERR_MSG_GENERATE_FAIL;
		goto cleanup;
	}
	*fek->len = FEK_LENGTH;
	hwdps_pr_info("%s success\n", __func__);
cleanup:
	clear_exist_key(&aes_key_buffer, &plaintext_fek_buffer);
	return err_code;
}

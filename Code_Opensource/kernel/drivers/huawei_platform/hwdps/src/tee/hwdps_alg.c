/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2020-2020. All rights reserved.
 * Description: This file contains the function required for operations about
 *              hwdps algorithm.
 * Create: 2020-06-16
 */

#include "inc/tee/hwdps_alg.h"
#include <linux/key.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/string.h>
#include <securec.h>
#include "inc/base/hwdps_utils.h"
#include "inc/tee/base_alg.h"

s32 hwdps_generate_dec(buffer_t aes_key,
	buffer_t ciphertext, buffer_t plaintext_fek)
{
	s32 ret;
	struct xattribs_t *xattr = NULL;
	u8 *de_ret = NULL;
	secondary_buffer_t ret_out = { &de_ret, &plaintext_fek.len };
	buffer_t iv = { NULL, 0 };
	buffer_t enc_fek = { NULL, 0 };

	xattr = (struct xattribs_t *)ciphertext.data;
	if (!xattr)
		return -EINVAL;
	iv.data = xattr->iv;
	iv.len = sizeof(xattr->iv);
	enc_fek.data = xattr->enc_fek;
	enc_fek.len = sizeof(xattr->enc_fek);

	if (!plaintext_fek.data || (plaintext_fek.len != FEK_LENGTH))
		return -EINVAL;

	ret = aes_cbc(aes_key, iv, enc_fek, ret_out, false);
	if ((ret != 0) || (plaintext_fek.len != FEK_LENGTH)) {
		kzfree(de_ret);
		return (ret == 0) ? -EINVAL : ret;
	}

	if (memcpy_s(plaintext_fek.data, plaintext_fek.len,
		de_ret, FEK_LENGTH) != EOK) {
		kzfree(de_ret);
		de_ret = NULL;
		return -EINVAL;
	}
	kzfree(de_ret);
	de_ret = NULL;
	return ret;
}

s32 hwdps_generate_enc(buffer_t aes_key, buffer_t ciphertext,
	buffer_t plaintext_fek, bool is_update)
{
	s32 ret;
	struct xattribs_t *xattr = NULL;
	u32 cipher_fek_len = FEK_LENGTH; /* AES-256-XTS */
	u8 *en_ret = NULL;
	secondary_buffer_t ret_out = { &en_ret, &cipher_fek_len };
	buffer_t iv = {0};

	hwdps_pr_debug("%s enter\n", __func__);
	if (!ciphertext.data || !plaintext_fek.data)
		return -EINVAL;
	xattr = (struct xattribs_t *)ciphertext.data;
	xattr->version[0] = VERSION_2; /* VERSION */

	get_random_bytes(xattr->iv, sizeof(xattr->iv));
	if (!is_update)
		get_random_bytes(plaintext_fek.data, plaintext_fek.len);
	iv.data = xattr->iv;
	iv.len = sizeof(xattr->iv);

	ret = aes_cbc(aes_key, iv, plaintext_fek, ret_out, true);
	if ((ret != 0) || (cipher_fek_len != FEK_LENGTH)) {
		kzfree(en_ret);
		en_ret = NULL;
		hwdps_pr_err("aes_cbc failed %u %d\n", cipher_fek_len, ret);
		return (ret == 0) ? -EINVAL : ret;
	}
	if (memcpy_s(xattr->enc_fek, sizeof(xattr->enc_fek), en_ret,
		cipher_fek_len) != EOK) {
		kzfree(en_ret);
		en_ret = NULL;
		hwdps_pr_err("memcpy_s failed\n");
		return -EINVAL;
	}
	kzfree(en_ret);
	en_ret = NULL;
	hwdps_pr_debug("%s end\n", __func__);

	return 0;
}

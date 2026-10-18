/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2020-2020. All rights reserved.
 * Description: This file contains the function required for operations about
 *              hwdps algorithm.
 * Create: 2020-06-16
 */

#ifndef _HWDPS_ALG_H
#define _HWDPS_ALG_H

#include <linux/types.h>
#include "inc/base/hwdps_defines.h"

#define VERSION_1 0x01
#define VERSION_2 0x02
#define AES_IV_LENGTH 16
#define VERSION_LENGTH 1
#define FEK_LENGTH 64
#define AES256_KEY_LEN 32
#define PHASE3_CIPHERTEXT_LENGTH (VERSION_LENGTH + \
	AES_IV_LENGTH + FEK_LENGTH)

#pragma pack(1)
struct xattribs_t {
	u8 version[VERSION_LENGTH];
	u8 iv[AES_IV_LENGTH];
	u8 enc_fek[FEK_LENGTH];
};
#pragma pack()

s32 hwdps_generate_enc(buffer_t aes_key, buffer_t ciphertext,
	buffer_t plaintext_fek, bool is_update);

s32 hwdps_generate_dec(buffer_t aes_key,
	buffer_t ciphertext, buffer_t plaintext_fek);

#endif

/*
 * kd_camkit_define_xa.h
 *
 * Copyright (c) 2020-2020 Huawei Technologies Co., Ltd.
 *
 * define image sensor parameters
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

#ifndef KD_CAMKIT_DEFINE_XA_H
#define KD_CAMKIT_DEFINE_XA_H

/*
 * define sensor normalized parameters as follow:
 */
// sensor id wn Main:577 Sub:441 Wide:646 Macro:562
// IMX
#define C645WMR_M010_WN_SENSOR_ID            0x0258011
#define C645WMR_M0A0_WN_SENSOR_ID            0x02580A1

#define C633KES_M020_CP_SENSOR_ID            0xC0682021
#define C633KES_M020_NTX_SENSOR_ID           0xC0682022
#define C633KES_M060_CP_SENSOR_ID            0xC0682061
#define C633KES_M060_NTX_SENSOR_ID           0xC0682062

#define C658JDI_M020_CP_SENSOR_ID            0xC0471021
#define C658JDI_M020_NTX_SENSOR_ID           0xC0471022

#define C606DBC_M010_CP_SENSOR_ID            0xC0582010

#define C606DBC_M020_CP_SENSOR_ID            0xC0582020

#define C628BDH_M090_CP_SENSOR_ID            0xC0355090
// OV
#define C585WGU_WN_SENSOR_ID                 0x002B001
#define C585WGU_CP_SENSOR_ID                 0xC002B002
#define C830WGU_NTX_SENSOR_ID                0xC002B003

#define C658FUV_M060_CP_SENSOR_ID            0xC1641061
#define C658FUV_M060_NTX_SENSOR_ID           0xC1641062

#define C562YGA_M010_WN_SENSOR_ID            0x2509011
#define C637YGA_M010_CP_SENSOR_ID            0xC2509012
#define C698YGA_M010_NTX_SENSOR_ID           0xC2509013

#define C633GHA_M020_CP_SENSOR_ID            0xC6443020

#define C606DVY_M060_CP_SENSOR_ID            0xC4842060

#define C606DVY_M030_CP_SENSOR_ID            0xC4842030

#define C628JWG_M020_CP_SENSOR_ID            0xC885A020

#define C628JWG_M060_CP_SENSOR_ID            0xC885A060

#define C9909QUV_M180_BRT_SENSOR_ID          0xC002B180
#define C830QUV_M100_CAR_SENSOR_ID           0xC002B100

#define C9922QCP_M100_CAR_SENSOR_ID          0xC0D42100

// Hynix
#define C645UAI_M090_WN_SENSOR_ID            0x1336091
#define C9908UAI_M190_BRT_SENSOR_ID          0xC1336190
#define C9922UAI_M330_CAR_SENSOR_ID          0xC1336330

#define C441UVO_M060_WN_SENSOR_ID            0x0846061
#define C441UVO_M030_WN_SENSOR_ID            0x0846031

#define C646HQF_M0B0_WN_SENSOR_ID            0x05560B1
#define C9910HQF_M180_BRT_SENSOR_ID          0xC0556180
#define C9920HQF_M330_CAR_SENSOR_ID          0xC0556330

#define C637QVV_M0B0_CP_SENSOR_ID            0xC00E10B1

#define C637QVV_M0C0_WN_SENSOR_ID            0x00E10C1
#define C637QVV_M0C0_CP_SENSOR_ID            0xC00E10C2

#define C628UVO_M010_CP_SENSOR_ID            0xC8846010

// GC
#define C646KEH_M030_WN_SENSOR_ID            0x5035031
#define C9910KEH_M100_BRT_SENSOR_ID          0xC5035100
#define C9920KEH_M190_CAR_SENSOR_ID          0xC5035190
#define C9920KEH_M180_CAR_SENSOR_ID          0xC5035180

#define C441FZB_M050_WN_SENSOR_ID            0x8054051

#define C585GFI_WN_SENSOR_ID                 0x2375001
#define C585GFI_CP_SENSOR_ID                 0xC2375002
#define C830GFI_NTX_SENSOR_ID                0xC2375003

#define C562EOY_M020_WN_SENSOR_ID            0x2375021
#define C637EOY_M020_CP_SENSOR_ID            0xC2375022
#define C698EOY_M020_NTX_SENSOR_ID           0xC2375023

#define C562EOY_M0B0_WN_SENSOR_ID            0x23750b1
#define C637EOY_M0B0_CP_SENSOR_ID            0xC23750B2
#define C698EOY_M0B0_NTX_SENSOR_ID           0xC23750B3

#define C585RWP_CP_SENSOR_ID                 0xC002E001
#define C830RWP_NTX_SENSOR_ID                0xC002E002
#define C9909RWP_M320_BRT_SENSOR_ID          0xC02E0320
#define C830RWP_CAR_SENSOR_ID                0xC02E0002

// Sumsung
#define C645XBA_M0C0_WN_SENSOR_ID            0x30C60c1
#define C9908XBA_M180_BRT_SENSOR_ID          0xC30C6180
#define C9922XBA_M340_CAR_SENSOR_ID          0xC30C6340

#define C441FAH_M0C0_WN_SENSOR_ID            0x487B0c1

#define C646TBQ_M050_WN_SENSOR_ID            0x059b051

#define C658OGU_M010_CP_SENSOR_ID            0xC3109011
#define C658OGU_M010_NTX_SENSOR_ID           0xC3109012

// SENSOR_DRVNAME
// IMX
#define SENSOR_DRVNAME_C645WMR_M010_WN       "c645wmr_m010_wn"
#define SENSOR_DRVNAME_C645WMR_M0A0_WN       "c645wmr_m0a0_wn"

#define SENSOR_DRVNAME_C633KES_M020_CP       "c633kes_m020_cp"
#define SENSOR_DRVNAME_C633KES_M060_CP       "c633kes_m060_cp"
#define SENSOR_DRVNAME_C633KES_M020_NTX      "c633kes_m020_ntx"
#define SENSOR_DRVNAME_C633KES_M060_NTX      "c633kes_m060_ntx"

#define SENSOR_DRVNAME_C658JDI_M020_CP       "c658jdi_m020_cp"
#define SENSOR_DRVNAME_C658JDI_M020_NTX      "c658jdi_m020_ntx"
#define SENSOR_DRVNAME_C606DBC_M010_CP       "c606dbc_m010_cp"
#define SENSOR_DRVNAME_C606DBC_M020_CP       "c606dbc_m020_cp"
#define SENSOR_DRVNAME_C628BDH_M090_CP       "c628bdh_m090_cp"
// OV
#define SENSOR_DRVNAME_C585WGU_WN            "c585wgu_wn"
#define SENSOR_DRVNAME_C585WGU_CP            "c585wgu_cp"
#define SENSOR_DRVNAME_C830WGU_NTX           "c830wgu_ntx"

#define SENSOR_DRVNAME_C658FUV_M060_CP       "c658fuv_m060_cp"
#define SENSOR_DRVNAME_C658FUV_M060_NTX      "c658fuv_m060_ntx"

#define SENSOR_DRVNAME_C562YGA_M010_WN       "c562yga_m010_wn"
#define SENSOR_DRVNAME_C633GHA_M020_CP       "c633gha_m020_chl"
#define SENSOR_DRVNAME_C606DVY_M060_CP       "c606dvy_m060_cp"
#define SENSOR_DRVNAME_C606DVY_M030_CP       "c606dvy_m030_cp"
#define SENSOR_DRVNAME_C628JWG_M020_CP       "c628jwg_m020_cp"
#define SENSOR_DRVNAME_C628JWG_M060_CP       "c628jwg_m060_cp"

#define SENSOR_DRVNAME_C637YGA_M010_CP       "c637yga_m010_cp"
#define SENSOR_DRVNAME_C698YGA_M010_NTX      "c698yga_m010_ntx"
#define SENSOR_DRVNAME_C9909QUV_M180_BRT     "c9909quv_m180_brt"
#define SENSOR_DRVNAME_C830QUV_M100_CAR      "c830quv_m100_car"

#define SENSOR_DRVNAME_C9922QCP_M100_CAR     "c9922qcp_m100_car"

// Hynix
#define SENSOR_DRVNAME_C645UAI_M090_WN       "c645uai_m090_wn"
#define SENSOR_DRVNAME_C9908UAI_M190_BRT     "c9908uai_m190_brt"
#define SENSOR_DRVNAME_C9922UAI_M330_CAR     "c9922uai_m330_car"

#define SENSOR_DRVNAME_C441UVO_M060_WN       "c441uvo_m060_wn"
#define SENSOR_DRVNAME_C441UVO_M030_WN       "c441uvo_m030_wn"

#define SENSOR_DRVNAME_C646HQF_M0B0_WN       "c646hqf_m0b0_wn"
#define SENSOR_DRVNAME_C9910HQF_M180_BRT     "c9910hqf_m180_brt"
#define SENSOR_DRVNAME_C9920HQF_M330_CAR     "c9920hqf_m330_car"

#define SENSOR_DRVNAME_C637QVV_M0B0_CP       "c637qvv_m0b0_cp"

#define SENSOR_DRVNAME_C637QVV_M0C0_WN       "c637qvv_m0c0_wn"
#define SENSOR_DRVNAME_C637QVV_M0C0_CP       "c637qvv_m0c0_cp"
#define SENSOR_DRVNAME_C628UVO_M010_CP       "c628uvo_m010_cp"

// GC
#define SENSOR_DRVNAME_C646KEH_M030_WN       "c646keh_m030_wn"
#define SENSOR_DRVNAME_C9910KEH_M100_BRT     "c9910keh_m100_brt"
#define SENSOR_DRVNAME_C9920KEH_M190_CAR     "c9920keh_m190_car"
#define SENSOR_DRVNAME_C9920KEH_M180_CAR     "c9920keh_m180_car"

#define SENSOR_DRVNAME_C441FZB_M050_WN       "c441fzb_m050_wn"

#define SENSOR_DRVNAME_C637EOY_M020_CP       "c637eoy_m020_cp"
#define SENSOR_DRVNAME_C637EOY_M0B0_CP       "c637eoy_m0b0_cp"
#define SENSOR_DRVNAME_C698EOY_M020_NTX      "c698eoy_m020_ntx"
#define SENSOR_DRVNAME_C698EOY_M0B0_NTX      "c698eoy_m0b0_ntx"

#define SENSOR_DRVNAME_C585GFI_WN            "c585gfi_wn"
#define SENSOR_DRVNAME_C585GFI_CP            "c585gfi_cp"
#define SENSOR_DRVNAME_C830GFI_NTX           "c830gfi_ntx"

#define SENSOR_DRVNAME_C562EOY_M020_WN       "c562eoy_m020_wn"
#define SENSOR_DRVNAME_C562EOY_M0B0_WN       "c562eoy_m0b0_wn"

#define SENSOR_DRVNAME_C585RWP_CP            "c585rwp_cp"
#define SENSOR_DRVNAME_C830RWP_NTX           "c830rwp_ntx"
#define SENSOR_DRVNAME_C9909RWP_M320_BRT     "c9909rwp_m320_brt"
#define SENSOR_DRVNAME_C830RWP_CAR           "c830rwp_car"

// Sumsung
#define SENSOR_DRVNAME_C645XBA_M0C0_WN       "c645xba_m0c0_wn"
#define SENSOR_DRVNAME_C9908XBA_M180_BRT     "c9908xba_m180_brt"
#define SENSOR_DRVNAME_C9922XBA_M340_CAR     "c9922xba_m340_car"

#define SENSOR_DRVNAME_C441FAH_M0C0_WN       "c441fah_m0c0_wn"

#define SENSOR_DRVNAME_C646TBQ_M050_WN       "c646tbq_m050_wn"

#define SENSOR_DRVNAME_C658OGU_M010_CP       "c658ogu_m010_cp"
#define SENSOR_DRVNAME_C658OGU_M010_NTX      "c658ogu_m010_ntx"

#endif // KD_CAMKIT_MERIDA2CAM_H

#
# Copyright (c) 2020-2020 Huawei Technologies Co., Ltd.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# Makefile for misc devices that really don't fit anywhere else.
#
MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))

ifeq ($(CONFIG_MACH_MT6833),y)
MTK_DERIVED_PLATFORM := mt6833
endif
ifeq ($(CONFIG_MACH_MT6877),y)
MTK_DERIVED_PLATFORM := mt6877
endif

ifeq ($(CONFIG_MACH_MT6885),y)
MTK_DERIVED_PLATFORM := mt6885
endif
ARCH_MTK_PROJECT := $(subst ",,$(CONFIG_ARCH_MTK_PROJECT))
IMGSENSOR_DRIVER_PATH  := $(srctree)/drivers/misc/mediatek/imgsensor/src
MTK_DRIVER_PATH  := $(srctree)/drivers/misc/mediatek

ifneq (,$(filter $(CONFIG_MTK_PLATFORM), "mt6765" "mt6768"))
$(warning "platform" $(CONFIG_MTK_PLATFORM))
COMMON_VERSION := v1
ccflags-y := -DDRIVER_VERSION=1
endif

ifneq (,$(filter $(CONFIG_MTK_PLATFORM), "mt6833" "mt6853" "mt6873" "mt6893" "mt6885" "mt6877"))
COMMON_VERSION := v1_1
ccflags-y := -DDRIVER_VERSION=2
endif

LOCAL_C_INCLUDES += $(TOP)/vendor/huawei/chipset_common/modules/libc_sec/include
LOCAL_SHARED_LIBRARIES += libc_secshared
MEDIATEK_DRIVER_PATH  := $(srctree)/drivers/misc/mediatek/
subdir-ccflags-y += -I$(MEDIATEK_DRIVER_PATH)/camkit/ \
	-I$(MEDIATEK_DRIVER_PATH)/camkit/customized/ \
	-I$(MEDIATEK_DRIVER_PATH)/camkit/adapter/ \
	-I$(MEDIATEK_DRIVER_PATH)/camkit/core/inc \
	-I$(srctree)/drivers/misc/mediatek/cam_hiview \
	-I$(MEDIATEK_DRIVER_PATH)/cam_btb_check

obj-y += core/src/camkit_sensor_i2c.o
obj-y += core/src/camkit_driver_impl.o
obj-y += core/src/camkit_driver_3a.o
obj-y += core/src/camkit_driver_interface.o
obj-y += core/src/camkit_probe_sensor.o

obj-y += adapter/
ifneq (,$(filter $(CONFIG_MTK_PLATFORM), "mt6853" "mt6885"))
$(warning "platform" $(CONFIG_MTK_PLATFORM))
obj-y += customized/
endif

ifeq (,$(wildcard $(IMGSENSOR_DRIVER_PATH)/$(MTK_PLATFORM)/camera_project/$(ARCH_MTK_PROJECT)/camera_hw))
	ifeq (,$(wildcard $(IMGSENSOR_DRIVER_PATH)/$(MTK_PLATFORM)/camera_hw_$(MTK_DERIVED_PLATFORM)))
		subdir-ccflags-y += -I$(IMGSENSOR_DRIVER_PATH)/$(MTK_PLATFORM)/camera_hw
	else
		subdir-ccflags-y += -I$(IMGSENSOR_DRIVER_PATH)/$(MTK_PLATFORM)/camera_hw_$(MTK_DERIVED_PLATFORM)
		subdir-ccflags-y += -I$(IMGSENSOR_DRIVER_PATH)/$(MTK_PLATFORM)/camera_hw
	endif
else
	subdir-ccflags-y += -I$(IMGSENSOR_DRIVER_PATH)/$(MTK_PLATFORM)/camera_project/$(ARCH_MTK_PROJECT)/camera_hw
	subdir-ccflags-y += -I$(IMGSENSOR_DRIVER_PATH)/$(MTK_PLATFORM)/camera_hw_$(MTK_DERIVED_PLATFORM)
	subdir-ccflags-y += -I$(IMGSENSOR_DRIVER_PATH)/$(MTK_PLATFORM)/camera_hw
endif

subdir-ccflags-y += -I$(IMGSENSOR_DRIVER_PATH)/common/$(COMMON_VERSION)/camera_hw
subdir-ccflags-y += -I$(IMGSENSOR_DRIVER_PATH)/common/$(COMMON_VERSION)

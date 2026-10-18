#
#easy-shell Makefile
#
obj-$(CONFIG_HUAWEI_BB_ECALL) += easy_shell.o
ccflags-y += -Wextra \
             -Wdate-time \
             -Wfloat-equal \
             -Wshadow \
             -Wformat=2
CFLAGS_REMOVE_xxx.o += -Wno-format \
                       -Wno-trigraphs \
					   -Wno-pointer-sign \
					   -Wno-unused-value \
					   -Wno-sign-compare \
					   -Wno-uninitialized \
					   -Wno-tautological-compare \
					   -Wno-duplicate-decl-specifier \
					   -I$(KBUILD_SRC)/arch/$(SRCARCH)/include \
                       -I./arch/$(SRCARCH)/include/generated \
                       -I$(KBUILD_SRC)/drivers/misc/mediatek/include \
                       -I./include \
                       -I$(KBUILD_SRC)/arch/$(SRCARCH)/include/uapi \
                       -I./arch/$(SRCARCH)/include/generated/uapi \
                       -I$(KBUILD_SRC)/include/uapi \
                       -I./include/generated/uapi \
                       -I./drivers/devkit/lcdkit/lcdkit3.0 \
                       -I$(KBUILD_SRC)/lib/libc_sec/include \
                       -I$(KBUILD_SRC)/drivers/easy_shell \
                       -Idrivers/easy_shell \
					   -D__KERNEL__ \
					   -fshort-wchar
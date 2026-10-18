ifeq ($(HLTHERM_RUNTEST), true)
    CFLAGS_KERNEL += -DCONFIG_HLTHERM_RUNTEST
endif

obj-y += huawei_charger_null.o

# power drivers
obj-y += common_module/
obj-y += charger/
obj-y += wireless/
obj-y += hardware_ic/
obj-y += battery/
obj-y += proxy/
obj-$(CONFIG_HUAWEI_FEATURE_PRINT_PID_NAME) += pid_socket/
obj-$(CONFIG_HUAWEI_USB_SHORT_CIRCUIT_PROTECT) += uscp/
obj-$(CONFIG_HUAWEI_DUBAI) += dubai/

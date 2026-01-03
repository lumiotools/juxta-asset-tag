COINES_INSTALL_PATH ?= ../../../..

EXAMPLE_FILE ?= mcu_alternate_config_step_counter_tap.c

API_LOCATION ?= ../..

COMMON_LOCATION ?= ..

C_SRCS += \
$(API_LOCATION)/bmi3.c \
$(API_LOCATION)/bmi323.c \
$(COMMON_LOCATION)/common/common.c

INCLUDEPATHS += \
$(API_LOCATION) \
$(COMMON_LOCATION)/common

ifndef TARGET
$(error TARGET is not defined; please specify a target)
endif
INVALID_TARGET = PC
$(if $(filter $(TARGET),PC), $(error TARGET has an invalid value '$(TARGET)'; 'PC' is not a valid target. Please use a MCU target (eg. MCU_APP30).), )

include $(COINES_INSTALL_PATH)/coines.mk
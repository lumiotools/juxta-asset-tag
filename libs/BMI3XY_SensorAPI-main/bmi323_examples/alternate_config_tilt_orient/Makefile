COINES_INSTALL_PATH ?= ../../../..

EXAMPLE_FILE ?= alternate_config_tilt_orient.c

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

INVALID_TARGET = MCU
INVALID_STR := $(findstring $(INVALID_TARGET), $(TARGET))
ifeq ($(INVALID_STR),$(INVALID_TARGET))
    $(error TARGET has an invalid value '$(TARGET)'; 'MCU' targets are not a valid. Please use PC as target.)
endif

include $(COINES_INSTALL_PATH)/coines.mk
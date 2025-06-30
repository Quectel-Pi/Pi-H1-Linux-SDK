#Btdiag Makefile for Android project

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_CFLAGS := -Wno-unused-parameter
LOCAL_CFLAGS += -DBLUEDROID_STACK
LOCAL_CFLAGS += -DPLATFORM_ANDROID
LOCAL_CPPFLAGS += -std=c++14

include build/core/version_defaults.mk
ifeq ($(PLATFORM_VERSION),6.0.1)
LOCAL_CFLAGS += -DANDROID_M
endif
ifeq ($(PLATFORM_VERSION),5.1.1)
LOCAL_CFLAGS += -DANDROID_L
endif

LOCAL_SRC_FILES := Btdiag.c connection.cpp hciattach_rome.c privateMembers.c uart.c
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE := Btdiag
#LOCAL_C_INCLUDES += 

LOCAL_SHARED_LIBRARIES += libcutils   \
                          libutils    \
                          libhardware \
                          libhardware_legacy
LOCAL_MULTILIB := 32
include $(BUILD_EXECUTABLE)


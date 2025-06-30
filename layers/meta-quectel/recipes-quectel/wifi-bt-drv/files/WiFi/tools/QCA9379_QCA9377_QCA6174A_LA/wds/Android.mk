#ifeq ($(call is-vendor-board-platform,QCOM),true)
#ifeq ($(findstring true, $(BOARD_HAVE_QCOM_FM) $(BOARD_HAVE_BLUETOOTH)),true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

BDROID_DIR:= system/bt
QCOM_DIR := hardware/qcom/bt/libbt-vendor

LOCAL_C_INCLUDES := $(TARGET_OUT_HEADERS)/common/inc
LOCAL_C_INCLUDES += $(BDROID_DIR)/hci/include
LOCAL_C_INCLUDES += $(QCOM_DIR)/include \

LOCAL_CFLAGS := -DANDROID

ifneq ($(BOARD_ANT_WIRELESS_DEVICE), )
LOCAL_CFLAGS += -DCONFIG_ANT
endif
LOCAL_SRC_FILES := wds_main.c
LOCAL_SRC_FILES += wds_hci_pfal_linux.c

LOCAL_MODULE := wdsdaemon
LOCAL_MODULE_TAGS := optional
#LOCAL_SHARED_LIBRARIES := libdiag
LOCAL_SHARED_LIBRARIES += libcutils \
                          libdl

include $(BUILD_EXECUTABLE)
#endif  # filter
#ndif  # is-vendor-board-platform

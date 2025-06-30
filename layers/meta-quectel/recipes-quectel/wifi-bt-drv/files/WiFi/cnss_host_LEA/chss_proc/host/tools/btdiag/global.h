/* 
Copyright (c) 2013-2020 Qualcomm Technologies, Inc.
All Rights Reserved. 
Confidential and Proprietary - Qualcomm Technologies, Inc. 
*/
#include "uart.h"
#define TCP_BUF_LENGTH         256

#define HCI_MAX_EVENT_SIZE     260
#define HCI_CMD_IND            (1)
#define HCI_COMMAND_HDR_SIZE   3

typedef unsigned char BYTE;

typedef enum 
{
    SERIAL,
    USB,
	ETHERNET,
    INVALID
} ConnectionType;
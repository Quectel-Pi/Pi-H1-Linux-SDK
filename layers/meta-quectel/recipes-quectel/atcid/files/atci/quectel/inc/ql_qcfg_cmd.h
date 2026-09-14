/*

    *Copyright :

    *Copyright (c) 2021, Quectel Wireless Solutions Co., Ltd. All rights reserved.

    *Quectel Wireless Solutions Proprietary and Confidential.
*/
/*=========================================================================



 ==========================================================================*/

/*=======================================================================

                          EDIT HISTORY FOR MODULE

  This section contains comments describing changes made to the module.

  Notice that changes are listed in reverse chronological order.



  WHEN		      WHO			 WHAT,WHERE,WHY

  ----------     ------         --------------------------------------------

  06/21/2023     steven.liu        init

  ========================================================================*/
#ifndef _QUECTEL_QCFG_CMD_H_
#define _QUECTEL_QCFG_CMD_H_


#define QL_USB_VID 0x2c7c
#define QL_USB_PID 0x7003
#define QL_USB_PORT_COUNT 6U

typedef struct {
    const char *cmd;
    int (*handler)(char *, char *);
} qcfg_hander_t;

typedef struct usb_cfg
{
  int pid;
  int vid;
  int usb_enum_arr[6];
} usb_cfg_t;

ATRESPONSE_t QL_AT_QCFG_Handle(char *cmdline, ATOP_t at_op, char *response);


int QL_AT_QCFG_VONR_HANDLE(char *cmdline, char *response);


#endif //_QUECTEL_QCFG_CMD__H_

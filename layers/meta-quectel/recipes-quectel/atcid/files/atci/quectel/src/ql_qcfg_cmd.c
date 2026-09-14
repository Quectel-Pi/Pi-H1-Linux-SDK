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

  06/20/2023     steven.liu      init

  ========================================================================*/

#include "atcid.h"
#include "at_tok.h"
#include "atcid_util.h"
#include "ql_qcfg_cmd.h"
#include "ql_atcmd_info.h"

qcfg_hander_t qcfg_hander_table[] = {
    {"vonr", QL_AT_QCFG_VONR_HANDLE},
    //add new cmd in here
};

bool is_get_operation(char *cmdline, char *cfg_str)
{
   void *ptr = strstr(cmdline, cfg_str);
    if (ptr)
    {
        if (!strcmp(ptr, cfg_str))
            return true;
    }
    return false;
}

int init_usb_params(char *cmdline, usb_cfg_t *p_usb_config)
{
    int ret = -1;
    int i = 0;
    char *command;

    ret = at_tok_nextstr(&cmdline, &command);
    if (ret < 0)
    {
        goto out;
    }

    ret = at_tok_nexthexint(&cmdline, &p_usb_config->vid);
    if (ret < 0)
    {
        LOGATCI(LOG_DEBUG, "usb params get vid failed\n");
        goto out;
    }

    ret = at_tok_nexthexint(&cmdline, &p_usb_config->pid);
    if (ret < 0)
    {
        LOGATCI(LOG_DEBUG, "usb params get pid failed\n");
        goto out;
    }

#if 0
    if (p_usb_config->vid > 0xffff || p_usb_config->vid < 0x1)
    {
        LOGATCI(LOG_DEBUG, "usb params vid range error\n");
        ret = -1;
        goto out;
    }
    if (p_usb_config->pid > 0xffff || p_usb_config->pid < 0x1)
    {
        LOGATCI(LOG_DEBUG, "usb params pid range error\n");
        ret = -1;
        goto out;
    }
#else
    if (p_usb_config->vid != QL_USB_VID || p_usb_config->pid != QL_USB_PID)
    {
        LOGATCI(LOG_DEBUG, "usb params:vid:%x, pid:%x\n", p_usb_config->vid, p_usb_config->pid);
        LOGATCI(LOG_DEBUG, "usb params pid vid check error\n");
        ret = -1;
        goto out;
    }
#endif

    for (i = 0; i < QL_USB_PORT_COUNT; i++)
    {
        ret = at_tok_nextint(&cmdline, &p_usb_config->usb_enum_arr[i]);
        if (ret < 0 || (p_usb_config->usb_enum_arr[i] != 0 && p_usb_config->usb_enum_arr[i] != 1))
        {
            LOGATCI(LOG_DEBUG, "usb params fail,usb port num:%d,value:%d\n", i, p_usb_config->usb_enum_arr[i]);
            ret = -1;
            goto out;
        }
    }
    ret = 0;
out:
    return ret;
}


int QL_AT_QCFG_VONR_HANDLE(char *cmdline, char *response)
{
    int is_get_op = 0;
    int i = 0;
    int ret = 0;
    int value;
    char *command;
    char vonr_string[] = "\"vonr\"";
    char vonr_ret_str[2] = {0};
    const char * sim1_vonr_option = "radio_property.property.persist_mtk_volte_enable1";
    const char * sim2_vonr_option = "radio_property.property.persist_mtk_volte_enable2";

    // match "vonr" end of cmdline
    is_get_op = is_get_operation(cmdline, vonr_string);

    if (is_get_op)
    {

    }
    else
    {

    }

    return ret;
}


int cmd_handle_dispatch(char *cmdline, char *response)
{
    int i = 0;
    int ret = 0;
    for (i = 0; i < sizeof(qcfg_hander_table) / sizeof(qcfg_hander_table[0]); i++) {
        if (strstr(cmdline, qcfg_hander_table[i].cmd)) {
            ret = qcfg_hander_table[i].handler(cmdline, response);
            return ret;
        }
    }
    LOGATCI(LOG_DEBUG, "unknow cmd：%s\n", cmdline);
    return -1;
}

//ATRESPONSE_t QL_AT_QCFG_Handle(char *cmdline, ATOP_t opType, char *response)
//{
//    LOGATCI(LOG_DEBUG, "cmdline %s", cmdline);
//    char *command;
//    int err, ret, i;
//    int is_get_op = 0;
//    usb_cfg_t *p_usb_config = NULL;
//
//    switch (opType)
//    {
//    case AT_TEST_OP:
//        sprintf(response, "+QCFG: \"usbcfg\",0x2c7c,0x7003,(0,1),(0,1),(0,1),(0,1),(0,1),(0,1)\n");
//        strcat(response, "+QCFG: \"ims\",(0,1)\n");
//        strcat(response, "+QCFG: \"vonr\",(0,1)\n");
//        return AT_OK;
//    case AT_SET_OP:
//        ret = cmd_handle_dispatch(cmdline, response);
//        if (ret == 0)
//            return AT_OK;
//        else
//            return AT_ERROR;
//    case AT_READ_OP:
//        LOGATCI(LOG_DEBUG, "this cmd is not support\n");
//        return AT_ERROR;
//    default:
//        break;
//    }
//
//    return ret < 0 ? AT_ERROR : AT_OK;
//}

/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <linux/pkt_sched.h>
#include <libnl3/netlink/object-api.h>
#include <dlfcn.h>

#include "wifi_hal.h"
#include "common.h"
#include <pthread.h>
interface_info *getIfaceInfo(wifi_interface_handle handle)
{
    return (interface_info *)handle;
}

wifi_handle getWifiHandle(wifi_interface_handle handle)
{
    return getIfaceInfo(handle)->handle;
}

hal_info *getHalInfo(wifi_handle handle)
{
    return (hal_info *)handle;
}

hal_info *getHalInfo(wifi_interface_handle handle)
{
    return getHalInfo(getWifiHandle(handle));
}

wifi_handle getWifiHandle(hal_info *info)
{
    return (wifi_handle)info;
}

wifi_interface_handle getIfaceHandle(interface_info *info)
{
    return (wifi_interface_handle)info;
}

wifi_error wifi_register_handler(wifi_handle handle, int cmd, nl_recvmsg_msg_cb_t func, void *arg)
{
    hal_info *info = (hal_info *)handle;

    pthread_mutex_lock(&info->cb_lock);

    wifi_error result = WIFI_ERROR_OUT_OF_MEMORY;

    for (int i = 0; i < info->num_event_cb; i++) {
        if(info->event_cb[i].nl_cmd == cmd &&
           info->event_cb[i].cb_arg == arg) {
            info->event_cb[i].cb_func = func;
            printf("Updated event handler %p for nl_cmd 0x%0x"
                    " and arg %p", func, cmd, arg);
            pthread_mutex_unlock(&info->cb_lock);
            return WIFI_SUCCESS;
        }
    }

    if (info->num_event_cb < info->alloc_event_cb) {
        info->event_cb[info->num_event_cb].nl_cmd  = cmd;
        info->event_cb[info->num_event_cb].vendor_id  = 0;
        info->event_cb[info->num_event_cb].vendor_subcmd  = 0;
        info->event_cb[info->num_event_cb].cb_func = func;
        info->event_cb[info->num_event_cb].cb_arg  = arg;
        info->num_event_cb++;
        printf("Successfully added event handler %p for command %d", func, cmd);
        result = WIFI_SUCCESS;
    } else {
        result = WIFI_ERROR_OUT_OF_MEMORY;
    }

    pthread_mutex_unlock(&info->cb_lock);
    return result;
}

wifi_error wifi_register_vendor_handler(wifi_handle handle,
        uint32_t id, int subcmd, nl_recvmsg_msg_cb_t func, void *arg)
{
    hal_info *info = (hal_info *)handle;

    pthread_mutex_lock(&info->cb_lock);

    wifi_error result = WIFI_ERROR_OUT_OF_MEMORY;

    for (int i = 0; i < info->num_event_cb; i++) {
        if(info->event_cb[i].vendor_id  == id &&
           info->event_cb[i].vendor_subcmd == subcmd)
        {
            info->event_cb[i].cb_func = func;
            info->event_cb[i].cb_arg  = arg;
            printf("Updated event handler %p for vendor 0x%0x, subcmd 0x%0x"
                " and arg %p", func, id, subcmd, arg);
            pthread_mutex_unlock(&info->cb_lock);
            return WIFI_SUCCESS;
        }
    }

    if (info->num_event_cb < info->alloc_event_cb) {
        info->event_cb[info->num_event_cb].nl_cmd  = NL80211_CMD_VENDOR;
        info->event_cb[info->num_event_cb].vendor_id  = id;
        info->event_cb[info->num_event_cb].vendor_subcmd  = subcmd;
        info->event_cb[info->num_event_cb].cb_func = func;
        info->event_cb[info->num_event_cb].cb_arg  = arg;
        info->num_event_cb++;
        printf("Added event handler %p for vendor 0x%0x, subcmd 0x%0x and arg"
            " %p", func, id, subcmd, arg);
        result = WIFI_SUCCESS;
    } else {
        result = WIFI_ERROR_OUT_OF_MEMORY;
    }

    pthread_mutex_unlock(&info->cb_lock);
    return result;
}

void wifi_unregister_handler(wifi_handle handle, int cmd)
{
    hal_info *info = (hal_info *)handle;

    if (cmd == NL80211_CMD_VENDOR) {
        printf("Must use wifi_unregister_vendor_handler to remove vendor handlers");
        return;
    }

    pthread_mutex_lock(&info->cb_lock);

    for (int i = 0; i < info->num_event_cb; i++) {
        if (info->event_cb[i].nl_cmd == cmd) {
            if(i < info->num_event_cb-1) {
                /* No need to memmove if only one entry exist and deleting
                 * the same, as the num_event_cb will become 0 in this case.
                 */
                memmove(&info->event_cb[i], &info->event_cb[i+1],
                        (info->num_event_cb - i) * sizeof(cb_info));
            }
            info->num_event_cb--;
            printf("Successfully removed event handler for command %d", cmd);
            break;
        }
    }

    pthread_mutex_unlock(&info->cb_lock);
}

void wifi_unregister_vendor_handler(wifi_handle handle, uint32_t id, int subcmd)
{
    hal_info *info = (hal_info *)handle;

    pthread_mutex_lock(&info->cb_lock);

    for (int i = 0; i < info->num_event_cb; i++) {

        if (info->event_cb[i].nl_cmd == NL80211_CMD_VENDOR
                && info->event_cb[i].vendor_id == id
                && info->event_cb[i].vendor_subcmd == subcmd) {
            if(i < info->num_event_cb-1) {
                /* No need to memmove if only one entry exist and deleting
                 * the same, as the num_event_cb will become 0 in this case.
                 */
                memmove(&info->event_cb[i], &info->event_cb[i+1],
                        (info->num_event_cb - i) * sizeof(cb_info));
            }
            info->num_event_cb--;
            printf("Successfully removed event handler for vendor 0x%0x", id);
            break;
        }
    }

    pthread_mutex_unlock(&info->cb_lock);
}


wifi_error wifi_register_cmd(wifi_handle handle, int id, WifiCommand *cmd)
{
    hal_info *info = (hal_info *)handle;

    if (info->num_cmd < info->alloc_cmd) {
        info->cmd[info->num_cmd].id   = id;
        info->cmd[info->num_cmd].cmd  = cmd;
        info->num_cmd++;
        printf("Successfully added command %d: %p", id, cmd);
        return WIFI_SUCCESS;
    } else {
        return WIFI_ERROR_OUT_OF_MEMORY;
    }
}

WifiCommand *wifi_unregister_cmd(wifi_handle handle, int id)
{
    hal_info *info = (hal_info *)handle;

    for (int i = 0; i < info->num_cmd; i++) {
        if (info->cmd[i].id == id) {
            WifiCommand *cmd = info->cmd[i].cmd;
            memmove(&info->cmd[i], &info->cmd[i+1], (info->num_cmd - i) * sizeof(cmd_info));
            info->num_cmd--;
            printf("Successfully removed command %d: %p", id, cmd);
            return cmd;
        }
    }

    return NULL;
}

void wifi_unregister_cmd(wifi_handle handle, WifiCommand *cmd)
{
    hal_info *info = (hal_info *)handle;

    for (int i = 0; i < info->num_cmd; i++) {
        if (info->cmd[i].cmd == cmd) {
            int id = info->cmd[i].id;
            memmove(&info->cmd[i], &info->cmd[i+1], (info->num_cmd - i) * sizeof(cmd_info));
            info->num_cmd--;
            printf("Successfully removed command %d: %p", id, cmd);
            return;
        }
    }
}

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

void hexdump(void *buf, u16 len)
{
    int i=0;
    unsigned char *bytes = (unsigned char *)buf;

    if (len) {
        printf("\n*******HexDump len:%d*********\n", len);
        for (i = 0; ((i + 7) < len); i+=8) {
            printf("\t %02x %02x %02x %02x  %02x %02x %02x %02x \n",
                bytes[i], bytes[i+1],
                bytes[i+2], bytes[i+3],
                bytes[i+4], bytes[i+5],
                bytes[i+6], bytes[i+7]);
        }

        if ((len - i) >= 4) {
            printf("\t %02x %02x %02x %02x",
                bytes[i], bytes[i+1],
                bytes[i+2], bytes[i+3]);
            i+=4;
        }
        for (;i < len;i++) {
            printf("\t %02x", bytes[i]);
        }
        printf("\n******HexDump End***********");
    } else {
        return;
    }
}

/* Firmware sends RSSI value without noise floor.
 * Add noise floor to the same and return absolute values.
 */
u8 get_rssi(u8 rssi_wo_noise_floor)
{
    return abs((int)rssi_wo_noise_floor - 96);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

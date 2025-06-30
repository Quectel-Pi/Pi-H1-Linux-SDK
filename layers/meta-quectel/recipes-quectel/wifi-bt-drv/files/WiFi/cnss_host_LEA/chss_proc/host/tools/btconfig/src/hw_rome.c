/*
 *
 *  Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *  Not a Contribution.
 *
 *  Copyright 2012 The Android Open Source Project
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you
 *  may not use this file except in compliance with the License. You may
 *  obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 *  implied. See the License for the specific language governing
 *  permissions and limitations under the License.
 *
 */

/******************************************************************************
 *
 *  Filename:      hw_rome.c
 *
 *  Description:   Contains controller-specific functions, like
 *                      firmware patch download
 *                      low power mode operations
 *
 ******************************************************************************/
#ifdef __cplusplus
extern "C"
{
#endif

#define LOG_TAG "bt_vendor"
//#include <sys/socket.h>
#ifdef ANDROID
#include <libgen.h>
#include <sys/socket.h>
#include <utils/Log.h>
#else
#include <stdint.h>
#include <stdio.h>
#endif
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#ifdef ANDROID
#include <cutils/properties.h>
#endif
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>

#include "hci_uart.h"
#include "hw_rome.h"

#ifdef ANDROID
#define BT_VERSION_FILEPATH "/data/misc/bluedroid/bt_fw_version.txt"
#else
#define BT_VERSION_FILEPATH "./bt_fw_version.txt"
#endif

#ifdef __cplusplus
}
#endif

#define RESERVED(p) \
    if (p)          \
        LOG_I("%s: reserved param", __FUNCTION__);

#define TRUE 1
#define FALSE 0
int read_vs_hci_event(int fd, unsigned char *buf, int size);

/******************************************************************************
**  Variables
******************************************************************************/
struct bt_qcom_struct q;

FILE *file;
unsigned char *phdr_buffer;
unsigned char *pdata_buffer = NULL;
patch_info rampatch_patch_info;
int chipset_ver = 0;
uint32_t g_product_id;
uint32_t g_rom_ver;
uint32_t g_soc_id;
unsigned char gTlv_type;
unsigned char gTlv_dwndCfg;
static unsigned int wipower_flag = 0;
static unsigned int wipower_handoff_ready = 0;
char *rampatch_file_path = NULL;
char *nvm_file_path = NULL;
char *fw_su_info = NULL;
unsigned short fw_su_offset = 0;
extern char enable_extldo;
unsigned char wait_vsc_evt = TRUE;
bool patch_dnld_pending = FALSE;
int dnld_fd = -1;

#define MAX_BTFW_PATH 256
static char btfw_rampatch_path[MAX_BTFW_PATH];
static char btfw_nvm_path[MAX_BTFW_PATH];

static const char *BT_FW_DIRS[] = {"/lib/firmware/updates",
                                   "/lib/firmware/image", "/lib/firmware/ar3k",
                                   "/firmware/image", NULL
                                  };

extern int unified_hci;
uint8_t soc_bd_addr[6] = {0x00, 0x00, 0x00,
                          0x00, 0x00, 0x00
                         }; // To check BD ADDR is programed

/******************************************************************************
**  Extern variables
******************************************************************************/

/*****************************************************************************
**   Functions
*****************************************************************************/
int do_write(int fd, unsigned char *buf, int len)
{
    int ret = 0;
    int write_offset = 0;
    int write_len = len;
    do
    {
        ret = write(fd, buf + write_offset, write_len);
        if (ret < 0)
        {
            LOG_E("%s, write failed ret = %d err = %s", __func__, ret,
                  strerror(errno));
            return -1;
        }
        else if (ret == 0)
        {
            LOG_E("%s, write failed with ret 0 err = %s", __func__, strerror(errno));
            return 0;
        }
        else
        {
            if (ret < write_len)
            {
                LOG_D("%s, Write pending,do write ret = %d err = %s", __func__, ret,
                      strerror(errno));
                write_len = write_len - ret;
                write_offset = ret;
            }
            else
            {
                LOG_V("Write successful");
                break;
            }
        }
    } while (1);
    return len;
}

int get_vs_hci_event(unsigned char *rsp)
{
    int err = 0;
    int paramlen = 0;
    unsigned char EMBEDDED_MODE_CHECK = 0x02;
    FILE *btversionfile = 0;
    unsigned short patchversion = 0;
    char build_label[255];
    int build_lbl_len;
    unsigned int opcode = 0;
    unsigned char subOpcode = 0;
    unsigned int ocf = 0;
    unsigned int ogf = 0;
    unsigned char status = 0;
    bool ret = false;
    unsigned char tu8;

    if ((rsp[EVENTCODE_OFFSET] == VSEVENT_CODE) ||
            (rsp[EVENTCODE_OFFSET] == EVT_CMD_COMPLETE))
    {
        BT_DEBUG("\n");
    }
    else
    {
        LOG_I("%s: Failed to receive HCI-Vendor Specific event", __FUNCTION__);
        err = -EIO;
        goto failed;
    }
    paramlen = rsp[EVT_PLEN];
    LOG_I("%s: Parameter Length: 0x%x", __func__, paramlen);
    if (!unified_hci)
    {
        ocf = rsp[CMD_RSP_OFFSET];
        subOpcode = rsp[RSP_TYPE_OFFSET];
        status = rsp[CMD_STATUS_OFFSET];
        LOG_I("%s: Command response: 0x%x", __func__, ocf);
        LOG_I("%s: Response type   : 0x%x", __func__, subOpcode);
    }
    else
    {
        opcode = rsp[5] << 8 | rsp[4];
        ocf = opcode & 0x03ff;
        ogf = opcode >> 10;
        status = rsp[6];
        subOpcode = rsp[7];
        LOG_I("%s: Opcode: 0x%x", __func__, opcode);
        LOG_I("%s: ocf: 0x%x", __func__, ocf);
        LOG_I("%s: ogf: 0x%x", __func__, ogf);
        LOG_I("%s: Status: 0x%x", __func__, status);
        LOG_I("%s: Sub-Opcode: 0x%x", __func__, subOpcode);
    }

    /* Check the status of the operation */
    switch (ocf)
    {
    case EDL_CMD_REQ_RES_EVT:
        LOG_I("%s: Command Request Response", __FUNCTION__);
        switch (subOpcode)
        {
        case EDL_PATCH_VER_RES_EVT:
        case EDL_APP_VER_RES_EVT:
            if (!unified_hci)
            {
                g_product_id = (uint32_t)(rsp[PATCH_PROD_ID_OFFSET + 3] << 24 |
                                          rsp[PATCH_PROD_ID_OFFSET + 2] << 16 |
                                          rsp[PATCH_PROD_ID_OFFSET + 1] << 8 |
                                          rsp[PATCH_PROD_ID_OFFSET]);
                patchversion =
                    (unsigned short)(rsp[PATCH_PATCH_VER_OFFSET + 1] << 8 |
                                     rsp[PATCH_PATCH_VER_OFFSET]);
                g_rom_ver = (uint32_t)(rsp[PATCH_ROM_BUILD_VER_OFFSET + 1] << 8 |
                                       rsp[PATCH_ROM_BUILD_VER_OFFSET]);

                LOG_I("\t Current Product ID\t\t: 0x%08x", g_product_id);
                LOG_I("\t Current Patch Version\t\t: 0x%04x", patchversion);
                LOG_I("\t Current ROM Build Version\t: 0x%04x", g_rom_ver);

                if (paramlen - 10)
                {
                    g_soc_id = (uint32_t)(rsp[PATCH_SOC_VER_OFFSET + 3] << 24 |
                                          rsp[PATCH_SOC_VER_OFFSET + 2] << 16 |
                                          rsp[PATCH_SOC_VER_OFFSET + 1] << 8 |
                                          rsp[PATCH_SOC_VER_OFFSET]);
                    LOG_I("\t Current SOC Version\t\t: 0x%08x", g_soc_id);
                }
            }
            else
            {
                g_product_id =
                    (uint32_t)(rsp[PATCH_PROD_ID_OFFSET_UNIFIED + 3] << 24 |
                               rsp[PATCH_PROD_ID_OFFSET_UNIFIED + 2] << 16 |
                               rsp[PATCH_PROD_ID_OFFSET_UNIFIED + 1] << 8 |
                               rsp[PATCH_PROD_ID_OFFSET_UNIFIED]);
                LOG_I("\t unified Current Product ID\t\t: 0x%08x", g_product_id);

                /* Patch Version indicates FW patch version */
                patchversion =
                    (unsigned short)(rsp[PATCH_PATCH_VER_OFFSET_UNIFIED + 1] << 8 |
                                     rsp[PATCH_PATCH_VER_OFFSET_UNIFIED]);
                LOG_I("\t unified Current Patch Version\t\t: 0x%04x", patchversion);

                /* ROM Build Version indicates ROM build version like 1.0/1.1/2.0
                 */
                g_rom_ver =
                    (uint32_t)(rsp[PATCH_ROM_BUILD_VER_OFFSET_UNIFIED + 1] << 8 |
                               rsp[PATCH_ROM_BUILD_VER_OFFSET_UNIFIED]);
                LOG_I("\t unified Current ROM Build Version\t: 0x%04x", g_rom_ver);

                if ((paramlen - 14) > 0)
                {
                    g_soc_id =
                        (uint32_t)(rsp[PATCH_SOC_VER_OFFSET_UNIFIED + 3] << 24 |
                                   rsp[PATCH_SOC_VER_OFFSET_UNIFIED + 2] << 16 |
                                   rsp[PATCH_SOC_VER_OFFSET_UNIFIED + 1] << 8 |
                                   rsp[PATCH_SOC_VER_OFFSET_UNIFIED]);
                    LOG_I("\t unified Current SOC Version\t\t: 0x%08x", g_soc_id);
                }
            }
            if (NULL != (btversionfile = fopen(BT_VERSION_FILEPATH, "wb")))
            {
                fprintf(btversionfile,
                        "Bluetooth Controller Product ID    : 0x%08x\n",
                        g_product_id);
                fprintf(btversionfile,
                        "Bluetooth Controller Patch Version : 0x%04x\n",
                        patchversion);
                fprintf(btversionfile,
                        "Bluetooth Controller Build Version : 0x%04x\n", g_rom_ver);
                fprintf(btversionfile,
                        "Bluetooth Controller SOC Version   : 0x%08x\n", g_soc_id);
                fclose(btversionfile);
            }
            else
            {
                LOG_E("Failed to dump SOC version info. Errno:%d", errno);
            }
            /* Rome Chipset Version can be decided by Patch version and SOC
            version, Upper 2 bytes will be used for Patch version and Lower 2
            bytes will be used for SOC as combination for BT host driver */
            chipset_ver = (g_rom_ver << 16) | (g_soc_id & 0x0000ffff);

            break;
        case EDL_PATCH_TLV_REQ_CMD:
        case EDL_TVL_DNLD_RES_EVT:
        case EDL_CMD_EXE_STATUS_EVT:
            err = status;
            switch (err)
            {
            case HCI_CMD_SUCCESS:
                LOG_I("%s: Download Packet successfully!", __FUNCTION__);
                break;
            case PATCH_LEN_ERROR:
                LOG_I(
                    "%s: Invalid patch length argument passed for EDL PATCH "
                    "SET REQ cmd",
                    __FUNCTION__);
                break;
            case PATCH_VER_ERROR:
                LOG_I(
                    "%s: Invalid patch version argument passed for EDL PATCH "
                    "SET REQ cmd",
                    __FUNCTION__);
                break;
            case PATCH_CRC_ERROR:
                LOG_I("%s: CRC check of patch failed!!!", __FUNCTION__);
                break;
            case PATCH_NOT_FOUND:
                LOG_I("%s: Invalid patch data!!!", __FUNCTION__);
                break;
            case TLV_TYPE_ERROR:
                LOG_I("%s: TLV Type Error !!!", __FUNCTION__);
                break;
            default:
                LOG_I("%s: Undefined error (0x%x)", __FUNCTION__, err);
                break;
            }
            break;
        case EDL_GET_BUILD_INFO:
        case HCI_VS_GET_BUILD_VER_EVT:
            if (!unified_hci)
            {
                build_lbl_len = rsp[5];
                memcpy(build_label, &rsp[6], build_lbl_len);
            }
            else
            {
                build_lbl_len = rsp[8];
                memcpy(build_label, &rsp[9], build_lbl_len);
            }

            *(build_label + build_lbl_len) = '\0';

            LOG_I("BT SoC FW SU Build info: %s, %d", build_label, build_lbl_len);
            if (NULL != (btversionfile = fopen(BT_VERSION_FILEPATH, "a+b")))
            {
                fprintf(btversionfile, "Bluetooth Contoller SU Build info  : %s\n",
                        build_label);
                fclose(btversionfile);
            }
            else
            {
                LOG_I("Failed to dump  FW SU build info. Errno:%d", errno);
            }
            break;
        }
        break;

    case NVM_ACCESS_CODE:
        LOG_I("%s: NVM Access Code!!!", __FUNCTION__);
        err = HCI_CMD_SUCCESS;
        break;

    case HCI_VS_WR_BT_ADDR:
        LOG_I("%s: HCI_VS_WR_BT_ADDR Done, status=%#x", __FUNCTION__, status);
        if (status)
            err = -1;
        else
            err = HCI_CMD_SUCCESS;
        break;

    case EDL_SET_BAUDRATE_CMD_OCF:
    case EDL_SET_BAUDRATE_RSP_EVT:
        tu8 = unified_hci ? status : rsp[BAUDRATE_RSP_STATUS_OFFSET];
        /* Rome 1.1 has bug with the response, so it should ignore it. */
        if (tu8 != BAUDRATE_CHANGE_SUCCESS)
        {
            LOG_E("%s: Set Baudrate request failed - 0x%x", __FUNCTION__,
                  rsp[CMD_STATUS_OFFSET]);
            err = -1;
        }
        break;
    case EDL_WIP_QUERY_CHARGING_STATUS_EVT:
        /* Query charging command has below return values
        0 - in embedded mode not charging
        1 - in embedded mode and charging
        2 - hadofff completed and in normal mode
        3 - no wipower supported on mtp. so irrepective of charging
        handoff command has to be sent if return values are 0 or 1.
        These change include logic to enable generic BT turn on sequence.*/
        if (rsp[4] < EMBEDDED_MODE_CHECK)
        {
            LOG_I("%s: WiPower Charging in Embedded Mode!!!", __FUNCTION__);
            wipower_handoff_ready = rsp[4];
            wipower_flag = 1;
        }
        break;
    case EDL_WIP_START_HANDOFF_TO_HOST_EVENT:
        /*TODO: rsp code 00 mean no charging
        this is going to change in FW soon*/
        if (rsp[4] == NON_WIPOWER_MODE)
        {
            LOG_E("%s: WiPower Charging hand off not ready!!!", __FUNCTION__);
        }
        break;
    case HCI_VS_GET_ADDON_FEATURES_EVENT:
        tu8 = unified_hci ? rsp[11] : rsp[4];
        if ((tu8 & ADDON_FEATURES_EVT_WIPOWER_MASK))
        {
            LOG_I("%s: WiPower feature supported!!", __FUNCTION__);
            // property_set_bt("persist.bluetooth.a4wp", "true");
        }
        break;
    case HCI_VS_STRAY_EVT:
        if (chipset_ver == HASTINGS_VER_2_0)
        {
            if (unified_hci)
            {
                err = status ? -1 : 0;
                LOG_I(
                    "%s: CCE for controller log, status = 0x%x, subOpcode = "
                    "0x%x",
                    __func__, status, subOpcode);
            }
            break;
        }
        /* WAR to handle stray Power Apply EVT during patch download */
        LOG_I("%s: Stray HCI VS EVENT", __FUNCTION__);
        if (patch_dnld_pending && dnld_fd != -1)
        {
            unsigned char rsp[HCI_MAX_EVENT_SIZE];
            memset(rsp, 0x00, HCI_MAX_EVENT_SIZE);
            read_vs_hci_event(dnld_fd, rsp, HCI_MAX_EVENT_SIZE);
        }
        else
        {
            LOG_E("%s: Not a valid status!!!", __FUNCTION__);
            err = -1;
        }
        break;
    default:
        LOG_E("%s: Not a valid status!!!", __FUNCTION__);
        err = -1;
        break;
    }

failed:
    return err;
}

/*
 * Read an VS HCI event from the given file descriptor.
 */
int read_vs_hci_event(int fd, unsigned char *buf, int size)
{
    int remain, r;
    int count = 0, i;
    unsigned char wake_byte;
    unsigned short int opcode;

    if (size <= 0)
    {
        LOG_E("Invalid size arguement!");
        return -1;
    }

    LOG_I("%s: Wait for HCI-Vendor Specfic Event from SOC", __FUNCTION__);

    /* The first byte identifies the packet type. For HCI event packets, it
     * should be 0x04, so we read until we get to the 0x04. */
    /* It will keep reading until find 0x04 byte */
    while (1)
    {
        r = read(fd, buf, 1);
        if (r <= 0)
            return -1;
        if (buf[0] == 0x04)
            break;
    }
    count++;

    /* The next two bytes are the event code and parameter total length. */
    while (count < 3)
    {
        r = read(fd, buf + count, 3 - count);
        if ((r <= 0) || ((buf[1] != 0xFF) && (buf[1] != EVT_CMD_COMPLETE)))
        {
            LOG_E("It is not VS event !! ret: %d, EVT: %d", r, buf[1]);
            return -1;
        }
        count += r;
    }

    /* Now we read the parameters. */
    if (buf[2] < (size - 3))
        remain = buf[2];
    else
        remain = size - 3;

    while ((count - 3) < remain)
    {
        r = read(fd, buf + count, remain - (count - 3));
        if (r <= 0)
            return -1;
        count += r;
    }

    if (buf[1] == VSEVENT_CODE)
    {
        LOG_V("VSC Event! good");
    }
    else if (buf[1] == EVT_CMD_COMPLETE)
    {
        LOG_I("%s: Expected CC", __func__);
        if (count > UNIFIED_HCI_CC_MIN_LENGTH)
        {
            opcode = (buf[4] | (buf[5] << 8));
            if (((HCI_VS_WIPOWER_CMD_OPCODE == opcode) &&
                    (UNIFIED_HCI_CODE == buf[6])) ||
                    ((HCI_VS_GET_VER_CMD_OPCODE == opcode) &&
                     (buf[7] == EDL_PATCH_VER_REQ_CMD)))
            {
                unified_hci = 1;
                LOG_I("HCI Unified command interface supported");
            }
        }
        if (unified_hci)
        {
            return (get_vs_hci_event(buf) == HCI_CMD_SUCCESS) ? count : -1;
        }
    }
    else
    {
        LOG_I("%s: Unexpected event! : opcode: %d", __func__, buf[1]);
        count = -1;
        return count;
    }

    /* Check if the set patch command is successful or not */
    if (get_vs_hci_event(buf) != HCI_CMD_SUCCESS)
        return -1;

    return count;
}

int hci_send_vs_cmd(int fd, unsigned char *cmd, unsigned char *rsp, int size)
{
    int ret = 0;
    int rv;

    /* Send the HCI command packet to UART for transmission */
    ret = do_write(fd, cmd, size);
    if (ret != size)
    {
        LOG_E("%s: Send failed with ret value: %d", __FUNCTION__, ret);
        goto failed;
    }

    if (wait_vsc_evt)
    {
        /* Check for response from the Controller */
        if (!unified_hci)
        {
            if (read_vs_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE) < 0)
            {
                ret = -ETIMEDOUT;
                LOG_I("%s: Failed to get ReadVsHciEvent Event from SOC", __func__);
                goto failed;
            }
            LOG_V("%s: Received HCI-Vendor Specific Event from SOC", __func__);
        }
        else
        {
            if (read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE) < 0)
            {
                ret = -ETIMEDOUT;
                LOG_I("%s: Failed to get ReadHciEvent Event from SOC", __func__);
                goto failed;
            }
            LOG_V("%s: Received HCI-Vendor Specific Event from SOC", __func__);
        }
    }

failed:
    return ret;
}

void frame_hci_cmd_pkt(unsigned char *cmd, int edl_cmd,
                       unsigned int p_base_addr, int segtNo, int size)
{
    int offset = 0;
    hci_command_hdr *cmd_hdr;

    memset(cmd, 0x0, HCI_MAX_CMD_SIZE);

    cmd_hdr = (void *)(cmd + 1);

    cmd[0] = HCI_COMMAND_PKT;
    cmd_hdr->opcode = cmd_opcode_pack(HCI_VENDOR_CMD_OGF, HCI_PATCH_CMD_OCF);
    cmd_hdr->plen = size;
    cmd[4] = edl_cmd;

    switch (edl_cmd)
    {
    case EDL_PATCH_SET_REQ_CMD:
        /* Copy the patch header info as CMD params */
        memcpy(&cmd[5], phdr_buffer, PATCH_HDR_LEN);
        LOG_D("%s: Sending EDL_PATCH_SET_REQ_CMD", __FUNCTION__);
        LOG_D("HCI-CMD %d:\t0x%x \t0x%x \t0x%x \t0x%x \t0x%x", segtNo, cmd[0],
              cmd[1], cmd[2], cmd[3], cmd[4]);
        break;
    case EDL_PATCH_DLD_REQ_CMD:
        offset = ((segtNo - 1) * MAX_DATA_PER_SEGMENT);
        p_base_addr += offset;
        cmd_hdr->plen = (size + 6);
        cmd[5] = (size + 4);
        cmd[6] = EXTRACT_BYTE(p_base_addr, 0);
        cmd[7] = EXTRACT_BYTE(p_base_addr, 1);
        cmd[8] = EXTRACT_BYTE(p_base_addr, 2);
        cmd[9] = EXTRACT_BYTE(p_base_addr, 3);
        memcpy(&cmd[10], (pdata_buffer + offset), size);

        LOG_D("%s: Sending EDL_PATCH_DLD_REQ_CMD: size: %d bytes", __FUNCTION__,
              size);
        LOG_D(
            "HCI-CMD %d:\t0x%x\t0x%x\t0x%x\t0x%x\t0x%x\t0x%x\t0x%x\t"
            "0x%x\t0x%x\t0x%x\t\n",
            segtNo, cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6],
            cmd[7], cmd[8], cmd[9]);
        break;
    case EDL_PATCH_ATCH_REQ_CMD:
        LOG_D("%s: Sending EDL_PATCH_ATTACH_REQ_CMD", __FUNCTION__);
        LOG_D("HCI-CMD %d:\t0x%x \t0x%x \t0x%x \t0x%x \t0x%x", segtNo, cmd[0],
              cmd[1], cmd[2], cmd[3], cmd[4]);
        break;
    case EDL_PATCH_RST_REQ_CMD:
        LOG_D("%s: Sending EDL_PATCH_RESET_REQ_CMD", __FUNCTION__);
        LOG_D("HCI-CMD %d:\t0x%x \t0x%x \t0x%x \t0x%x \t0x%x", segtNo, cmd[0],
              cmd[1], cmd[2], cmd[3], cmd[4]);
        break;
    case EDL_PATCH_VER_REQ_CMD:
        LOG_D("%s: Sending EDL_PATCH_VER_REQ_CMD", __FUNCTION__);
        LOG_D("HCI-CMD %d:\t0x%x \t0x%x \t0x%x \t0x%x \t0x%x", segtNo, cmd[0],
              cmd[1], cmd[2], cmd[3], cmd[4]);
        break;
    case EDL_PATCH_TLV_REQ_CMD:
        LOG_D("%s: Sending EDL_PATCH_TLV_REQ_CMD", __FUNCTION__);
        /* Parameter Total Length */
        cmd[3] = size + 2;

        /* TLV Segment Length */
        cmd[5] = size;
        LOG_D("HCI-CMD %d:\t0x%x \t0x%x \t0x%x \t0x%x \t0x%x \t0x%x", segtNo,
              cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5]);
        offset = (segtNo * MAX_SIZE_PER_TLV_SEGMENT);
        memcpy(&cmd[6], (pdata_buffer + offset), size);
        printf("size =%d\n", size);
        // for (int i = 0; i < size; i++) {
        //  printf("0x%x ", cmd[i]);
        //  if (i % 20 == 0) {
        //    LOG_D("\n");
        //  }
        // }
        // printf("\n");
        // printf("Buffer Bit: %d:\t0x%x\n", segtNo, cmd[6]);
        break;
    case EDL_GET_BUILD_INFO:
        LOG_D("%s: Sending EDL_GET_BUILD_INFO", __FUNCTION__);
        LOG_D("HCI-CMD %d:\t0x%x \t0x%x \t0x%x \t0x%x \t0x%x", segtNo, cmd[0],
              cmd[1], cmd[2], cmd[3], cmd[4]);
        break;
    default:
        LOG_E("%s: Unknown EDL CMD !!!", __FUNCTION__);
    }
}

void update_new_nvm_format(tlv_nvm_hdr *nvm)
{
    int enable_ibs = 0;
    uint8_t *nvm_byte_ptr = (uint8_t *)nvm;

    if (!nvm)
        return;

    nvm_byte_ptr += sizeof(tlv_nvm_hdr);
    /* Update Tag#17: HCI UART Settings */
    if (nvm->tag_id == 17)
    {
        uint8_t baudrate = BAUDRATE_3000000;

        LOG_I("%s: baudrate %02x", __func__, baudrate);

        /* Byte#1: UART baudrate */
        *(nvm_byte_ptr + 1) = baudrate;
    }

    /* Update Tag#27: SIBS Settings */
    if (nvm->tag_id == 27)
    {
        if (!enable_ibs)
        {
            /* TxP Sleep Mode: Disable */
            *(nvm_byte_ptr + 1) &= ~0x01;
            LOG_I("%s: SIBS Disable", __func__);
        }
        else
        {
            /* TxP Sleep Mode-1:UART_SIBS, 2:USB_SUSPEND, 3: GPIO_OOB, 4:
             * UART_HWIBS */
            *(nvm_byte_ptr + 1) |= 0x01;
            LOG_I("%s: SIBS Enable", __func__);
        }
    }
}

int rome_get_tlv_file(char *file_path)
{
    FILE *pFile;
    long fileSize;
    int readSize, err = 0, total_segment, remain_size, nvm_length, nvm_index, i;
    unsigned short nvm_tag_len;
    tlv_patch_info *ptlv_header;
    tlv_nvm_hdr *nvm_ptr;
    unsigned char data_buf[PRINT_BUF_SIZE] = {
        0,
    };
    unsigned char *nvm_byte_ptr;

    LOG_I("File Open (%s)", file_path);
    pFile = fopen(file_path, "r");
    if (pFile == NULL)
    {
        ;
        LOG_E("%s File Open Fail", file_path);
        return -1;
    }

    /* Get File Size */
    fseek(pFile, 0, SEEK_END);
    fileSize = ftell(pFile);
    rewind(pFile);

    pdata_buffer = (unsigned char *)malloc(sizeof(char) * fileSize);
    if (pdata_buffer == NULL)
    {
        LOG_E("Allocated Memory failed");
        fclose(pFile);
        return -1;
    }

    /* Copy file into allocated buffer */
    readSize = fread(pdata_buffer, 1, fileSize, pFile);

    /* File Close */
    fclose(pFile);

    if (readSize != fileSize)
    {
        LOG_E("Read file size(%d) not matched with actual file size (%ld bytes)",
              readSize, fileSize);
        return -1;
    }

    ptlv_header = (tlv_patch_info *)pdata_buffer;

    /* To handle different event between rampatch and NVM */
    gTlv_type = ptlv_header->tlv_type;
    gTlv_dwndCfg = ptlv_header->tlv.patch.dwnd_cfg;

    if (ptlv_header->tlv_type == TLV_TYPE_PATCH)
    {
        LOG_I("====================================================\n");
        LOG_I("TLV Type\t\t\t : 0x%x\n", ptlv_header->tlv_type);
        LOG_I("Length\t\t\t : %d bytes\n", (ptlv_header->tlv_length1) |
              (ptlv_header->tlv_length2 << 8) |
              (ptlv_header->tlv_length3 << 16));
        LOG_I("Total Length\t\t\t : %d bytes\n",
              ptlv_header->tlv.patch.tlv_data_len);
        LOG_I("Patch Data Length\t\t\t : %d bytes\n",
              ptlv_header->tlv.patch.tlv_patch_data_len);
        LOG_I("Signing Format Version\t : 0x%x\n", ptlv_header->tlv.patch.sign_ver);
        LOG_I("Signature Algorithm\t\t : 0x%x\n",
              ptlv_header->tlv.patch.sign_algorithm);
        LOG_I("Event Handling\t\t\t : 0x%x\n", ptlv_header->tlv.patch.dwnd_cfg);
        LOG_I("Reserved\t\t\t : 0x%x\n", ptlv_header->tlv.patch.reserved1);
        LOG_I("Product ID\t\t\t : 0x%04x\n", ptlv_header->tlv.patch.prod_id);
        LOG_I("Rom Build Version\t\t : 0x%04x\n", ptlv_header->tlv.patch.build_ver);
        LOG_I("Patch Version\t\t : 0x%04x\n", ptlv_header->tlv.patch.patch_ver);
        LOG_I("Reserved\t\t\t : 0x%x\n", ptlv_header->tlv.patch.reserved2);
        LOG_I("Patch Entry Address\t\t : 0x%x\n",
              (ptlv_header->tlv.patch.patch_entry_addr));
        LOG_I("====================================================");
    }
    else if (ptlv_header->tlv_type == TLV_TYPE_NVM)
    {
        LOG_I("====================================================\n");
        LOG_I("TLV Type\t\t\t : 0x%x\n", ptlv_header->tlv_type);
        nvm_length = ((ptlv_header->tlv_length1) | (ptlv_header->tlv_length2 << 8) |
                      (ptlv_header->tlv_length3 << 16));
        LOG_I("Length\t\t\t : %d bytes\n", nvm_length);

        if (nvm_length <= 0)
            return readSize;

        for (nvm_byte_ptr = (unsigned char *)(nvm_ptr = &(ptlv_header->tlv.nvm)),
                nvm_index = 0;
                nvm_index < nvm_length; nvm_ptr = (tlv_nvm_hdr *)nvm_byte_ptr)
        {
            LOG_I("TAG ID\t\t\t : %d", nvm_ptr->tag_id);
            nvm_tag_len = nvm_ptr->tag_len;
            LOG_I("TAG Length\t\t\t : %d", nvm_tag_len);
            LOG_I("TAG Pointer\t\t\t : %d", nvm_ptr->tag_ptr);
            LOG_I("TAG Extended Flag\t\t : %d", nvm_ptr->tag_ex_flag);

            /* Increase nvm_index to NVM data */
            nvm_index += sizeof(tlv_nvm_hdr);
            nvm_byte_ptr += sizeof(tlv_nvm_hdr);

#if 1
            /* Write BD Address */
            if (nvm_ptr->tag_id == TAG_NUM_2)
            {
                memcpy(nvm_byte_ptr, q.bdaddr, 6);
                LOG_I("BD Address: %.02x:%.02x:%.02x:%.02x:%.02x:%.02x", *nvm_byte_ptr,
                      *(nvm_byte_ptr + 1), *(nvm_byte_ptr + 2), *(nvm_byte_ptr + 3),
                      *(nvm_byte_ptr + 4), *(nvm_byte_ptr + 5));
            }
#endif

            if (chipset_ver == HASTINGS_VER_2_0)
            {
                update_new_nvm_format(nvm_ptr);
            }
            else
            {

                /* Update Tag#27: SIBS Settings */
                if (nvm_ptr->tag_id == 17)
                {

                    /* TxP Sleep Mode: Disable */
                    //*(nvm_byte_ptr) &= (~(0x01 << 7));
                }

                if (nvm_ptr->tag_id == 27)
                {
                    /* TxP Sleep Mode: Disable */
                    //*(nvm_byte_ptr) &= ~0x01;
                    LOG_I("%s: SIBS Disable", __func__);

                }
            }

            for (i = 0; (i < nvm_ptr->tag_len && (i * 3 + 2) < PRINT_BUF_SIZE); i++)
                snprintf((char *)data_buf, PRINT_BUF_SIZE, "%s%.02x ", (char *)data_buf,
                         *(nvm_byte_ptr + i));

            LOG_I("TAG Data\t\t\t : %s", data_buf);

            /* Clear buffer */
            memset(data_buf, 0x0, PRINT_BUF_SIZE);

            /* increased by tag_len */
            nvm_index += nvm_ptr->tag_len;
            nvm_byte_ptr += nvm_ptr->tag_len;
        }

        LOG_I("====================================================");
    }
    else
    {
        LOG_I("TLV Header type is unknown (%d) ", ptlv_header->tlv_type);
    }

    return readSize;
}

int rome_tlv_dnld_segment(int fd, int index, int seg_size,
                          unsigned char wait_cc_evt)
{
    int size = 0, err = -1;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    unsigned char rsp[HCI_MAX_EVENT_SIZE];

    LOG_I("%s: Downloading TLV Patch segment no.%d, size:%d wait_cc_evt=%d\n",
          __FUNCTION__, index, seg_size, wait_cc_evt);

    /* Frame the HCI CMD PKT to be sent to Controller*/
    frame_hci_cmd_pkt(cmd, EDL_PATCH_TLV_REQ_CMD, 0, index, seg_size);

    /* Total length of the packet to be sent to the Controller */
    size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + cmd[PLEN]);

    /* Initialize the RSP packet everytime to 0 */
    memset(rsp, 0x0, HCI_MAX_EVENT_SIZE);

    /* Send HCI Command packet to Controller */
    err = hci_send_vs_cmd(fd, (unsigned char *)cmd, rsp, size);
    if (err != size)
    {
        LOG_E("Failed to send the patch payload to the Controller! 0x%x", err);
        return err;
    }

    if (!unified_hci)
    {
        if (wait_cc_evt)
        {
            /* Initialize the RSP packet everytime to 0 */
            memset(rsp, 0x0, HCI_MAX_EVENT_SIZE);
            err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
            if (err < 0)
            {
                LOG_E("%s: Failed to downlaod patch segment: %d!", __FUNCTION__, index);
                return err;
            }
        }
    }

    LOG_I("%s: Successfully downloaded patch segment: %d", __FUNCTION__, index);
    return err;
}

int rome_tlv_dnld_req(int fd, int tlv_size)
{
    int total_segment, remain_size, i, err = -1;
    unsigned char wait_cc_evt = TRUE;

    total_segment = tlv_size / MAX_SIZE_PER_TLV_SEGMENT;
    remain_size = (tlv_size < MAX_SIZE_PER_TLV_SEGMENT)
                  ? tlv_size
                  : (tlv_size % MAX_SIZE_PER_TLV_SEGMENT);

    LOG_I("%s: TLV size: %d, Total Seg num: %d, remain size: %d", __FUNCTION__,
          tlv_size, total_segment, remain_size);

    if (gTlv_type == TLV_TYPE_PATCH)
    {
        /* Prior to Rome version 3.2(including inital few rampatch release of
         * Rome 3.2), the event handling mechanism is ROME_SKIP_EVT_NONE. After
         * few release of rampatch for Rome 3.2, the mechamism is changed to
         * ROME_SKIP_EVT_VSE_CC. Rest of the mechanism is not used for now
         */
        switch (gTlv_dwndCfg)
        {
        case ROME_SKIP_EVT_NONE:
            wait_vsc_evt = TRUE;
            wait_cc_evt = TRUE;
            LOG_I("Event handling type: ROME_SKIP_EVT_NONE");
            break;
        case ROME_SKIP_EVT_VSE_CC:
            wait_vsc_evt = FALSE;
            wait_cc_evt = FALSE;
            LOG_I("Event handling type: ROME_SKIP_EVT_VSE_CC");
            break;
        /* Not handled for now */
        case ROME_SKIP_EVT_VSE:
        case ROME_SKIP_EVT_CC:
        default:
            LOG_E("Unsupported Event handling: %d", gTlv_dwndCfg);
            break;
        }
    }
    else
    {
        /* This is for the NV items.
         * Expectation is to have the CCE and VSE for each segment except last
         * segment For the last segment, we should get only the VSE
         */
        wait_vsc_evt = TRUE;
        wait_cc_evt = TRUE;
    }

    for (i = 0; i < total_segment; i++)
    {
        if ((i + 1) == total_segment)
        {
            if ((chipset_ver == TUFELLO_VER_1_1) && (gTlv_type == TLV_TYPE_PATCH))
            {
                if (gTlv_dwndCfg == ROME_SKIP_EVT_NONE)
                {
                    wait_cc_evt = !remain_size ? FALSE : TRUE;
                }
                else if (gTlv_dwndCfg == ROME_SKIP_EVT_VSE_CC)
                {
                    wait_vsc_evt = !remain_size ? TRUE : FALSE;
                }
            }
            else if ((chipset_ver >= ROME_VER_1_1) &&
                     (chipset_ver < ROME_VER_3_2) &&
                     (gTlv_type == TLV_TYPE_PATCH))
            {
                /* If the Rome version is from 1.1 to 3.1
                 * 1. No CCE for the last command segment but all other segment
                 * 2. All the command segments get VSE including the last one
                 */
                wait_cc_evt = !remain_size ? FALSE : TRUE;
            }
            else if ((chipset_ver >= ROME_VER_3_2) &&
                     (gTlv_type == TLV_TYPE_PATCH))
            {
                /* If the Rome version is 3.2
                 * 1. None of the command segments receive CCE
                 * 2. No command segments receive VSE except the last one
                 * 3. If gTlv_dwndCfg is ROME_SKIP_EVT_NONE then the logic is
                 *    same as Rome 2.1, 2.2, 3.0
                 */
                if (gTlv_dwndCfg == ROME_SKIP_EVT_NONE)
                {
                    wait_cc_evt = !remain_size ? FALSE : TRUE;
                }
                else if (gTlv_dwndCfg == ROME_SKIP_EVT_VSE_CC)
                {
                    wait_vsc_evt = !remain_size ? TRUE : FALSE;
                }
            }
        }

        patch_dnld_pending = TRUE;
        if ((err = rome_tlv_dnld_segment(fd, i, MAX_SIZE_PER_TLV_SEGMENT,
                                         wait_cc_evt)) < 0)
            goto error;
        patch_dnld_pending = FALSE;
    }

    if ((chipset_ver == TUFELLO_VER_1_1) && (gTlv_type == TLV_TYPE_PATCH))
    {
        if (gTlv_dwndCfg == ROME_SKIP_EVT_NONE)
        {
            wait_cc_evt = remain_size ? FALSE : TRUE;
        }
        else if (gTlv_dwndCfg == ROME_SKIP_EVT_VSE_CC)
        {
            wait_vsc_evt = remain_size ? TRUE : FALSE;
        }
    }
    else if ((chipset_ver == NAPLES_VER_1_0) && (gTlv_type == TLV_TYPE_PATCH))
    {
        if (gTlv_dwndCfg == ROME_SKIP_EVT_NONE)
        {
            wait_cc_evt = remain_size ? FALSE : TRUE;
        }
        else if (gTlv_dwndCfg == ROME_SKIP_EVT_VSE_CC)
        {
            wait_vsc_evt = remain_size ? TRUE : FALSE;
        }
    }
    else if ((chipset_ver == HASTINGS_VER_2_0) &&
             (gTlv_type == TLV_TYPE_PATCH))
    {
        if (gTlv_dwndCfg == ROME_SKIP_EVT_NONE)
        {
            wait_cc_evt = remain_size ? FALSE : TRUE;
        }
        else if (gTlv_dwndCfg == ROME_SKIP_EVT_VSE_CC)
        {
            wait_vsc_evt = remain_size ? TRUE : FALSE;
        }
    }
    else if ((chipset_ver >= ROME_VER_1_1) && (chipset_ver < ROME_VER_3_2) &&
             (gTlv_type == TLV_TYPE_PATCH))
    {
        /* If the Rome version is from 1.1 to 3.1
         * 1. No CCE for the last command segment but all other segment
         * 2. All the command segments get VSE including the last one
         */
        wait_cc_evt = remain_size ? FALSE : TRUE;
    }
    else if ((chipset_ver >= ROME_VER_3_2) && (gTlv_type == TLV_TYPE_PATCH))
    {
        /* If the Rome version is 3.2
         * 1. None of the command segments receive CCE
         * 2. No command segments receive VSE except the last one
         * 3. If gTlv_dwndCfg is ROME_SKIP_EVT_NONE then the logic is
         *    same as Rome 2.1, 2.2, 3.0
         */
        if (gTlv_dwndCfg == ROME_SKIP_EVT_NONE)
        {
            wait_cc_evt = remain_size ? FALSE : TRUE;
        }
        else if (gTlv_dwndCfg == ROME_SKIP_EVT_VSE_CC)
        {
            wait_vsc_evt = remain_size ? TRUE : FALSE;
        }
    }
    patch_dnld_pending = TRUE;
    if (remain_size)
        err = rome_tlv_dnld_segment(fd, i, remain_size, wait_cc_evt);
    patch_dnld_pending = FALSE;

error:
    if (patch_dnld_pending)
        patch_dnld_pending = FALSE;
    return err;
}

int rome_download_tlv_file(int fd)
{
    int tlv_size, err = -1;

    /* Rampatch TLV file Downloading */
    pdata_buffer = NULL;
    if ((tlv_size = rome_get_tlv_file(rampatch_file_path)) < 0)
        goto error;
    LOG_E("%s:  download patch file", __FUNCTION__);
    if ((err = rome_tlv_dnld_req(fd, tlv_size)) < 0)
        goto error;

    if (pdata_buffer != NULL)
    {
        free(pdata_buffer);
        pdata_buffer = NULL;
    }
nvm_download:
    LOG_E("%s:  download nvm file", __FUNCTION__);
    if (!nvm_file_path)
    {
        LOG_I("%s: nvm file is not available", __FUNCTION__);
        err = 0; // in case of nvm/rampatch is not available
        goto error;
    }

    /* NVM TLV file Downloading */
    if ((tlv_size = rome_get_tlv_file(nvm_file_path)) <= 0)
        goto error;

    if ((err = rome_tlv_dnld_req(fd, tlv_size)) < 0)
        goto error;

error:
    if (pdata_buffer != NULL)
        free(pdata_buffer);

    return err;
}

int rome_patch_ver_req(int fd)
{
    int size, err = 0;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    unsigned char rsp[HCI_MAX_EVENT_SIZE];

    /* Frame the HCI CMD to be sent to the Controller */
    frame_hci_cmd_pkt(cmd, EDL_PATCH_VER_REQ_CMD, 0, -1, EDL_PATCH_CMD_LEN);

    /* Total length of the packet to be sent to the Controller */
    size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + EDL_PATCH_CMD_LEN);

    /* Send HCI Command packet to Controller */
    err = hci_send_vs_cmd(fd, (unsigned char *)cmd, rsp, size);
    if (err != size)
    {
        LOG_E("Failed to attach the patch payload to the Controller!");
        goto error;
    }

    /* Read Command Complete Event - This is extra routine for ROME 1.0. From
     * ROM 2.0, it should be removed. */
    if (!unified_hci)
    {
        err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
        if (err < 0)
        {
            LOG_E("%s: Failed to get patch version(s)", __FUNCTION__);
            goto error;
        }
    }

error:
    return err;
}

int rome_get_build_info_req(int fd)
{
    int size, err = 0;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    unsigned char rsp[HCI_MAX_EVENT_SIZE];

    /* Frame the HCI CMD to be sent to the Controller */
    frame_hci_cmd_pkt(cmd, EDL_GET_BUILD_INFO, 0, -1, EDL_PATCH_CMD_LEN);

    /* Total length of the packet to be sent to the Controller */
    size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + EDL_PATCH_CMD_LEN);

    /* Send HCI Command packet to Controller */
    err = hci_send_vs_cmd(fd, (unsigned char *)cmd, rsp, size);
    if (err != size)
    {
        LOG_E("Failed to send get build info cmd to the SoC!");
        goto error;
    }

    if (!unified_hci)
    {
        err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
        if (err < 0)
        {
            LOG_E("%s: Failed to get build info", __FUNCTION__);
            goto error;
        }
    }
error:
    return err;
}

int rome_set_baudrate_req(int fd)
{
    int size, err = 0;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    unsigned char rsp[HCI_MAX_EVENT_SIZE];
    hci_command_hdr *cmd_hdr;
    int flags;
    LOG_E("%s: %d", __FUNCTION__, __LINE__);

    memset(cmd, 0x0, HCI_MAX_CMD_SIZE);

    cmd_hdr = (void *)(cmd + 1);
    cmd[0] = HCI_COMMAND_PKT;
    cmd_hdr->opcode =
        cmd_opcode_pack(HCI_VENDOR_CMD_OGF, EDL_SET_BAUDRATE_CMD_OCF);
    cmd_hdr->plen = VSC_SET_BAUDRATE_REQ_LEN;
    cmd[4] = BAUDRATE_3000000;

    /* Total length of the packet to be sent to the Controller */
    size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + VSC_SET_BAUDRATE_REQ_LEN);

    /* Flow off during baudrate change */
    if ((err = userial_vendor_ioctl(USERIAL_OP_FLOW_OFF, &flags)) < 0)
    {
        LOG_E("%s: HW Flow-off error: 0x%x\n", __FUNCTION__, err);
        goto error;
    }

    /* Send the HCI command packet to UART for transmission */
    err = do_write(fd, cmd, size);
    if (err != size)
    {
        LOG_E("%s: Send failed with ret value: %d", __FUNCTION__, err);
        goto error;
    }

    /* Change Local UART baudrate to high speed UART */
    userial_vendor_set_baud(USERIAL_BAUD_3M);

    /* Flow on after changing local uart baudrate */
    if ((err = userial_vendor_ioctl(USERIAL_OP_FLOW_ON, &flags)) < 0)
    {
        LOG_E("%s: HW Flow-on error: 0x%x \n", __FUNCTION__, err);
        return err;
    }

    if (!unified_hci)
    {
        /* Check for response from the Controller */
        if ((err = read_vs_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE)) < 0)
        {
            LOG_E("%s: Failed to get HCI-VS Event from SOC", __FUNCTION__);
            goto error;
        }
        LOG_I("%s: Received HCI-Vendor Specific Event from SOC", __FUNCTION__);
    }
    LOG_E("%s: %d", __FUNCTION__, __LINE__);
    /* Wait for command complete event */
    err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
    if (err < 0)
    {
        LOG_E("%s: Failed to set patch info on Controller", __FUNCTION__);
        goto error;
    }

error:
    return err;
}

int rome_hci_reset(int fd)
{
    int size, err = 0;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    unsigned char rsp[HCI_MAX_EVENT_SIZE];
    hci_command_hdr *cmd_hdr;
    int flags;

    LOG_I("%s: HCI RESET ", __FUNCTION__);

    memset(cmd, 0x0, HCI_MAX_CMD_SIZE);

    cmd_hdr = (void *)(cmd + 1);
    cmd[0] = HCI_COMMAND_PKT;
    cmd_hdr->opcode = HCI_RESET;
    cmd_hdr->plen = 0;

    /* Total length of the packet to be sent to the Controller */
    size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE);
    err = do_write(fd, cmd, size);
    if (err != size)
    {
        LOG_E("%s: Send failed with ret value: %d", __FUNCTION__, err);
        err = -1;
        goto error;
    }

    /* Wait for command complete event */
    err = read_cmd_compl_event(fd, rsp, HCI_MAX_EVENT_SIZE);
    if (err < 0)
    {
        LOG_E("%s: Failed to set patch info on Controller", __FUNCTION__);
        goto error;
    }
    err = 0;

error:
    return err;
}

void enable_controller_log(int fd, unsigned char wait_for_evt)
{
    int ret = 0;
    /* VS command to enable controller logging to the HOST. By default it is
     * disabled */
    unsigned char cmd[6] = {0x01, 0x17, 0xFC, 0x02, 0x00, 0x00};
    unsigned char rsp[HCI_MAX_EVENT_SIZE];
    char value[PROPERTY_VALUE_MAX] = {'\0'};

    strncpy(value, "false", 5);
    //   property_get_bt("persist.service.bdroid.soclog", value, "false");

    // value at cmd[5]: 1 - to enable, 0 - to disable
    ret = (strcmp(value, "true") == 0) ? cmd[5] = 0x01 : 0;
    LOG_I("%s: %d", __func__, ret);
    /* Ignore vsc evt if wait_for_evt is true */
    if (wait_for_evt)
        wait_vsc_evt = FALSE;

    ret = hci_send_vs_cmd(fd, (unsigned char *)cmd, rsp, 6);
    if (ret != 6)
    {
        LOG_E("%s: command failed", __func__);
    }
    /*Ignore hci_event if wait_for_evt is true*/
    if (wait_for_evt)
        goto end;

    if (!unified_hci || chipset_ver == HASTINGS_VER_2_0)
    {
        ret = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
        if (ret < 0)
        {
            LOG_E("%s: Failed to get CC for enable SoC log", __FUNCTION__);
        }
    }
end:
    wait_vsc_evt = TRUE;
    return;
}

static bool is_extldo_enabled()
{
    bool is_extldo = false;
    char extldo_prop[PROPERTY_VALUE_MAX];

#ifdef ANDROID
    property_get("wc_transport.extldo", extldo_prop, "disabled");
#else
    //  property_get_bt("wc_transport.extldo", extldo_prop, "disabled");
    strncpy(extldo_prop, "disabled", 8);
#endif

    if (!strcmp(extldo_prop, "enabled"))
    {
        is_extldo = true;
    }

    return is_extldo;
}

/* This function is called with q_lock held and q is non-NULL */
static int disable_internal_ldo(int fd)
{
    int ret = 0;
    if (is_extldo_enabled())
    {
        unsigned char cmd[5] = {0x01, 0x0C, 0xFC, 0x01, 0x32};
        unsigned char rsp[HCI_MAX_EVENT_SIZE];

        LOG_I(" %s ", __FUNCTION__);
        ret = do_write(fd, cmd, 5);
        if (ret != 5)
        {
            LOG_E("%s: Send failed with ret value: %d", __FUNCTION__, ret);
            ret = -1;
        }
        else
        {
            /* Wait for command complete event */
            ret = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
            if (ret < 0)
            {
                LOG_E("%s: Failed to get response from controller", __FUNCTION__);
            }
        }
    }
    return ret;
}

static int get_btfw_path(char *name, char *path, int path_size)
{
    int l;
    char **p = BT_FW_DIRS;
    char *b = name;

    while (*p)
    {
        l = snprintf(path, path_size, "%s/%s", *p, b);
        if (l < path_size)
        {
            if (!access(path, R_OK))
            {
                LOG_I("in %s : name = %s path = %s\n", __func__, name, path);
                return 0;
            }
        }
        p++;
    }
    return -1;
}

int rome_soc_init(int fd, char *bdaddr)
{
    int err = -1, size = 0;
    dnld_fd = fd;
    LOG_I(" %s ", __FUNCTION__);
    // RESERVED(bdaddr);
    // memcpy(q.bdaddr, bdaddr, 6);
    /* Get Rome version information */
    if ((err = rome_patch_ver_req(fd)) < 0)
    {
        LOG_I("%s: Fail to get Rome Version (0x%x)", __FUNCTION__, err);
        goto error;
    }

    LOG_I("%s: Chipset Version (0x%08x)", __FUNCTION__, chipset_ver);

    switch (chipset_ver)
    {
    case ROME_VER_1_0:
        break;
    case ROME_VER_1_1:
        rampatch_file_path = ROME_RAMPATCH_TLV_PATH;
        nvm_file_path = ROME_NVM_TLV_PATH;
        goto download;
    case ROME_VER_1_3:
        rampatch_file_path = ROME_RAMPATCH_TLV_1_0_3_PATH;
        nvm_file_path = ROME_NVM_TLV_1_0_3_PATH;
        goto download;
    case NAPLES_VER_1_0:
        if (get_btfw_path(basename(NAPLES_RAMPATCH_TLV_UART_1_0_PATH),
                          btfw_rampatch_path, sizeof(btfw_rampatch_path)))
            rampatch_file_path = NAPLES_RAMPATCH_TLV_UART_1_0_PATH;
        else
            rampatch_file_path = btfw_rampatch_path;

        if (get_btfw_path(basename(NAPLES_NVM_TLV_UART_1_0_PATH), btfw_nvm_path,
                          sizeof(btfw_nvm_path)))
            nvm_file_path = NAPLES_NVM_TLV_UART_1_0_PATH;
        else
            nvm_file_path = btfw_nvm_path;
        // rome_ver = ROME_VER_3_2;	// SET to ROME 3.2 as the patch
        // downloading workflow is same as Rome 3.2 LOG_D(" set rome_ver to
        // ROME_VER_3_2\n");
        goto download;
    case ROME_VER_2_1:
        rampatch_file_path = ROME_RAMPATCH_TLV_2_0_1_PATH;
        nvm_file_path = ROME_NVM_TLV_2_0_1_PATH;
        goto download;
    case ROME_VER_3_0:
        rampatch_file_path = ROME_RAMPATCH_TLV_3_0_0_PATH;
        nvm_file_path = ROME_NVM_TLV_3_0_0_PATH;
        fw_su_info = ROME_3_1_FW_SU;
        fw_su_offset = ROME_3_1_FW_SW_OFFSET;
        goto download;
    case ROME_VER_3_2:
        if (get_btfw_path("btfw32.tlv",
                          btfw_rampatch_path, sizeof(btfw_rampatch_path)))
            rampatch_file_path = ROME_RAMPATCH_TLV_3_0_2_PATH;
        else
            rampatch_file_path = btfw_rampatch_path;

        if (get_btfw_path("btnv32.bin", btfw_nvm_path,
                          sizeof(btfw_nvm_path)))
            nvm_file_path = ROME_NVM_TLV_3_0_2_PATH;
        else
            nvm_file_path = btfw_nvm_path;

        fw_su_info = ROME_3_2_FW_SU;
        fw_su_offset = ROME_3_2_FW_SW_OFFSET;
        goto download;
    case TUFELLO_VER_1_0:
        rampatch_file_path = TF_RAMPATCH_TLV_1_0_0_PATH;
        nvm_file_path = TF_NVM_TLV_1_0_0_PATH;
        goto download;
    case HASTINGS_VER_2_0:
        rampatch_file_path = HASTINGS_RAMPATCH_TLV_UART_2_0_PATH;
        nvm_file_path = HASTINGS_NVM_TLV_UART_2_0_PATH;
        goto download;
    case TUFELLO_VER_1_1:
        rampatch_file_path = TF_RAMPATCH_TLV_1_0_1_PATH;
        nvm_file_path = TF_NVM_TLV_1_0_1_PATH;

download:
        /* Change baud rate 115.2 kbps to 3Mbps*/
        err = rome_set_baudrate_req(fd);
        if (err < 0)
        {
            LOG_E("%s: Baud rate change failed!", __FUNCTION__);
            goto error;
        }
        LOG_I("%s: Baud rate changed successfully ", __FUNCTION__);
        /* Donwload TLV files (rampatch, NVM) */
        err = rome_download_tlv_file(fd);
        if (err < 0)
        {
            LOG_E("%s: Download TLV file failed!", __FUNCTION__);
            goto error;
        }
        LOG_I("%s: Download TLV file successfully ", __FUNCTION__);

        /* Get SU FM label information */
        if ((err = rome_get_build_info_req(fd)) < 0)
        {
            LOG_I("%s: Fail to get Rome FW SU Build info (0x%x)", __FUNCTION__,
                  err);
            // Ignore the failure of ROME FW SU label information
            err = 0;
        }
        /* Disable internal LDO to use external LDO instead*/
        err = disable_internal_ldo(fd);

        /* Send HCI Reset */
        err = rome_hci_reset(fd);
        if (err < 0)
        {
            LOG_E("HCI Reset Failed !!");
            goto error;
        }

        LOG_I("HCI Reset is done\n");

        break;
    default:
        LOG_I("%s: Detected unknown SoC version: 0x%08x", __FUNCTION__,
              chipset_ver);
        err = -1;
        break;
    }

error:
    dnld_fd = -1;
    LOG_I("Exit %s : unified_hci = %d, wait_vsc_evt = %d, err = %d", __func__,
          unified_hci, wait_vsc_evt, err);
    return err;
}

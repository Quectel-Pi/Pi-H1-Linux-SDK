/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein
 * is confidential and proprietary to MediaTek Inc. and/or its licensors.
 * Without the prior written permission of MediaTek inc. and/or its licensors,
 * any reproduction, modification, use or disclosure of MediaTek Software,
 * and information contained herein, in whole or in part, shall be strictly prohibited.
 */
/* MediaTek Inc. (C) 2020. All rights reserved.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER ON
 * AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
 * NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
 * SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
 * SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
 * THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
 * THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
 * CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
 * SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
 * CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
 * AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
 * OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
 * MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek Software")
 * have been modified by MediaTek Inc. All revisions are subject to any receiver's
 * applicable license agreements with MediaTek Inc.
 */
#include "atcid_universal_ant_cmd.h"
#include "atcid.h"
#include "at_tok.h"
#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================ //
//define
// ============================================================ //
#define TAG    "[UNIVERSAL ANT] "

#define ULOGI(fmt, arg...)    ALOGI("\n[%s:%d] " fmt, __FUNCTION__, __LINE__, ##arg)
#define ULOGD(fmt, arg...)    ALOGD("\n[%s:%d] " fmt, __FUNCTION__, __LINE__, ##arg)
#define ULOGE(fmt, arg...)    ALOGE("\n[%s:%d] " fmt, __FUNCTION__, __LINE__, ##arg)

#define MAX_AT_RESPONSE 2048

// ============================================================ //
// Global variable
// ============================================================ //
typedef enum {
    ANT_RAT_UNKNOW = -1,
    ANT_RAT_C2K = 0,
    ANT_RAT_TDSCDMA,
    ANT_RAT_GSM,
    ANT_RAT_WCDMA,
    ANT_RAT_LTE,
    ANT_RAT_NR
} uant_rat;

static const char *RatToString(uant_rat rat) {
    switch(rat) {
        case ANT_RAT_NR : return "RAT_NR";
        case ANT_RAT_LTE : return "RAT_LTE";
        case ANT_RAT_WCDMA: return "RAT_WCDMA";
        case ANT_RAT_TDSCDMA: return "RAT_TDSCDMA";
        case ANT_RAT_GSM: return "RAT_GSM";
        case ANT_RAT_C2K: return "RAT_C2K";
        default: return "<unknown Rat>";
    }
}



bool isReceive = false;
int lte_rssi0_dbm = 0;
int lte_rssi1_dbm = 0;
int lte_rssi2_dbm = 0;
int lte_rssi3_dbm = 0;

void sendCommand(char* cmd, char* response)
{
    return;
}


static int lte_ant_test(int band, int channel, char *response) {
    return 0;
}

static int nr_ant_test(int band, int channel, char *response) {
    //Received RSSI
    int err = 0;
    int rssi0_dbm = 0;
    int rssi1_dbm = 0;
    int rssi2_dbm = 0;
    int rssi3_dbm = 0;
    // response parse
    char resp[MAX_AT_RESPONSE];
    char* temp = NULL;
    char *skipstr = NULL;
    // command
    char str[50]; memset(str, 0, sizeof(str));

    ULOGD(TAG "Start\n");

    /*Set rat mode */
    memset(resp, 0, sizeof(resp));
    sendCommand("AT+ERAT=15", resp);
    if (strstr(resp, "OK")  == NULL) goto error;
    usleep(300000);

    /* To start SIM*/
    memset(resp, 0, sizeof(resp));
    sendCommand("AT+CFUN=4", resp);
    if (strstr(resp, "OK")  == NULL) goto error;
    usleep(300000);

    /* To start test RSSI */
    err = sprintf(str, "AT+EGMC=1,\"NrRssi\",%d,%d", band, channel);
    if (err < 0) goto error;
    memset(resp, 0, sizeof(resp));
    sendCommand(str, resp);

    if (strstr(resp, "+EGMC")  == NULL) goto error;
    temp = resp;
    at_tok_start(&temp);
    //act
    if (0 != at_tok_nextstr(&temp, &skipstr)) goto error;

    //parse out and assigned to rssi0_dbm
    if (0 != at_tok_nextint(&temp, &rssi0_dbm)) goto error;
    //parse out and assigned to rssi1_dbm
    if (0 != at_tok_nextint(&temp, &rssi1_dbm)) goto error;
    //parse out and assigned to rssi2_dbm
    if (0 != at_tok_nextint(&temp, &rssi2_dbm)) goto error;
    //parse out and assigned to rssi3_dbm
    if (0 != at_tok_nextint(&temp, &rssi3_dbm)) goto error;

    ULOGD(TAG "rssi0_dbm:%d rssi1_dbm:%d rssi2_dbm:%d rssi3_dbm:%d\n", rssi0_dbm, rssi1_dbm, rssi2_dbm, rssi3_dbm);
    if(rssi0_dbm == 0 ||rssi1_dbm == 0 || rssi2_dbm == 0 || rssi3_dbm == 0) goto error;

    /* Wait RESPONSE +EGMC: "NrRssi", <rssi0_dBm>, <rssi1_dBm>, <rssi2_dBm>, <rssi3_dBm> for verification */
    err = sprintf(response,"\r\n+UANT: %d,%d,%d,%d\r\n", rssi0_dbm, rssi1_dbm, rssi2_dbm, rssi3_dbm);
    if (err < 0) goto error;
    ULOGD(TAG "success exit\n");
    return 0;

error:
   ULOGD(TAG "fail exit\n");
    return -1;
}


ATRESPONSE_t universal_ant_test_handler(char* cmdline, ATOP_t at_op, char* response){
    int err = 0;
    int rat = ANT_RAT_UNKNOW, band = 0, channel = 0;
    ULOGD(TAG "cmdline:%s, at_op=%d\n", cmdline, at_op);

    if (cmdline!=NULL)
        err = cmdline[0] == '\0' ? -1 : 0;

    switch (at_op) {
        case AT_SET_OP:
            if (err > -1) {
                // cmdline:x,xx,xxx
                err = at_tok_nextint(&cmdline, &rat);
                if (err < 0 || rat == ANT_RAT_UNKNOW) goto error;

                err = at_tok_nextint(&cmdline, &band);
                if (err < 0 || band == 0) goto error;

                err = at_tok_nextint(&cmdline, &channel);
                if (err < 0 || channel == 0) goto error;

                ULOGD(TAG "rat:%s band:%d channel:%d\n", RatToString(rat), band, channel);

                if (rat == ANT_RAT_LTE) {
                    err = lte_ant_test(band, channel, response);
                } else if (rat == ANT_RAT_NR) {
                    err = nr_ant_test(band, channel, response);
                } else {
                    ULOGE(TAG "RAT not support: %d\n", rat);
                    goto error;
                }
                if (err < 0) {
                    ULOGE(TAG "Test fail!\n");
                    goto error;
                }
            } else {
               ULOGE(TAG "cmdline is null\n");
               goto error;
            }
            break;
        case AT_ACTION_OP:
        case AT_READ_OP:
        case AT_TEST_OP:
        default:
            ULOGD(TAG "unknown at_op :%d\n", at_op);
            break;
    }

    return AT_OK;

error:
    return AT_ERROR;
}

ATRESPONSE_t QL_AT_TEST_Handle(char* cmdline, ATOP_t opType, char* response) {
    return AT_OK;
}

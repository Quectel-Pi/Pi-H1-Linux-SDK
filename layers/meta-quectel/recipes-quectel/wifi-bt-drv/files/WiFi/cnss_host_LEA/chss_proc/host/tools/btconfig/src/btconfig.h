/*
 * Copyright (c) 2013,2016 The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *        * Redistributions of source code must retain the above copyright
 *          notice, this list of conditions and the following disclaimer.
 *        * Redistributions in binary form must reproduce the above copyright
 *          notice, this list of conditions and the following disclaimer in the
 *          documentation and/or other materials provided with the distribution.
 *        * Neither the name of The Linux Foundation nor
 *          the names of its contributors may be used to endorse or promote
 *          products derived from this software without specific prior written
 *          permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.    IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _BTCONFIG_H_
#define _BTCONFIG_H_
#define VERSION "2.14"
#define DUMP_DEBUG 1
#define DEBUG 1 // @@ boyi @@
#ifdef ANDROID
#define FW_PATH_AR "/etc/firmware/ar3k/"
#else
#define NPL_RAM_PATCH_UART_1_0_PATH "/lib/firmware/ar3k/rampatch_tlv_uart_npl_1.0.tlv"
#define NPL_NVM_PATCH_UART_1_0_PATH "/lib/firmware/ar3k/nvm_tlv_uart_npl_1.0.bin"
#define TUFF_RAM_PATCH_UART_1_0_PATH "/lib/firmware/ar3k/rampatch_tlv_3.2.tlv"
#define TUFF_NVM_PATCH_UART_1_0_PATH "/lib/firmware/ar3k/nvm_tlv_3.2.bin"
#define SOCKETNAME "/etc/bluetooth/btprop"
#define FW_PATH_AR "/lib/firmware/ar3k/"
#endif
#define BDADDR_FILE "ar3kbdaddr.pst"
#define PS_ASIC_FILE "PS_ASIC.pst"
#define PS_FPGA_FILE "PS_FPGA.pst"
#define PATCH_FILE "RamPatch.txt"
#define ROM_DEV_TYPE 0xdeadc0de
#define FPGA_ROM_VERSION 0x99999999

#define PS_RAM_SIZE 2048
#define TRUE 1
#define FALSE 0
#define LINE_SIZE_MAX 3000
#define PS_RESET 2

#define PS_WRITE 1
#define HCI_PS_CMD_HDR_LEN 7

#define PS_RESET_PARAM_LEN 6
#define HCI_MAX_CMD_SIZE 260
#define PS_RESET_CMD_LEN (HCI_PS_CMD_HDR_LEN + PS_RESET_PARAM_LEN)

#define PS_ID_MASK 0xFF
#define HCI_RESET 0x0C03
#define HCI_VENDOR_CMD_OGF 0x3F
#define HCI_PS_CMD_OCF 0x0B
#define HCI_CHG_BAUD_CMD_OCF 0x0C
#define HCI_CMD_OGF_HOST_CTL 0x03
#define HCI_CMD_OGF_INFO_PARAM 0x04

#define HCI_CMD_OCF_RESET 0x0003
#define HCI_CMD_OCF_READ_BD_ADDR 0x0009

#define WRITE_BDADDR_CMD_LEN 14
#define WRITE_BAUD_CMD_LEN 6
#define MAX_CMD_LEN WRITE_BDADDR_CMD_LEN

#define BD_ADDR_SIZE 6
#define BD_ADDR_PSTAG 1
#define PS_READ 0
#define PS_READ_RAW 3

#define WRITE_PATCH 8
#define ENABLE_PATCH 11

#define PS_WRITE_RAW 4
#define PS_GET_LENGTH 5
#define PS_SET_ACCESS_MODE 6
#define PS_SET_ACCESS_PRIORITY 7
#define PS_WRITE 1
#define PS_DYNMEM_OVERRIDE 10
#define PS_VERIFY_CRC 9
#define CHANGE_BDADDR 15
#define PS_COMMAND_HEADER 4
#define HCI_EVENT_SIZE 7
#define PS_RETRY_COUNT 3
#define RAM_PS_REGION (1 << 0)
#define RAM_PATCH_REGION (1 << 1)
#define RAM_DYN_MEM_REGION (1 << 2)
#define RAMPS_MAX_PS_DATA_PER_TAG 244
#define RAMPS_MAX_PS_TAGS_PER_FILE 50
#define PSTAG_RF_TEST_BLOCK_START (300)
#define PSTAG_SYSTEM_BLOCK_START (1)
#define BT_SOC_INIT_TOOL_START_MAGIC_WORD 0xB1B1
#define PSTAG_RF_PARAM_TABLE0 (PSTAG_RF_TEST_BLOCK_START + 0)
#define PSTAG_SYSCFG_PARAM_TABLE0 (PSTAG_SYSTEM_BLOCK_START + 18)
#define PATCH_MAX_LEN 20000
#define DYN_MEM_MAX_LEN 40
#define SKIP_BLANKS(str) \
    while (*str == ' ')  \
    str++
#define MAX_RADIO_CFG_TABLE_SIZE 1000
#define MAX_BYTE_LENGTH 244
#define DEBUG_EVENT_TYPE_PS 0x02
#define DEBUG_EVENT_TYPE_MEMBLK 0x03
#define HCI_EVENT_HEADER_SIZE 0x03
#define HI_MAGIC_NUMBER ((const unsigned short int)0xFADE)
#define HI_VERSION (0x0300) // Version 3.0
#define EEPROM_CONFIG 0x00020C00
#define FPGA_REGISTER 0x4FFC
#define MAX_EVENT_SIZE 260

// Vendor specific command OCF
#define OCF_PS 0x000B
#define OCF_MEMOP 0x0014
#define OGF_TEST_CMD 0x06
#define OCF_HOST_INTEREST 0x000A
#define OCF_CONT_TX_TESTER 0x0023
#define OCF_TX_TESTER 0x001B
#define OCF_SLEEP_MODE 0x0004
#define OCF_READ_MEMORY 0x0005
#define OCF_WRITE_MEMORY 0x0006
#define OCF_DISABLE_TX 0x002D
#define OCF_TEST_MODE_SEQN_TRACKING 0x0018
#define OCF_READ_VERSION 0x001E
#define OCF_AUDIO_CMD 0x0013
#define OCF_GET_BERTYPE 0x005C
#define OCF_RX_TESTER 0x005B

#define UCHAR unsigned char
#define BOOL unsigned short
#define UINT16 unsigned short int
#define UINT32 unsigned int
#define SINT16 signed short int
#define UINT8 unsigned char
#define SINT8 signed char

#if __BYTE_ORDER == __LITTLE_ENDIAN
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define htobs(d) (d)
#define htobl(d) (d)
#define btohs(d) (d)
#define btohl(d) (d)
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#elif __BYTE_ORDER == __BIG_ENDIAN
#define htobs(d) bswap_16(d)
#define htobl(d) bswap_32(d)
#define btohs(d) bswap_16(d)
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define btohl(d) bswap_32(d)
#else
#error "Unknown byte order"
#endif
#define MAX_TAGS 50
#define PS_HDR_LEN 4
#define HCI_VENDOR_CMD_OGF 0x3F
#define HCI_PS_CMD_OCF 0x0B
#define PS_EVENT_LEN 100
#define HCI_EV_SUCCESS 0x00

#define BT_PORT 2398

/* For ROME QCA61x4 chipset */
#define EVT_VENDOR (0xFF)
#define EVT_CMD_COMPLETE (0x0E)
#define HCI_COMMAND_PKT (0x01)
#define HCI_MAX_EVENT_SIZE (260)

#define MAX_SIZE_PER_TLV_SEGMENT (243)
#define EDL_PATCH_CMD_LEN (1)

#define EDL_PATCH_VER_REQ_CMD (0x19)
#define EDL_PATCH_TLV_REQ_CMD (0x1E)

#define EDL_CMD_REQ_RES_EVT (0x00)
#define EDL_PATCH_VER_RES_EVT (0x19)
#define EDL_APP_VER_RES_EVT (0x02)
#define EDL_TVL_DNLD_RES_EVT (0x04)
#define EDL_CMD_EXE_STATUS_EVT (0x00)
#define EDL_SET_BAUDRATE_RSP_EVT (0x92)
#define EDL_NVM_ACCESS_CODE_EVT (0x0B)

#define HCI_PATCH_CMD_OCF (0)

#define FW_PATH_ROME "/lib/firmware/ar3k"
#define FLOW_CTL (0x0001)
#define BAUDRATE_CHANGE_SUCCESS (0x01)
#define CC_MIN_SIZE (0x7)
#define HCI_CMD_SUCCESS (0x0)
#define HCI_CMD_IND (1)
#define PLEN (3)
#define HCI_COMMAND_HDR_SIZE 3
/* This header value in rampatch file decides event handling mechanism in the HOST */
#define ROME_SKIP_EVT_NONE 0x00
#define ROME_SKIP_EVT_VSE 0x01
#define ROME_SKIP_EVT_CC 0x02
#define ROME_SKIP_EVT_VSE_CC 0x03
/* VS Opcode */
#define HCI_PATCH_CMD_OCF (0)
#define EDL_SET_BAUDRATE_CMD_OCF (0x48)

/* VS Commands */
#define VSC_SET_BAUDRATE_REQ_LEN (1)
#define EDL_PATCH_CMD_LEN (1)
#define EDL_PATCH_CMD_REQ_LEN (1)
#define EDL_PATCH_DLD_REQ_CMD (0x01)
#define EDL_PATCH_RST_REQ_CMD (0x05)
#define EDL_PATCH_SET_REQ_CMD (0x16)
#define EDL_PATCH_ATCH_REQ_CMD (0x17)
#define EDL_PATCH_VER_REQ_CMD (0x19)
#define EDL_PATCH_TLV_REQ_CMD (0x1E)
#define VSC_DISABLE_IBS_LEN (0x04)

/* VS Event */
#define EDL_CMD_REQ_RES_EVT (0x00)
#define EDL_CMD_EXE_STATUS_EVT (0x00)
#define EDL_SET_BAUDRATE_RSP_EVT (0x92)
#define EDL_PATCH_VER_RES_EVT (0x19)
#define EDL_TVL_DNLD_RES_EVT (0x04)
#define EDL_APP_VER_RES_EVT (0x02)

#define cmd_opcode_pack(ogf, ocf) (uint16_t)((ocf & 0x03ff) | (ogf << 10))
#define PRINT_BUF_SIZE ((HCI_MAX_CMD_SIZE * 3) + 2)

enum
{
    NAPLES_PATCH_VER_0100 = 0x0100,
    ROME_PATCH_VER_0100 = 0x0100,
    ROME_PATCH_VER_0101 = 0x0101,
    ROME_PATCH_VER_0200 = 0x0200,
    ROME_PATCH_VER_0300 = 0x0300,
    ROME_PATCH_VER_0302 = 0x0302
};

enum
{
    ROME_SOC_ID_00 = 0x00000000,
    ROME_SOC_ID_11 = 0x00000011,
    ROME_SOC_ID_13 = 0x00000013,
    ROME_SOC_ID_22 = 0x00000022,
    ROME_SOC_ID_23 = 0x00000023,
    ROME_SOC_ID_44 = 0x00000044,
    NAPLES_SOC_ID_15 = 0x00000015,
};
enum
{
    ROME_VER_UNKNOWN = 0,
    ROME_VER_1_0 = ((ROME_PATCH_VER_0100 << 16) | ROME_SOC_ID_00),
    ROME_VER_1_1 = ((ROME_PATCH_VER_0101 << 16) | ROME_SOC_ID_00),
    ROME_VER_1_3 = ((ROME_PATCH_VER_0200 << 16) | ROME_SOC_ID_00),
    ROME_VER_2_1 = ((ROME_PATCH_VER_0200 << 16) | ROME_SOC_ID_11),
    ROME_VER_3_0 = ((ROME_PATCH_VER_0300 << 16) | ROME_SOC_ID_22),
    ROME_VER_3_2 = ((ROME_PATCH_VER_0302 << 16) | ROME_SOC_ID_44),
    TUFELLO_VER_1_0 = ((ROME_PATCH_VER_0300 << 16) | ROME_SOC_ID_13),
    TUFELLO_VER_1_1 = ((ROME_PATCH_VER_0302 << 16) | ROME_SOC_ID_23),
    NAPLES_VER_1_0 = ((NAPLES_PATCH_VER_0100 << 16) | NAPLES_SOC_ID_15),
};

enum qca_bardrate_type
{
    QCA_BAUDRATE_115200 = 0,
    QCA_BAUDRATE_57600,
    QCA_BAUDRATE_38400,
    QCA_BAUDRATE_19200,
    QCA_BAUDRATE_9600,
    QCA_BAUDRATE_230400,
    QCA_BAUDRATE_250000,
    QCA_BAUDRATE_460800,
    QCA_BAUDRATE_500000,
    QCA_BAUDRATE_720000,
    QCA_BAUDRATE_921600,
    QCA_BAUDRATE_1000000,
    QCA_BAUDRATE_1250000,
    QCA_BAUDRATE_2000000,
    QCA_BAUDRATE_3000000,
    QCA_BAUDRATE_4000000,
    QCA_BAUDRATE_1600000,
    QCA_BAUDRATE_3200000,
    QCA_BAUDRATE_3500000,
    QCA_BAUDRATE_AUTO = 0xFE,
    QCA_BAUDRATE_RESERVED
};
typedef struct
{
    unsigned short rom_version;
    unsigned short build_version;
} __attribute__((packed)) patch_version;

typedef struct
{
    unsigned int patch_id;
    patch_version patch_ver;
    unsigned int patch_base_addr;
    unsigned int patch_entry_addr;
    unsigned short patch_length;
    int patch_crc;
    unsigned short patch_ctrl;
} __attribute__((packed)) patch_info;

typedef struct
{
    unsigned int tlv_data_len;
    unsigned int tlv_patch_data_len;
    unsigned char sign_ver;
    unsigned char sign_algorithm;
    unsigned char dwnd_cfg;
    unsigned char reserved1;
    unsigned short prod_id;
    unsigned short build_ver;
    unsigned short patch_ver;
    unsigned short reserved2;
    unsigned int patch_entry_addr;
} __attribute__((packed)) tlv_patch_hdr;

typedef struct
{
    unsigned short tag_id;
    unsigned short tag_len;
    unsigned int tag_ptr;
    unsigned int tag_ex_flag;
} __attribute__((packed)) tlv_nvm_hdr;

typedef struct
{
    unsigned char tlv_type;
    unsigned char tlv_length1;
    unsigned char tlv_length2;
    unsigned char tlv_length3;
    union
    {
        tlv_patch_hdr patch;
        tlv_nvm_hdr nvm;
    } tlv;
} __attribute__((packed)) tlv_patch_info;
enum qca_tlv_type
{
    TLV_TYPE_PATCH = 1,
    TLV_TYPE_NVM
};
struct patch_data
{
    int8_t type;
    int64_t len;
    uint8_t *data;
};

typedef struct tPsTagEntry
{
    int TagId;
    UCHAR TagLen;
    UCHAR TagData[RAMPS_MAX_PS_DATA_PER_TAG];
} tPsTagEntry, *tpPsTagEntry;

typedef struct tRamPatch
{
    int Len;
    UCHAR Data[PATCH_MAX_LEN];
} tRamPatch, *ptRamPatch;

typedef struct tRamDynMemOverride
{
    int Len;
    UCHAR Data[DYN_MEM_MAX_LEN];
} tRamDynMemOverride, *ptRamDynMemOverride;

tPsTagEntry PsTagEntry[RAMPS_MAX_PS_TAGS_PER_FILE];
tRamPatch RamPatch[50];
tRamDynMemOverride RamDynMemOverride;

enum MB_FILEFORMAT
{
    MB_FILEFORMAT_PS,
    MB_FILEFORMAT_PATCH,
    MB_FILEFORMAT_DY,
};
enum RamPsSection
{
    RAM_PS_SECTION,
    RAM_PATCH_SECTION,
    RAM_DYN_MEM_SECTION
};

enum eType
{
    eHex,
    edecimal,
};

struct ST_PS_DATA_FORMAT
{
    enum eType eDataType;
    BOOL bIsArray;
};
#define CONV_DEC_DIGIT_TO_VALUE(c) ((c) - '0')
#define IS_HEX(c) (IS_BETWEEN((c), '0', '9') || IS_BETWEEN((c), 'a', 'f') || IS_BETWEEN((c), 'A', 'F'))
#define IS_BETWEEN(x, lower, upper) (((lower) <= (x)) && ((x) <= (upper)))
#define IS_DIGIT(c) (IS_BETWEEN((c), '0', '9'))
#define CONV_HEX_DIGIT_TO_VALUE(c) (IS_DIGIT(c) ? ((c) - '0') : (IS_BETWEEN((c), 'A', 'Z') ? ((c) - 'A' + 10) : ((c) - 'a' + 10)))
#define BYTES_OF_PS_DATA_PER_LINE 16
struct ST_READ_STATUS
{
    unsigned uTagID;
    unsigned uSection;
    unsigned uLineCount;
    unsigned uCharCount;
    unsigned uByteCount;
};

// DUT MODE related
#define MC_BCAM_COMPARE_ADDRESS 0x00008080
#define HCI_3_PATCH_SPACE_LENGTH_1 (0x80)
#define HCI_3_PATCH_SPACE_LENGTH_2 (0x279C)
#define MEM_BLK_DATA_MAX (244)
#define MC_BCAM_VALID_ADDRESS 0x00008100

// Audio stat

typedef struct tAudio_Stat
{
    UINT16 RxSilenceInsert;
    UINT16 RxAirPktDump;
    UINT16 RxCmplt;
    UINT16 TxCmplt;
    UINT16 MaxPLCGenInterval;
    UINT16 RxAirPktStatusGood;
    UINT16 RxAirPktStatusError;
    UINT16 RxAirPktStatusLost;
    UINT16 RxAirPktStatusPartial;
    SINT16 SampleMin;
    SINT16 SampleMax;
    UINT16 SampleCounter;
    UINT16 SampleStatEnable;
} tAudioStat;

// DMA stats

typedef struct tBRM_Stats
{
    // DMA Stats
    UINT32 DmaIntrs;

    // Voice Stats
    UINT16 VoiceTxDmaIntrs;
    UINT16 VoiceTxErrorIntrs;
    UINT16 VoiceTxDmaErrorIntrs;
    UINT16 VoiceTxPktAvail;
    UINT16 VoiceTxPktDumped;
    UINT16 VoiceTxDmaSilenceInserts;

    UINT16 VoiceRxDmaIntrs;
    UINT16 VoiceRxErrorIntrs;
    UINT16 VoiceRxGoodPkts;
    UINT16 VoiceRxErrCrc;
    UINT16 VoiceRxErrUnderOverFlow;
    UINT16 VoiceRxPktDumped;

    UINT16 VoiceTxReapOnError;
    UINT16 VoiceRxReapOnError;
    UINT16 VoiceSchedulingError;
    UINT16 SchedOnVoiceError;

    UINT16 Temp1;
    UINT16 Temp2;

    // Control Stats
    UINT16 ErrWrongLlid;
    UINT16 ErrL2CapLen;
    UINT16 ErrUnderOverFlow;
    UINT16 RxBufferDumped;
    UINT16 ErrWrongLmpPktType;
    UINT16 ErrWrongL2CapPktType;
    UINT16 HecFailPkts;
    UINT16 IgnoredPkts;
    UINT16 CrcFailPkts;
    UINT16 HwErrRxOverflow;

    UINT16 CtrlErrNoLmpBufs;

    // ACL Stats
    UINT16 DataTxBuffers;
    UINT16 DataRxBuffers;
    UINT16 DataRxErrCrc;
    UINT16 DataRxPktDumped;
    UINT16 LmpTxBuffers;
    UINT16 LmpRxBuffers;
    UINT16 ForceOverQosJob;

    // Sniff Stats
    UINT16 SniffSchedulingError;
    UINT16 SniffIntervalNoCorr;

    // Test Mode Stats
    UINT16 TestModeDroppedTxPkts;
    UINT16 TestModeDroppedLmps;

    // Error Stats
    UINT16 TimePassedIntrs;
    UINT16 NoCommandIntrs;

} tBRM_Stats;

typedef struct tSYSUTIL_ChipId
{
    char *pName;
    UINT32 HwRev;
} tSYSUTIL_ChipId;

typedef struct tSU_RevInfo
{
    tSYSUTIL_ChipId *pChipId;
    tSYSUTIL_ChipId *pChipRadioId;
    UINT32 ChipRadioId;
    UINT32 SubRadioId;
    UINT32 RomVersion;
    UINT32 RomBuildNumber;
    UINT32 BuildVersion;
    UINT32 BuildNumber;
    UINT16 RadioFormat;
    UINT16 RadioContent;
    UINT16 SysCfgFormat;
    UINT16 SysCfgContent;
    UINT8 ProductId;
} tSU_RevInfo;

typedef struct
{
    UINT8 b[6];
} __attribute__((packed)) bdaddr_t;



#define HCI_COMMAND_HEADER_SIZE 3
#define HCI_OPCODE_PACK(ogf, ocf) (UINT16)((ocf & 0x03ff) | (ogf << 10))

#endif

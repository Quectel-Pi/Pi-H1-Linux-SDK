/*
 * Copyright (c) 2015-17 Qualcomm Atheros, Inc.
 *
 * All Rights Reserved.
 * Qualcomm Atheros Confidential and Proprietary.
 */

/*
 * Copyright (c) 2014-2015 The Linux Foundation. All rights reserved.
 *
 * Previously licensed under the ISC license by Qualcomm Atheros, Inc.
 *
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * This file was originally distributed by Qualcomm Atheros, Inc.
 * under proprietary terms before Copyright ownership was assigned
 * to the Linux Foundation.
 */

#ifndef _CLD_DIAG_PARSER_H
#define _CLD_DIAG_PARSER_H

#define FEATURE_LOG_EXPOSED_HEADER

#include <stdio.h>
#include <stdint.h>
#ifndef __KERNEL__
#include <sys/socket.h>
#include <netinet/in.h>
#endif // __KERNEL__

#ifdef USE_GLIB
#include <glib.h>
#define strlcat g_strlcat
#define strlcpy g_strlcpy
#endif
#include <inttypes.h>
#include <pthread.h>

#ifdef CONFIG_CLD80211_LIB
#include "cld80211_lib.h"
#endif

#define DIAG_KEEP_ORIG             0   /* Macro to decide if keep the original code which napier X86 not need */

#define INIT_WITH_SLEEP            10
#define CLD_NETLINK_USER           17

#define LOGFILE_FLAG               0x01
#define CONSOLE_FLAG               0x02
#define QXDM_FLAG                  0x04
#define SILENT_FLAG                0x08
#define DEBUG_FLAG                 0x10
#define BUFFER_SIZE_FLAG           0x20
#define QXDM_SYNC_FLAG             0x40
#define LOGFILE_QXDM_FLAG          0x80
#define LOG_BUFF_FLAG              0x100
#define DECODED_LOG_FLAG           0x200

#define HDRLEN                     16

#define RECLEN (HDRLEN + ATH6KL_FWLOG_PAYLOAD_SIZE)

#define DEFAULT_LOG_BUFFER_LIMIT             (32 * 1024)
#define DEFAULT_BUFFER_SIZE_MAX  (2 * 1024 * 1024)
#define EACH_BUF_SIZE            (4 * 1024)
#define WRITE_TO_FILE_DISABLED   0
#define WRITE_TO_INTERNAL_SDCARD 1
#define WRITE_TO_EXTERNAL_SDCARD 2
#define FLAG_VALUE_OFFSET        3
#define MAX_RETRY_COUNT          40
#define MAX_FILE_SIZE            ((30) * (1024) * (1024))
#define MAX_FILE_INDEX           10
#define MAX_FILENAME_SIZE        100
#define MAX_SIZE                 512
#define TIME_DATA_SIZE           100
#define PKT_BUF_SIZE             1024
#define i_isspace(ch)  (((ch) >= 0x09 && (ch) <= 0x0d) || (ch) == ' ')
#define HEXDUMP_PKT8(p, pkt) \
          snprintf(p, 25, "%02x %02x %02x %02x %02x %02x %02x %02x ", \
                          pkt[0], pkt[1], pkt[2], pkt[3], \
                          pkt[4], pkt[5], pkt[6], pkt[7]);
#define UNUSED(x)	(void)(x)

#define MAX_REQ_SIZE		(16 * 1024)
#define MAX_HDLC_BUF_SIZE	(MAX_REQ_SIZE * 2 + 3)
#define ESC_CHAR		0x7D
#define ESC_MASK		0x20

#define HDLC_INCOMPLETE		0
#define HDLC_COMPLETE		1

#define HDLC_FOOTER_LEN		3
#define CONTROL_CHAR		0x7E
#define CRC_16_L_SEED		0xFFFF

#define CRC_16_L_STEP(xx_crc, xx_c) \
	crc_ccitt_byte(xx_crc, xx_c)

/* Request for extended msg report */
#define DIAG_EXT_MSG_F		121
/* Send Optimized F3 messages */
#define DIAG_QSR_EXT_MSG_TERSE_F	146

#define DIAG_LOG_F          16
#define DIAG_EVENT_REPORT_F  96

#define DIAG_CMD_MSG_CONFIG	0x7D
#define DIAG_CMD_OP_SET_MSG_MASK	4

#define DIAG_CMD_EVENT_TOGGLE    0x60
#define DIAG_CMD_SET_EVENT_MASK  0x82

#define MSG_SSID_WLAN       4500
#define MSG_SSID_WLAN_LAST  4583
#define MASK_LOW_LEVEL      0x1F
#define DIAG_MAX_REQ_SIZE	(16 * 1024)

#define DIAG_MAX_HDLC_BUF_SIZE	((DIAG_MAX_REQ_SIZE * 2) + 3)



#define DB_PATH		"msg_hash.txt"
#define DB_META_LINE		21

#define NETLINK_CUSTOM_FW	27


extern int max_file_size;
extern int optionflag;

static inline unsigned int get_32(const unsigned char *pos)
{
	return pos[0] | (pos[1] << 8) | (pos[2] << 16) | (pos[3] << 24);
}

/* General purpose MACROS to handle the WNI Netlink msgs */
#define ANI_NL_MASK        3

/*
 * The following enum defines the target kernel module for which the netlink
 * message is intended for. Each kernel module along with its counterpart
 * in the user space, will then define a set of messages they recognize.
 * Each of this message will have an header of type tAniHdr define below.
 * Each Netlink message to/from a kernel module will contain only one
 * message which is preceded by a tAniHdr.
 *
 *         +------------+-------+-------+----------+
 *         |Netlink hdr | Align |tAniHdr| msg body |
 *         +------------+-------+-------|----------+
 */
#define ANI_NL_MSG_BASE     0x10    /* Some arbitrary base */
typedef enum eAniNlModuleTypes {
	ANI_NL_MSG_NETSIM = ANI_NL_MSG_BASE,// NetSim Messages (to the server)
	ANI_NL_MSG_PUMAC,       // Messages for/from the Upper MAC driver
	ANI_NL_MSG_WNS,         // Messages for the Wireless Networking
	//  Services module(s)
	ANI_NL_MSG_MACSW,       // Messages from MAC
	ANI_NL_MSG_ES,          // Messages from ES
	ANI_NL_MSG_WSM,         // Message from the WSM in user space
	ANI_NL_MSG_DVT,         // Message from the DVT application
	ANI_NL_MSG_PTT,         // Message from the PTT application
	ANI_NL_MSG_MAC_CLONE,     //Message from the Mac clone App
	ANI_NL_MSG_LOG = ANI_NL_MSG_BASE + 0x0C, // Message for WLAN logging
	ANI_NL_MSG_MAX
} tAniNlModTypes;

typedef enum _boolean {
	FALSE,
	TRUE
} boolean;

struct dbglog_slot {
	unsigned int diag_type;
	unsigned int timestamp;
	unsigned int length;
	unsigned int dropped;
	/* max ATH6KL_FWLOG_PAYLOAD_SIZE bytes */
	uint8_t payload[0];
};


enum cnss_diag_type {
	DIAG_TYPE_FW_EVENT,           /* send fw event- to diag */
	DIAG_TYPE_FW_LOG,             /* send log event- to diag */
	DIAG_TYPE_FW_DEBUG_MSG,       /* send dbg message- to diag */
	DIAG_TYPE_INIT_REQ,           /* cnss_diag initialization- from diag */
	DIAG_TYPE_FW_MSG,             /* fw msg command-to diag */
	DIAG_TYPE_HOST_MSG,           /* host command-to diag */
	DIAG_TYPE_CRASH_INJECT,       /*crash inject-from diag */
	DIAG_TYPE_DBG_LEVEL,          /* DBG LEVEL-from diag */
};


enum fileType {
	HOST_LOG_FILE,
	FW_LOG_FILE,
	HOST_QXDM_LOG_FILE,
	FW_QXDM_LOG_FILE,
	BUFFER_HOST_FILE,
	BUFFER_FW_FILE,
	LOG_FILE_MAX
};

struct cnss_log_file_data {
	FILE *fp;
	int index;
	char *buf;
	char *buf_ptr;
	int32_t free_buf_mem;
	int wrap_cnt;
	char name[MAX_FILENAME_SIZE];
	int num_buffers_occupied;
	pthread_mutex_t buff_lock;
};

/* WLAN device Type for log timestamp format identification */
enum {
	CNSS_DIAG_WLAN_DEV_UNDEF = 0,
	CNSS_DIAG_WLAN_ROM_DEV,
	CNSS_DIAG_WLAN_TUF_DEV,
	CNSS_DIAG_WLAN_HEL_DEV,
	CNSS_DIAG_WLAN_NAP_DEV,
	CNSS_DIAG_WLAN_DEV_MAX
};

//All Netlink messages must contain this header
typedef struct sAniHdr {
	unsigned short type;
	unsigned short length;
} tAniHdr, tAniMsgHdr;

/*
 * This msg hdr will always follow tAniHdr in all the messages exchanged
 * between the Applications in userspace the Pseudo Driver, in either
 * direction.
 */
typedef struct sAniNlMsg {
	struct  nlmsghdr nlh;   // Netlink Header
	int radio;          // unit number of the radio
	tAniHdr wmsg;       // Airgo Message Header
} tAniNlHdr;

typedef struct sAniCLDMsg {
	int radio;          /* unit number of the radio */
	tAniHdr wmsg;       /* Airgo Message Header */
} tAniCLDHdr;

typedef struct sAniAppRegReq {
	tAniNlModTypes type;    /* The module id that the application is
			           registering for */
	int pid;            /* Pid returned in the nl_sockaddr structure
			    in the call getsockbyname after the
			    application opens and binds a netlink
			    socket */
} tAniNlAppRegReq;

typedef struct buffer_pointer {
	unsigned char *start;		/* Start location in buffer */
	unsigned char *end;		/* End Location in buffer   */
	struct buffer_pointer *next;	/* Locating to Next Buffer  */
} t_buffer;


enum diag_send_state_enum_type {
	DIAG_STATE_START,
	DIAG_STATE_BUSY,
	DIAG_STATE_CRC1,
	DIAG_STATE_CRC2,
	DIAG_STATE_TERM,
	DIAG_STATE_COMPLETE
};

struct diag_msg_build_mask_t {
	uint8_t cmd_code;
	uint8_t sub_cmd;
	uint16_t ssid_first;
	uint16_t ssid_last;
	uint8_t status;
	uint8_t padding;
} __attribute__ ((__packed__));

struct diag_event_mask_config_t {
        uint8_t cmd_code;
        uint8_t status;
        uint16_t padding;
        uint16_t num_bits;
} __attribute__ ((__packed__));

struct diag_event_toggle_config_t {
       uint8_t cmd_code;
       uint8_t enable;
} __attribute__ ((__packed__));


struct diag_log_config_req_t {
        uint8_t cmd_code;
        uint8_t padding[3];
        uint32_t sub_cmd;
        uint32_t equip_id;
        uint32_t num_items;
} __attribute__ ((__packed__));

/******** diag log related start *************/
#define DIAG_CMD_OP_LOG_DISABLE		0
#define DIAG_CMD_OP_GET_LOG_RANGE	1
#define DIAG_CMD_OP_SET_LOG_MASK	3
#define DIAG_CMD_OP_GET_LOG_MASK	4

#define DIAG_CMD_LOG_CONFIG	0x73

typedef enum {
  LOG_EQUIP_ID_OEM     = 0, /* 3rd party OEM (licensee) use */
  LOG_EQUIP_ID_1X      = 1, /* Traditional 1X line of products */
  LOG_EQUIP_ID_RSVD2   = 2,
  LOG_EQUIP_ID_RSVD3   = 3,
  LOG_EQUIP_ID_WCDMA   = 4,
  LOG_EQUIP_ID_GSM     = 5,
  LOG_EQUIP_ID_LBS     = 6,
  LOG_EQUIP_ID_UMTS    = 7,
  LOG_EQUIP_ID_TDMA    = 8,
  LOG_EQUIP_ID_BOA     = 9,
  LOG_EQUIP_ID_DTV     = 10,
  LOG_EQUIP_ID_APPS    = 11,
  LOG_EQUIP_ID_DSP     = 12,
  LOG_EQUIP_ID_TDSCDMA = 13,

  LOG_EQUIP_ID_LAST_DEFAULT = LOG_EQUIP_ID_TDSCDMA

} log_equip_id_enum_type;

/* refer to fw code in file log_codes.h */
#define LOG_1X_BASE_C ((uint16_t) 0x1000) /* for eqiupment ID 0x1*/
#define LOG_1X_LAST_C ((0xA11) + LOG_1X_BASE_C)
#define LOG_LAST_C   (LOG_1X_LAST_C & 0xFFF)
#define LOG_EQUIP_ID_1_LAST_CODE LOG_LAST_C
#define LOG_GET_ITEM_NUM(xx_code) (((uint16_t) (xx_code)) & 0x0FFF)

/******** diag log related end *************/

struct diag_hdlc_decode_type {
	uint8_t *src_ptr;
	unsigned int src_idx;
	unsigned int src_size;
	uint8_t *dest_ptr;
	unsigned int dest_idx;
	unsigned int dest_size;
	int escaping;
};

struct diag_send_desc_type {
	const void *pkt;
	const void *last;	/* Address of last byte to send. */
	enum diag_send_state_enum_type state;
	unsigned char terminate;	/* True if this fragment
					   terminates the packet */
};

struct diag_hdlc_dest_type {
	void *dest;
	void *dest_last;
	/* Below: internal use only */
	uint16_t crc;
};


struct s_hdlc_buf {
	unsigned char *buf;
	unsigned used_len;
};

typedef struct {
	unsigned short line;			/*!< Line number in source file */
	unsigned short ss_id;			/*!< Subsystem ID               */
	unsigned ss_mask;			/*!< Subsystem Mask             */
} msg_desc_type;

typedef struct {
	unsigned char cmd_code;		/*!< Command code */
	unsigned char ts_type;		/*!< Time stamp type */
	unsigned char num_args;		/*!< Number of arguments in message */
	unsigned char drop_cnt;		/*!< number of messages dropped since last successful message */
	unsigned ts_lo; /* Time stamp */
	unsigned ts_hi;
} msg_hdr_type;

typedef struct	{
	msg_hdr_type hdr;	/*!< Header */
	msg_desc_type desc;	/*!< line number, SSID, mask */
	unsigned args[1];		/*!< Array of long args, specified by 'hdr.num_args' */
				/*!< followed by NULL terminated format and file strings */
} msg_ext_type;

typedef struct
{
	msg_hdr_type hdr;
	msg_desc_type desc;
	uint32_t msg_hash;
	uint32_t args[1];
} msg_qsr_type;

typedef struct {
	struct sockaddr_nl src_addr;
	struct sockaddr_nl dest_addr;
	struct nlmsghdr *nlh;
	struct iovec iov;
	int sock_fd;
	struct msghdr msg;

	boolean enable;
} tSock;

/* QXDM / APEX Container Log Packet Type */
enum {
	LOG_TYPE_F3_STRING = 0,
	LOG_TYPE_EVENT = 1,
	LOG_TYPE_LOG_PACKET = 2,
};

/* QXDM / APEX Container Log Packet source */
enum {
	LOG_SOURCE_HOST = 0,
	LOG_SOURCE_FW = 1,
};

/* QXDM / APEX Container Log Packet - CNSS WLAN header */
struct cnss_log_pkt_hdr {
	//WLAN Diag buf hdr
	uint8_t version;        // WLAN log packet Version v1 0x1
	uint8_t source;         // 0 - Host 1 - FW
        uint8_t type;           // 0- F3 String 1- Events 2-Container Log Packets.
	uint16_t num_ent;       // Number of log_entries in given
	uint64_t time_stamp;    // Log ref time for Diag. CDMA (MODEM)
} __attribute__ ((__packed__));

/* QXDM / APEX Container Log Packet - CNSS WLAN log entry header */
struct cnss_log_ent {
        int32_t delta_time;     // Time difference of log generation.
        uint16_t txt_len;       // Length of actual log string
        uint16_t lvl;           // MSG_LEGACY_LOW / MED / HIGH / ERROR / FATAL level
        uint16_t ss_id;         // WLAN internal Module ID
        char txt[0];            // Ascii string of text log. Null term.
} __attribute__ ((__packed__));

/* QXDM / APEX Container Log Packet buffer container */
struct cnss_log_pkt_buf {
        struct cnss_log_pkt_hdr hdr;
        struct cnss_log_ent log[0]; // log entry stream placeholder
} __attribute__ ((__packed__));

/* QXDM / APEX Container Log Packet - Direct logging buffer */
struct cnss_diag_log_buf {
#if	DIAG_KEEP_ORIG
	log_hdr_type diag_log_hdr; //Log alloc function hdr
#endif
	struct cnss_log_pkt_buf log_pkt_buf;
} __attribute__ ((__packed__));

/* QXDM / APEX Container Log Packet - QMDL2 file logging buffer */
struct cnss_diag_std_file_buf_hdr_log_pkt_hdr {
	uint16_t log_code;      // CNSS_DIAG_LOG_ID
	uint64_t ts;            // dont care
} __attribute__ ((__packed__));

/* QXDM / APEX Container Log Packet - QMDL2 file logging buffer header */
struct cnss_diag_std_file_buf_hdr {
	uint8_t cmd_code;       // 0x10
	uint8_t more;           // 0
	uint16_t len1;          // Length of Container Log Packet + 10
	uint16_t len2;          // Length of container log Packet + 10
	struct cnss_diag_std_file_buf_hdr_log_pkt_hdr log_pkt_hdr;
} __attribute__ ((__packed__));

/* QXDM / APEX Container Log Packet - QMDL2 file logging buffer */
struct cnss_diag_file_buf {
	uint8_t start_marker;   // 0x7E
	uint8_t version;        // WLAN log packet Version v1 0x1
	uint16_t chunk_length;  // Length of Container Log Packet + Standard Log Packet Header (16)
	struct cnss_diag_std_file_buf_hdr std_hdr;
	uint8_t log_packet[0];  // log_packet placeholder
	uint8_t end_marker;     // 0x7E End marker placeholder. Will be at the end of buf
} __attribute__ ((__packed__));

/* QXDM / APEX Container Log Packet - QMDL2 file header */
struct qmdl_file_hdr {
	uint32_t hdr_len;       // Length of the file header (this struct)
	uint8_t version;        // Version of the file header (this struct) === 1
	uint8_t data_type;      // 0 - Non-HDLC encoded; 1 - HDLC encoded
	uint32_t guid_list_count; // Number of entries in guidList
	uint8_t guid_list[0];   // QShrink4 GUID requirements;
} __attribute__ ((__packed__));

// Allocated by DIAG Tools team
#define CNSS_DIAG_LOG_ID 0x1A00
#define QTIMER_FREQ_KHZ 19200
//4K driver + 1K log pkt headers
#define CNSS_DIAG_LOG_BUF_MAX (5*1024)

static inline unsigned int aniNlAlign(unsigned int len)
{
	return ((len + ANI_NL_MASK) & ~ANI_NL_MASK);
}

/*
 * Determines the aligned length of the WNI MSG including the hdr
 * for a given payload of length 'len'.
 */
static inline unsigned int aniNlLen(unsigned int len)
{
	return  (aniNlAlign(sizeof(tAniHdr)) + len);
}

/* KERNEL DEFS END */

#ifdef CONFIG_ANDROID_LOG
#include <android/log.h>

#define FWDEBUG_LOG_NAME        "CNSS"
#define FWDEBUG_NAME            "CNSS_DEBUG"
#define android_printf(...) \
    __android_log_print(ANDROID_LOG_INFO, FWDEBUG_LOG_NAME, __VA_ARGS__);

#define debug_printf(...) do {                         \
	if (optionflag & DEBUG_FLAG)                  \
		__android_log_print(ANDROID_LOG_INFO,  \
			FWDEBUG_NAME, __VA_ARGS__);    \
} while(0)
#else
#define android_printf printf
#define debug_printf   printf
#endif

#define WLAN_NL_MSG_CNSS_DIAG   27 /* Msg type between user space/wlan driver */
#define WLAN_NL_MSG_CNSS_HOST_MSG    28
#define WLAN_NL_CNSS_FW_MSG    29
#define WLAN_NL_MSG_CNSS_HOST_EVENT_LOG    17
#define CNSS_WLAN_DIAG          0x07
#define CNSS_WLAN_SSR_TYPE      0x01
#define CNSS_WLAN_LEVEL_TYPE    0x02
/* NL messgage Carries actual Logs from Driver */
#define ANI_NL_MSG_LOG_HOST_MSG_TYPE 89
#define ANI_NL_MSG_LOG_MGMT_MSG_TYPE 0x5B
#define ANI_NL_MSG_LOG_FW_MSG_TYPE 0x5C
#define ANI_NL_MSG_LOG_HOST_EVENT_LOG_TYPE 0x5050
/* NL message Registration Req/Response to and from Driver */
#define ANI_NL_MSG_LOG_REG_TYPE  0x0001
#define MAX_MSG_SIZE 50000
/* PKT SIZE for intf */
#define MAX_PKT_SIZE 8192

/* SPECIAL DIAG HANDLING */
#define DIAG_WLAN_MODULE_STA_PWRSAVE  34292
#define DIAG_WLAN_MODULE_WAL          42996
#define DIAG_NAN_MODULE_ID            56820
#define DIAG_WLAN_MODULE_IBSS_PWRSAVE 57332

#define RESTART_LEVEL     \
    "echo related > /sys/bus/msm_subsys/devices/subsys%d/restart_level"
#define DB_1_FILE_1_PATH        "/firmware/image/Data.msc"
#define DB_1_FILE_2_PATH        "/vendor/firmware/Data.msc"
#define BUF_SIZ  256
#define NUM_OF_DRIVERS  2
#define DB_2_FILE_1_PATH        "/lib/firmware/qca9377/Data.msc"

#define WLAN_LOG_TO_DIAG(xx_ss_id, xx_ss_mask, xx_fmt) \
do { \
	if (xx_ss_mask & (MSG_BUILD_MASK_ ## xx_ss_id)) { \
		msg_const_type xx_msg = { \
		{__LINE__, (xx_ss_id), (xx_ss_mask)}, (NULL), msg_file}; \
		xx_msg.fmt = xx_fmt; \
		msg_send (&xx_msg); \
	} \
} while  (0); \

#ifndef ARRAY_LENGTH
#define ARRAY_LENGTH(a)         (sizeof(a) / sizeof((a)[0]))
#endif

#if DIAG_KEEP_ORIG
void *cnss_wlan_handle(void *req_pkt, uint16_t pkt_len);

static const diagpkt_user_table_entry_type cnss_wlan_tbl[] =
{ /* susbsys_cmd_code lo = 7 , susbsys_cmd_code hi = 7, call back function */
	{CNSS_WLAN_DIAG, CNSS_WLAN_DIAG,cnss_wlan_handle},
};
#endif

int32_t parser_init();

int32_t
dbglog_parse_debug_logs(u_int8_t *datap, u_int16_t len, u_int16_t dropped, uint32_t radio_id);

void
diag_initialize(int sock_fd, uint32_t optionflag);

void
process_diaghost_msg(uint8_t *datap, uint16_t len);

uint32_t
process_diagfw_msg(uint8_t *datap, uint16_t len, uint32_t optionflag,
		int32_t version, int sock_fd, uint32_t radio_id);

int
diag_msg_handler(uint32_t id, char *payload,  uint16_t vdevid, uint64_t timestamp, uint32_t radio_id);

int
cnssdiag_register_kernel_logging(int sock_fd, struct nlmsghdr *nlh);

void process_cnss_host_message(tAniCLDHdr *wnl, int32_t optionflag);
void process_cnss_fw_message(tAniCLDHdr *wnl, int32_t optionflag);

void process_pronto_firmware_logs(tAniNlHdr *wnl, int32_t optionflag);
void buffer_fw_logs_log_pkt(char *fw_log, boolean force);
void cnss_write_buf_logs(int log_len, char *log, enum fileType type);
void process_cnss_host_diag_events_log(char *pData, int32_t optionflag);
void backup_file(enum fileType);
void readDir(const char *dirName , enum fileType type);
void cnss_open_log_file(int max_size_reached, enum fileType type);
void printLocalTime(FILE *fp, const char *prefix, const char *suffix);
void appendLocalTime(char* tmbuf);
int diag_local_enable_debug_msg(void);
int diag_local_enable_event(void);
int diag_local_event_toggle(uint8_t value);
int diag_local_enable_log(void);
int diag_local_disable_log(void);

#if !DIAG_KEEP_ORIG
#define log_commit(_x1) /* no op */
#define log_alloc(_x1, _x2)  NULL/* no op */
#endif


#endif


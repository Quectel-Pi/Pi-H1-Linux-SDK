/* 
Copyright (c) 2013 Qualcomm Atheros, Inc.
All Rights Reserved. 
Qualcomm Atheros Confidential and Proprietary. 
*/  

//q_os_if.c

#include <sys/types.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/types.h>
#include <linux/if.h>
#include <linux/wireless.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <ctype.h>
#include <getopt.h>
#include <err.h>
#include <errno.h>
#include <time.h>

//#define _LITTLE_ENDIAN  1234    /* LSB first: i386, vax */
//#define _BYTE_ORDER     _LITTLE_ENDIAN

#ifndef _LITTLE_ENDIAN
#define _LITTLE_ENDIAN  1234    /* LSB first: i386, vax */
#endif
#ifndef _BIG_ENDIAN
#define _BIG_ENDIAN     4321    /* MSB first: 68000, ibm, net */
#endif
#include <asm/byteorder.h>
#if defined(__LITTLE_ENDIAN)
#define _BYTE_ORDER     _LITTLE_ENDIAN
#elif defined(__BIG_ENDIAN)
#define _BYTE_ORDER     _BIG_ENDIAN
#else
#error "Please fix asm/byteorder.h"
#endif

#ifndef __packed
#define __packed    __attribute__((__packed__))
#endif

#define	SIOCG80211STATS			(SIOCDEVPRIVATE+2)
#define	IEEE80211_IOCTL_GETKEY		(SIOCDEVPRIVATE+3)
#define	IEEE80211_IOCTL_GETWPAIE	(SIOCDEVPRIVATE+4)
#define	IEEE80211_IOCTL_STA_STATS	(SIOCDEVPRIVATE+5)
#define	IEEE80211_IOCTL_STA_INFO	(SIOCDEVPRIVATE+6)
#define	SIOC80211IFCREATE		(SIOCDEVPRIVATE+7)
#define	SIOC80211IFDESTROY	 	(SIOCDEVPRIVATE+8)
#define	IEEE80211_IOCTL_SCAN_RESULTS	(SIOCDEVPRIVATE+9)
#define IEEE80211_IOCTL_RES_REQ         (SIOCDEVPRIVATE+10)
#define IEEE80211_IOCTL_GETMAC          (SIOCDEVPRIVATE+11)
#define IEEE80211_IOCTL_CONFIG_GENERIC  (SIOCDEVPRIVATE+12)
#define SIOCIOCTLTX99                   (SIOCDEVPRIVATE+13)
#define IEEE80211_IOCTL_P2P_BIG_PARAM   (SIOCDEVPRIVATE+14)
#define SIOCDEVVENDOR                   (SIOCDEVPRIVATE+15)
#define	IEEE80211_IOCTL_GET_SCAN_SPACE  (SIOCDEVPRIVATE+16)


#define ATH_XIOCTL_UNIFIED_UTF_CMD      0x1000
#define ATH_XIOCTL_UNIFIED_UTF_RSP      0x1001

#define CMD_TIMEOUT   10 


//struct ifreq {
//    char ifr_name[IFNAMSIZ]; /* Interface name */
//    union {
//        struct sockaddr ifr_addr;
//        struct sockaddr ifr_dstaddr;
//        struct sockaddr ifr_broadaddr;
//        struct sockaddr ifr_netmask;
//        struct sockaddr ifr_hwaddr;
//        short           ifr_flags;
//        int             ifr_ifindex;
//        int             ifr_metric;
//        int             ifr_mtu;
//        struct ifmap    ifr_map;
//        char            ifr_slave[IFNAMSIZ];
//        char            ifr_newname[IFNAMSIZ];
//        char           *ifr_data;
//    };
//};


typedef struct {
    int sock;
    struct ifreq ifr;
    char ifname[IFNAMSIZ];
    void (*rx_cb)(void *buf);
    unsigned char initialized;
    unsigned char timeout;
    struct sigevent sev;
    timer_t timer;
} INIT_STRUCT;

static INIT_STRUCT initCfg;
static unsigned char responseBuf[2048+8];

int IsDeviceOpened()
{
  return(1);
}

int cmd_set_timer()
{
    struct itimerspec exp_time;
    int err;

    bzero(&exp_time, sizeof(exp_time));
    exp_time.it_value.tv_sec = CMD_TIMEOUT;
    err = timer_settime(initCfg.timer, 0, &exp_time, NULL);
    initCfg.timeout = 0;
    printf("cmd_set_timer ret[%d]..\n",err);
    if (err < 0)
       return errno;

    return 0;
}

int cmd_stop_timer()
{
    struct itimerspec exp_time;
    int err;

    bzero(&exp_time, sizeof(exp_time));
    err = timer_settime(initCfg.timer, 0, &exp_time, NULL);
    printf("cmd_stop_timer ret[%d]..\n",err);
    if (err < 0)
       return errno;

    return 0;
}


static void timer_expire(union sigval sig)
{
    printf("Timer Expired..\n");
    initCfg.timeout = 1;
}

int cmd_init (char *ifname, void (*rx_cb)(void *buf))
{
    int ret = 0,s;

    printf("cmd_init Beg..\n");
    if ( initCfg.initialized )
        return -1;

    memset(&initCfg.ifr, 0, sizeof(initCfg.ifr));
    strncpy(initCfg.ifr.ifr_name, ifname, IFNAMSIZ);

    initCfg.initialized = 1;

    initCfg.rx_cb = rx_cb;

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        err(1, "socket(SOCK_DRAGM)");
        ret = -1;
    }

    initCfg.sock = s;

    initCfg.sev.sigev_notify = SIGEV_THREAD;
    initCfg.sev.sigev_notify_function = timer_expire;

    timer_create(CLOCK_REALTIME,&initCfg.sev,&initCfg.timer);

    printf("cmd_init Socket[%d] ret[%d]..\n",s,ret);
    return ret;
}


int cmd_end()
{
    initCfg.initialized = 0;
	return 0;
}

struct tcmd_tlv_report {
    int preserve;
    short tcmd_cmd;
    short tcmd_subcmd;
    short len;
    short dummy;
};

void cmd_send2 (void *buf, int len, unsigned char *respdata, unsigned int *responseNeeded )
{
    int error = 0, rLen;
    unsigned int *responseCode;

    struct tcmd_tlv_report *ptcmd;
    int isMck=0;
    unsigned char *ifr_inchar;
    
    if (!initCfg.initialized)
       return;

    *(unsigned int *)buf = ATH_XIOCTL_UNIFIED_UTF_CMD;
    *((unsigned int *)buf + 1) = len;

    initCfg.ifr.ifr_data = (void *)buf;

    printf("cmd_send2 bufLen[%d] rspLen[%u]..Beg\n",len, *responseNeeded);
    if (ioctl(initCfg.sock, SIOCIOCTLTX99, &initCfg.ifr) < 0) {
       err(1, "ioctl");
       return;
    }
    printf("cmd_send2 RspNeeded[%d]..Mid\n",*responseNeeded);
    if (*responseNeeded) 
    {
        cmd_set_timer();

        while (1)
        {
            memset(&responseBuf[0], 0, sizeof(responseBuf));
            *(unsigned int *)responseBuf = ATH_XIOCTL_UNIFIED_UTF_RSP;

            initCfg.ifr.ifr_data = (void *)responseBuf;

            error = ioctl(initCfg.sock, SIOCIOCTLTX99, &initCfg.ifr);
            if ( initCfg.timeout )
            {
                printf("cmd_send2 timeout ioctl[%d]..\n",ATH_XIOCTL_UNIFIED_UTF_RSP);
                memset(&responseBuf[0], 0, sizeof(responseBuf));
                initCfg.ifr.ifr_data = responseBuf;
                break;
            }

            if ( error < 0 )
            {
                if ( errno == EAGAIN )
                    continue;
                else
                {
                    printf("------errno %d\n",errno);
                    memset(&responseBuf[0], 0, sizeof(responseBuf));
                    initCfg.ifr.ifr_data = responseBuf;
                    break;
                }
            }
            printf("cmd_send2 OK RspIoctl ret[%d]..\n",error);

            responseCode = (unsigned int*)&initCfg.ifr.ifr_data[32];


            if (*responseCode == 0)
            {
                responseCode = (unsigned int*)&initCfg.ifr.ifr_data[0];  //for mck
            }
           
            ifr_inchar = initCfg.ifr.ifr_data;

            if(ifr_inchar[3] == 0x60 && ifr_inchar[2] == 0x04) 
            {
                isMck =1;
                //clear tag
                ifr_inchar[2] = 0;
                ifr_inchar[3] = 0;
            } 

            printf("%s:: responseCode = 0x%x is mck %d\n", __func__,*responseCode, isMck);

            if (*responseCode!=0)
                break;

        }

        if(!initCfg.timeout)
        {
            cmd_stop_timer(); 
            initCfg.timeout = 0;
        }

        if ( initCfg.rx_cb != NULL ) {
            if (isMck) 
            {
                ptcmd = (struct tcmd_tlv_report *)(initCfg.ifr.ifr_data);
                rLen = ptcmd->len;
                ptcmd->len=0; //clear after use, this is filled by driver
                rLen += 4; //for length field
                memcpy(respdata, (unsigned char *)(initCfg.ifr.ifr_data)+8, rLen);
            } else {
                rLen = *(int *)initCfg.ifr.ifr_data;
                rLen+=4;  // Includes length itself 4 bytes
                memcpy(respdata,(unsigned char *)(initCfg.ifr.ifr_data),rLen);
            }
            printf("cmd_send2 CallBack() rLen[%d]\n",rLen);
            initCfg.rx_cb(initCfg.ifr.ifr_data);
            DispHexString((unsigned char *)respdata, rLen);
            error = rLen;
        }
        *responseNeeded = (error>0)?error:0;
    }
    printf("cmd_send2 ..End [%d]\n", error);
}


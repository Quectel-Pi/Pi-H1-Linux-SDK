/*
* Copyright (c) 2014-2016 Qualcomm Technologies, Inc.
* All Rights Reserved.
* Confidential and Proprietary - Qualcomm Technologies, Inc.
*
*/
#include <stdio.h>
#include "nan_test.hpp"
#include "wifi_hal.h"
#include "common.hpp"

#include <netinet/in.h>
#include <sys/select.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <poll.h>
#include <unistd.h>
#include <pthread.h>

extern size_t strlcpy(char *, const char *, size_t);
extern int get_ifname(char *ifname);

#undef LOG_TAG
#define LOG_TAG "HalProxyDaemon"

#define UDP_PORT 51000

wifi_handle gbl_handle;

#ifndef HAL_PROXY_DAEMON_VER
#define HAL_PROXY_DAEMON_VER "1.0.0"
#endif /* HAL_PROXY_DAEMON_VER */

wifi_interface_handle wifi_get_iface_handle(wifi_handle handle, char *name);
#define SERVER "127.0.0.1"
#define BUFLEN 512  /* Max length of buffer */
#define PORT 8888   /* The port on which to send data */
struct sockaddr_in si;
int app_sock, s2, i, slen=sizeof(si);
char buf[BUFLEN];
char message[BUFLEN];

namespace HAL
{
    class HalProxyDaemon
    {
    public:
        HalProxyDaemon(
            int argc,
            char** argv);

        ~HalProxyDaemon();

        inline bool
        isGood() const
            {
                return isGood_;
            }

        inline wifi_handle
        getWifiHandle()
            {
                return wifiHandle_;
            }
        int
        operator()();

    private:
        enum MsgTypes
        {
            /* Control messages */
            MSG_TYPE_ERROR_RSP         = 0,
            MSG_TYPE_DUMP_STATS_REQ    = 1,
            MSG_TYPE_DUMP_STATS_RSP    = 2,
            MSG_TYPE_CLEAR_STATS_REQ   = 3,
            MSG_TYPE_CLEAR_STATS_RSP   = 4,
            MSG_TYPE_REGISTER_REQ      = 5,
            MSG_TYPE_REGISTER_RSP      = 6,
            MSG_TYPE_DEREGISTER_REQ    = 7,
            MSG_TYPE_DEREGISTER_RSP    = 8,

            /* Data messages */
            MSG_TYPE_RAW_REQ           = 9,
            MSG_TYPE_EVENT             = 10
        };

        void
        hexdump(
            uint8_t* data,
            size_t len);

        void
        clearStats();

        void
        usage() const;

        int
        parseOptions(
            int& argc,
            char** argv);

        const char* program_;
        bool isGood_;

        wifi_handle wifiHandle_;

        struct Stats_t {
            uint32_t numUdpRawReqRcvd_;
            uint32_t numUdpMsgSent_;

            uint32_t numSuppRawReqSent_;
            uint32_t numSuppMsgRcvd_;
        } stats_;

        /* Not thread safe. */
        static const size_t MAX_BUF_SIZE = 2048;
        static char ipcMsgData_[ MAX_BUF_SIZE ];
    };

    /* We do a one-time allocation of this so we don't chew up stack
     * space for this.
     */
    char HalProxyDaemon::ipcMsgData_[
        MAX_BUF_SIZE ];

    HalProxyDaemon::HalProxyDaemon(
        int argc,
        char** argv) :
        program_(argv[0]),
        isGood_(false),
        wifiHandle_(NULL),
        stats_()
    {
        if (parseOptions(argc, argv) < 0)
        {
            printf("parseOptions() failed. \n");
            return;
        }
        clearStats();

        wifi_error err = wifi_initialize(&wifiHandle_);
        if (err)
        {
            printf("wifi hal initialize failed. \n");
            return;
        }

        printf("halProxyDaemon running. \n");

        isGood_ = true;
    }

    HalProxyDaemon::~HalProxyDaemon()
    {
    }


    void
    HalProxyDaemon::usage() const
    {
        fprintf(stderr, "Usage: %s [-option]\n", program_);
        fprintf(stderr, " -h                Display help\n");
        fprintf(stderr, "\n");
    }

    int
    HalProxyDaemon::parseOptions(
        int& argc,
        char** argv)
    {
        UNUSED(argc), UNUSED(argv);
        return 0;
    }


    int
    HalProxyDaemon::operator()()
    {
        return 0;
    }

    void
    HalProxyDaemon::hexdump(
        uint8_t* data,
        size_t len)
    {
        char buf[512];
        uint16_t index;
        uint8_t* ptr;
        int pos;

        ptr = data;
        pos = 0;
        for (index=0; index<len; index++)
        {
            pos += snprintf(&(buf[pos]), sizeof(buf) - pos, "%02x ", *ptr++);
            if (pos > 508)
            {
                break;
            }

        }

    }

    void
    HalProxyDaemon::clearStats()
    {
        memset(&stats_, 0, sizeof(stats_));
    }

}

void app_process_event_handler(void * buf, int len)
{
    char *data = (char *)buf;
    int i=0;

    printf("######################################\n");
    for(i=0; i<len; i++)
    {
        printf("%c", data[i]);
    }
    printf("\n######################################\n");
}

static int app_internal_pollin_handler(wifi_handle handle)
{
    UNUSED(handle);
    memset(buf,'\0', BUFLEN);
    /* Try to receive some data, this is a blocking call */
    /* read datagram from client socket */
    if (recvfrom(app_sock, buf, BUFLEN, 0, (struct sockaddr *) &si,
        (socklen_t*)&slen) == -1)
    {
        printf("Error recvfrom");
    }
    app_process_event_handler(buf, slen);
    return 0;
}

static void app_internal_event_handler(wifi_handle handle, int events)
{
    if (events & POLLERR) {
        printf("Error reading from socket");
    } else if (events & POLLHUP) {
        printf("Remote side hung up");
    } else if (events & POLLIN) {
        printf("Found some events!!!");
        app_internal_pollin_handler(handle);
    } else {
        printf("Unknown event - %0x", events);
    }
}
void app_event_loop(wifi_handle handle)
{
    pollfd pfd;
    memset(&pfd, 0, sizeof(pfd));

    pfd.fd = app_sock;
    pfd.events = POLLIN;

    do {
        pfd.revents = 0;
        printf("Polling socket");
        int result = poll(&pfd, 1, -1);
        printf("Poll result = %0x", result);
        if (result < 0) {
            printf("Error polling socket");
        } else if (pfd.revents & (POLLIN | POLLHUP | POLLERR)) {
            app_internal_event_handler(handle, pfd.revents);
        }
    } while (1);
}

void * my_app_thread_function (void *ptr) {
    UNUSED(ptr);
    app_event_loop(gbl_handle);
    pthread_exit(0);
    return (void *)NULL;
}

void * my_thread_function (void *ptr) {
    UNUSED(ptr);
    wifi_event_loop(gbl_handle);
    pthread_exit(0);
    return (void *)NULL;
}

s8 char_to_hex(char c)
{
    s8 num = -1;

    if (c >= '0' && c <= '9')
       num = c - '0';
    else if (c >= 'a' && c <= 'f')
        num = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
        num = c - 'A' + 10;
    else
        fprintf(stderr, "Not a valid hex char\n");
    return num;
}

/*Convert string to MAC address
* @txt: [IN] MAC address as a string
* @mac_addr: [OUT] Buffer for the MAC address
* @lenght: [IN] lenght of mac address
*/
int mac_addr_aton(u8 *mac_addr, const char *txt, size_t length)
{
    size_t str_len;
    size_t i;

    str_len = (length * 2) + length - 1;
    if (strlen(txt) != str_len) {
        fprintf(stderr, "Invalid MAC Address\n");
        return -1;
     }

    for (i = 0; i < length; i++) {
        s8 a, b;
        a = char_to_hex(*txt++);
        if (a < 0) {
            fprintf(stderr, "Invalid Mac Address \n");
            return -1;
        }
        b = char_to_hex(*txt++);
        if (b < 0) {
            fprintf(stderr, "Invalid Mac Address \n");
            return -1;
        }
        mac_addr[i] = (a << 4) | b;
        if (i < (length - 1) && *txt++ != ':') {
            fprintf(stderr, "Invalid Mac Address \n");
            return -1;
        }
    }
    return 0;
}

int mac_addr_aton(u8 *mac_addr, const char *txt)
{
    return mac_addr_aton(mac_addr, txt, MAC_ADDR_LEN);
}

void cleanup_handler(wifi_handle handle)
{
    fprintf(stderr, "Cleanup done for handle : %p\n", handle);
}

/*Fetch the total-number-of-iterations argument from argv.
i is 4
*/
int get_total_iter_index(int argc, char *argv[], int i)
{
    int total_iter_index = 0;
    int cmdId;
    if (i < argc)
        cmdId = atoi(argv[i-1]);
    else
        return 0;

    switch(cmdId) {
        case 10:
            /* Move i by: "<mac_oui>" */
            total_iter_index = i + 1;
            break;
        case 11:
        case 12:
        case 13:
        case 15:
            /* i is alredy pointing to total iterations */
            total_iter_index = i;
            break;
        case 14:
            /* Move i By: "<id> <no_of_networks> <list_of_ssids>"*/
            if (i+1 < argc)
                total_iter_index = i + 2 + atoi(argv[i + 1]);
            break;
        default:
            fprintf(stderr, "%s: Unknown input.\n", __func__);
    }
    if (total_iter_index && total_iter_index < argc)
        return total_iter_index;

    /* Return 0 means, somthing went wrong in parsing the arguments */
    return 0;
}

/* Validate sleep duration between commands */
int validate_sleep_duration(int sleep_time)
{
    if (sleep_time > MAX_SLEEP_DURATION) {
        fprintf(stderr, "Maximum sleep duration allowed is %d Seconds\n",
                MAX_SLEEP_DURATION);
        return -1;
    }
    return 0;
}
extern void wifi_cleanup(wifi_handle handle, wifi_cleaned_up_handler handler);
int
main(
    int argc,
    char* argv[])
{

    int request_id;
    int cmdId = -1;
    int max = 0;
    int flush = 0;
    int band = 0;
    unsigned int sleep_time;
    char ifname [IFNAMSIZ+1];
    int ret = 0;
    HAL::HalProxyDaemon halProxyDaemon(argc, argv);

    if (!halProxyDaemon.isGood())
    {
        printf("%s: halProxyDaemon failed at startup.\n", argv[0]);
        ::exit(-1);
    }

    wifi_handle wifiHandle = halProxyDaemon.getWifiHandle();
    gbl_handle = wifiHandle;

    pthread_t thread1;  /* thread variables */
    pthread_t thread2;  /* thread variables */

    /* Create an app socket */
    if ( (app_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        fprintf(stderr, "%s: halProxy failed cause the app socket is not created.\n", __func__);
        ::exit(-1);
    }

    /* create threads 1 */
    pthread_create (&thread1, NULL, &my_thread_function, NULL);
    /* create threads 2 */
    ret = get_ifname(ifname);
    if (ret != 0)
        exit(-1);
   
    pthread_create (&thread2, NULL, &my_app_thread_function, NULL);

    wifi_interface_handle ifaceHandle = wifi_get_iface_handle(wifiHandle,
                                                             ifname);
    if (!ifaceHandle)
    {
        fprintf(stderr, "%s: halProxy failed cause it couldn't retrieve iface ptrs.\n", __func__);
        ::exit(-1);
    }
    if(argc >= 2)
    {
        fprintf(stderr, "%s: Version: " HAL_PROXY_DAEMON_VER "\n", argv[0]);
        if(strcasecmp(argv[1], NAN_TEST::NanTestSuite::NAN_CMD) == 0)
        {
            NAN_TEST::NanTestSuite nan(wifiHandle);
            nan.processCmd(argc, argv);
        }
        else
        {
            fprintf(stderr, "%s: Unknown command %s\n", argv[0], argv[1]);
        }
    }
    else
    {
        fprintf(stderr, "%s: Version: " HAL_PROXY_DAEMON_VER "\n", argv[0]);
        fprintf(stderr, "%s: Insufficent args\n", argv[0]);
    }

    if (cmdId != 1000) {
        while (1) {
            usleep(2000000);
            fprintf(stderr, "daemon: Sleep: \n");
        }
    }
    wifi_cleanup(gbl_handle, cleanup_handler);
    usleep(2000000);
    return halProxyDaemon();
}

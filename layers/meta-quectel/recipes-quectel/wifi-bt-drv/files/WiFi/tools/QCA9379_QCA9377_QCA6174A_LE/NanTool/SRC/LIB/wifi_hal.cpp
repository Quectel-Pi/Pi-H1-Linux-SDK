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

#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <libnl3/netlink/genl/genl.h>
#include <libnl3/netlink/genl/family.h>
#include <libnl3/netlink/genl/ctrl.h>
#include <linux/rtnetlink.h>
#include <netpacket/packet.h>
#include <linux/filter.h>
#include <linux/errqueue.h>

#include <linux/pkt_sched.h>
#include <libnl3/netlink/object-api.h>
#include <libnl3/netlink/netlink.h>
#include <libnl3/netlink/socket.h>

#include "nl80211_copy.h"

#include <dirent.h>
#include <net/if.h>
#include <netinet/in.h>

#include "sync.h"

#define LOG_TAG  "WifiHAL"

#include "wifi_hal.h"
#include "common.h"
#include "cpp_bindings.h"
#include "ifaceeventhandler.h"
//#include "wifiloggercmd.h"
//#include "vendor_definitions.h"
#include "qca-vendor_copy.h"
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <bsd/stdlib.h>
size_t strlcpy(char *, const char *, size_t);

/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
size_t
strlcpy(char *dst, const char *src, size_t siz)
{
        char *d = dst;
        const char *s = src;
        size_t n = siz;

        /* Copy as many bytes as will fit */
        if (n != 0 && --n != 0) {
                do {
                        if ((*d++ = *s++) == 0)
                                break;
                } while (--n != 0);
        }

        /* Not enough room in dst, add NUL and traverse rest of src */
        if (n == 0) {
                if (siz != 0)
                        *d = '\0';                /* NUL-terminate dst */
                while (*s++)
                        ;
        }

        return(s - src - 1);        /* count does not include NUL */
}
/*
 BUGBUG: normally, libnl allocates ports for all connections it makes; but
 being a static library, it doesn't really know how many other netlink
 connections are made by the same process, if connections come from different
 shared libraries. These port assignments exist to solve that
 problem - temporarily. We need to fix libnl to try and allocate ports across
 the entire process.
 */

#define WIFI_HAL_CMD_SOCK_PORT       644
#define WIFI_HAL_EVENT_SOCK_PORT     645

static void internal_event_handler(wifi_handle handle, int events,
                                   struct nl_sock *sock);
static int internal_valid_message_handler(nl_msg *msg, void *arg);
static int wifi_get_multicast_id(wifi_handle handle, const char *name,
        const char *group);
static int wifi_add_membership(wifi_handle handle, const char *group);
static wifi_error wifi_init_interfaces(wifi_handle handle);
/* Initialize/Cleanup */

wifi_interface_handle wifi_get_iface_handle(wifi_handle handle, char *name)
{
    hal_info *info = (hal_info *)handle;
    for (int i=0;i<info->num_interfaces;i++)
    {
        if (!strcmp(info->interfaces[i]->name, name))
        {
            return ((wifi_interface_handle )(info->interfaces)[i]);
        }
    }
    return NULL;
}

void wifi_socket_set_local_port(struct nl_sock *sock, uint32_t port)
{
    /* Release local port pool maintained by libnl and assign a own port
     * identifier to the socket.
     */
    nl_socket_set_local_port(sock, ((uint32_t)getpid() & 0x3FFFFFU) | (port << 22));
}

static nl_sock * wifi_create_nl_socket(int port, int protocol)
{
    // printf("Creating socket");
    struct nl_sock *sock = nl_socket_alloc();
    if (sock == NULL) {
        printf("Failed to create NL socket");
        return NULL;
    }

    wifi_socket_set_local_port(sock, port);

    if (nl_connect(sock, protocol)) {
        printf("Could not connect handle");
        nl_socket_free(sock);
        return NULL;
    }

    return sock;
}

int ack_handler(struct nl_msg *msg, void *arg)
{
    int *err = (int *)arg;
    *err = 0;
    return NL_STOP;
}

int finish_handler(struct nl_msg *msg, void *arg)
{
    int *ret = (int *)arg;
    *ret = 0;
    return NL_SKIP;
}

int error_handler(struct sockaddr_nl *nla,
                  struct nlmsgerr *err, void *arg)
{
    int *ret = (int *)arg;
    *ret = err->error;

    printf("%s invoked with error: %d", __func__, err->error);
    return NL_SKIP;
}
static int no_seq_check(struct nl_msg *msg, void *arg)
{
    return NL_OK;
}
wifi_error wifi_initialize(wifi_handle *handle)
{
    int err = 0;
    bool driver_loaded = false;
    wifi_error ret = WIFI_SUCCESS;
    wifi_interface_handle iface_handle;
    struct nl_sock *cmd_sock = NULL;
    struct nl_sock *event_sock = NULL;
    struct nl_cb *cb = NULL;

    hal_info *info = (hal_info *)malloc(sizeof(hal_info));
    if (info == NULL) {
        printf("Could not allocate hal_info");
        return WIFI_ERROR_OUT_OF_MEMORY;
    }

    memset(info, 0, sizeof(*info));

    cmd_sock = wifi_create_nl_socket(WIFI_HAL_CMD_SOCK_PORT,
                                                     NETLINK_GENERIC);
    if (cmd_sock == NULL) {
        printf("Failed to create command socket port");
        ret = WIFI_ERROR_UNKNOWN;
        goto unload;
    }

    /* Set the socket buffer size */
    if (nl_socket_set_buffer_size(cmd_sock, (256*1024), 0) < 0) {
        printf("Could not set nl_socket RX buffer size for cmd_sock: %s",
                   strerror(errno));
        /* continue anyway with the default (smaller) buffer */
    }

    event_sock =
        wifi_create_nl_socket(WIFI_HAL_EVENT_SOCK_PORT, NETLINK_GENERIC);
    if (event_sock == NULL) {
        printf("Failed to create event socket port");
        ret = WIFI_ERROR_UNKNOWN;
        goto unload;
    }

    /* Set the socket buffer size */
    if (nl_socket_set_buffer_size(event_sock, (256*1024), 0) < 0) {
        printf("Could not set nl_socket RX buffer size for event_sock: %s",
                   strerror(errno));
        /* continue anyway with the default (smaller) buffer */
    }

    cb = nl_socket_get_cb(event_sock);
    if (cb == NULL) {
        printf("Failed to get NL control block for event socket port");
        ret = WIFI_ERROR_UNKNOWN;
        goto unload;
    }

    err = 1;
    nl_cb_set(cb, NL_CB_SEQ_CHECK, NL_CB_CUSTOM, no_seq_check, NULL);
    nl_cb_err(cb, NL_CB_CUSTOM, error_handler, &err);
    nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &err);
    nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &err);

    nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, internal_valid_message_handler,
            info);
    nl_cb_put(cb);

    info->cmd_sock = cmd_sock;
    info->event_sock = event_sock;
    info->clean_up = false;
    info->in_event_loop = false;

    info->event_cb = (cb_info *)malloc(sizeof(cb_info) * DEFAULT_EVENT_CB_SIZE);
    if (info->event_cb == NULL) {
        printf("Could not allocate event_cb");
        ret = WIFI_ERROR_OUT_OF_MEMORY;
        goto unload;
    }
    info->alloc_event_cb = DEFAULT_EVENT_CB_SIZE;
    info->num_event_cb = 0;

    info->cmd = (cmd_info *)malloc(sizeof(cmd_info) * DEFAULT_CMD_SIZE);
    if (info->cmd == NULL) {
        printf("Could not allocate cmd info");
        ret = WIFI_ERROR_OUT_OF_MEMORY;
        goto unload;
    }
    info->alloc_cmd = DEFAULT_CMD_SIZE;
    info->num_cmd = 0;

    info->nl80211_family_id = genl_ctrl_resolve(cmd_sock, "nl80211");
    if (info->nl80211_family_id < 0) {
        printf("Could not resolve nl80211 familty id");
        ret = WIFI_ERROR_UNKNOWN;
        goto unload;
    }

    pthread_mutex_init(&info->cb_lock, NULL);
    pthread_mutex_init(&info->pkt_fate_stats_lock, NULL);

    *handle = (wifi_handle) info;

    wifi_add_membership(*handle, "scan");
    wifi_add_membership(*handle, "mlme");
    wifi_add_membership(*handle, "regulatory");
    wifi_add_membership(*handle, "vendor");

    if (ret != WIFI_SUCCESS) {
        printf("Failed to alloc user socket");
        goto unload;
    }
    ret = wifi_init_interfaces(*handle);
    if (ret != WIFI_SUCCESS) {
        printf("Failed to init interfaces\n");
        goto unload;
    }

    if (info->num_interfaces == 0) {
        printf("No interfaces found\n");
        ret = WIFI_ERROR_UNINITIALIZED;
        goto unload;
    }

    iface_handle = wifi_get_iface_handle((info->interfaces[0])->handle,
            (info->interfaces[0])->name);
    if (iface_handle == NULL) {
        int i;
        for (i = 0; i < info->num_interfaces; i++)
        {
            free(info->interfaces[i]);
        }
        printf("%s no iface with %s\n", __func__, info->interfaces[0]->name);
        goto unload;
    }

    info->exit_sockets[0] = -1;
    info->exit_sockets[1] = -1;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, info->exit_sockets) == -1) {
        printf("Failed to create exit socket pair");
        ret = WIFI_ERROR_UNKNOWN;
        goto unload;
    }
unload:
    if (ret != WIFI_SUCCESS) {
        if (cmd_sock)
            nl_socket_free(cmd_sock);
        if (event_sock)
            nl_socket_free(event_sock);
        if (info) {
            if (info->cmd) free(info->cmd);
            if (info->event_cb) free(info->event_cb);
            free(info);
        }
    }
    return ret;
}

static int wifi_add_membership(wifi_handle handle, const char *group)
{
    hal_info *info = getHalInfo(handle);

    int id = wifi_get_multicast_id(handle, "nl80211", group);
    if (id < 0) {
        printf("Could not find group %s", group);
        return id;
    }

    int ret = nl_socket_add_membership(info->event_sock, id);
    if (ret < 0) {
        printf("Could not add membership to group %s", group);
    }

    return ret;
}

static void internal_cleaned_up_handler(wifi_handle handle)
{
    hal_info *info = getHalInfo(handle);
    wifi_cleaned_up_handler cleaned_up_handler = info->cleaned_up_handler;

    if (info->cmd_sock != 0) {
        nl_socket_free(info->cmd_sock);
        nl_socket_free(info->event_sock);
        info->cmd_sock = NULL;
        info->event_sock = NULL;
    }

    if (info->exit_sockets[0] >= 0) {
        close(info->exit_sockets[0]);
        info->exit_sockets[0] = -1;
    }

    if (info->exit_sockets[1] >= 0) {
        close(info->exit_sockets[1]);
        info->exit_sockets[1] = -1;
    }
    (*cleaned_up_handler)(handle);
    pthread_mutex_destroy(&info->cb_lock);
    pthread_mutex_destroy(&info->pkt_fate_stats_lock);
    free(info);
}
void wifi_cleanup(wifi_handle handle, wifi_cleaned_up_handler handler)
{
    if (!handle) {
        printf("Handle is null");
        return;
    }

    hal_info *info = getHalInfo(handle);
    info->cleaned_up_handler = handler;
    info->clean_up = true;

    TEMP_FAILURE_RETRY(write(info->exit_sockets[0], "E", 1));
    printf("Sent msg on exit sock to unblock poll()");
}
static int internal_pollin_handler(wifi_handle handle, struct nl_sock *sock)
{
    struct nl_cb *cb = nl_socket_get_cb(sock);

    int res = nl_recvmsgs(sock, cb);
    if(res)
        printf("Error :%d while reading nl msg", res);
    nl_cb_put(cb);
    return res;
}

static void internal_event_handler(wifi_handle handle, int events,
                                   struct nl_sock *sock)
{
    if (events & POLLERR) {
        printf("Error reading from socket");
        internal_pollin_handler(handle, sock);
    } else if (events & POLLHUP) {
        printf("Remote side hung up");
    } else if (events & POLLIN) {
        //printf("Found some events!!!");
        internal_pollin_handler(handle, sock);
    } else {
        printf("Unknown event - %0x", events);
    }
}
/* Run event handler */
void wifi_event_loop(wifi_handle handle)
{
    hal_info *info = getHalInfo(handle);
    if (info->in_event_loop) {
        return;
    } else {
        info->in_event_loop = true;
    }

    pollfd pfd[3];
    memset(&pfd, 0, 3*sizeof(pfd[0]));

    pfd[0].fd = nl_socket_get_fd(info->event_sock);
    pfd[0].events = POLLIN;

    pfd[1].fd = info->exit_sockets[1];
    pfd[1].events = POLLIN;

    /* TODO: Add support for timeouts */

    do {
        pfd[0].revents = 0;
        pfd[1].revents = 0;
        //printf("Polling sockets");
        int result = poll(pfd, 3, -1);
        if (result < 0) {
            printf("Error polling socket");
        } else {
            if (pfd[0].revents & (POLLIN | POLLHUP | POLLERR)) {
                internal_event_handler(handle, pfd[0].revents, info->event_sock);
            }
        }
        /*rb_timerhandler(info);*/
    } while (!info->clean_up);
    internal_cleaned_up_handler(handle);
}
static int internal_valid_message_handler(nl_msg *msg, void *arg)
{
    wifi_handle handle = (wifi_handle)arg;
    hal_info *info = getHalInfo(handle);

    WifiEvent event(msg);
    int res = event.parse();
    if (res < 0) {
        printf("Failed to parse event: %d", res);
        return NL_SKIP;
    }

    int cmd = event.get_cmd();
    uint32_t vendor_id = 0;
    int subcmd = 0;

    if (cmd == NL80211_CMD_VENDOR) {
        vendor_id = event.get_u32(NL80211_ATTR_VENDOR_ID);
        subcmd = event.get_u32(NL80211_ATTR_VENDOR_SUBCMD);
        /* Restrict printing GSCAN_FULL_RESULT which is causing lot
           of logs in bug report */
        if (subcmd != QCA_NL80211_VENDOR_SUBCMD_GSCAN_FULL_SCAN_RESULT) {
            printf("event received %s, vendor_id = 0x%0x, subcmd = 0x%0x",
                  event.get_cmdString(), vendor_id, subcmd);
        }
    } else {
        printf("event received %s", event.get_cmdString());
    }

    // event.log();

    bool dispatched = false;

    pthread_mutex_lock(&info->cb_lock);

    for (int i = 0; i < info->num_event_cb; i++) {
        if (cmd == info->event_cb[i].nl_cmd) {
            if (cmd == NL80211_CMD_VENDOR
                && ((vendor_id != info->event_cb[i].vendor_id)
                || (subcmd != info->event_cb[i].vendor_subcmd)))
            {
                /* event for a different vendor, ignore it */
                continue;
            }

            cb_info *cbi = &(info->event_cb[i]);
            pthread_mutex_unlock(&info->cb_lock);
            if (cbi->cb_func) {
                (*(cbi->cb_func))(msg, cbi->cb_arg);
                dispatched = true;
            }
            return NL_OK;
        }
    }

#ifdef QC_HAL_DEBUG
    if (!dispatched) {
        printf("event ignored!!");
    }
#endif

    pthread_mutex_unlock(&info->cb_lock);
    return NL_OK;
}

////////////////////////////////////////////////////////////////////////////////

class GetMulticastIdCommand : public WifiCommand
{
private:
    const char *mName;
    const char *mGroup;
    int   mId;
public:
    GetMulticastIdCommand(wifi_handle handle, const char *name,
            const char *group) : WifiCommand(handle, 0)
    {
        mName = name;
        mGroup = group;
        mId = -1;
    }

    int getId() {
        return mId;
    }

    virtual int create() {
        int nlctrlFamily = genl_ctrl_resolve(mInfo->cmd_sock, "nlctrl");
        // printf("ctrl family = %d", nlctrlFamily);
        int ret = mMsg.create(nlctrlFamily, CTRL_CMD_GETFAMILY, 0, 0);
        if (ret < 0) {
            return ret;
        }
        ret = mMsg.put_string(CTRL_ATTR_FAMILY_NAME, mName);
        return ret;
    }

    virtual int handleResponse(WifiEvent& reply) {

        // printf("handling reponse in %s", __func__);

        struct nlattr **tb = reply.attributes();
        struct nlattr *mcgrp = NULL;
        int i;

        if (!tb[CTRL_ATTR_MCAST_GROUPS]) {
            printf("No multicast groups found");
            return NL_SKIP;
        } else {
            // printf("Multicast groups attr size = %d",
            // nla_len(tb[CTRL_ATTR_MCAST_GROUPS]));
        }

        for_each_attr(mcgrp, tb[CTRL_ATTR_MCAST_GROUPS], i) {

            // printf("Processing group");
            struct nlattr *tb2[CTRL_ATTR_MCAST_GRP_MAX + 1];
            nla_parse(tb2, CTRL_ATTR_MCAST_GRP_MAX, (nlattr *)nla_data(mcgrp),
                nla_len(mcgrp), NULL);
            if (!tb2[CTRL_ATTR_MCAST_GRP_NAME] || !tb2[CTRL_ATTR_MCAST_GRP_ID])
            {
                continue;
            }

            char *grpName = (char *)nla_data(tb2[CTRL_ATTR_MCAST_GRP_NAME]);
            int grpNameLen = nla_len(tb2[CTRL_ATTR_MCAST_GRP_NAME]);

            // printf("Found group name %s", grpName);

            if (strncmp(grpName, mGroup, grpNameLen) != 0)
                continue;

            mId = nla_get_u32(tb2[CTRL_ATTR_MCAST_GRP_ID]);
            break;
        }

        return NL_SKIP;
    }

};

static int wifi_get_multicast_id(wifi_handle handle, const char *name,
        const char *group)
{
    GetMulticastIdCommand cmd(handle, name, group);
    int res = cmd.requestResponse();
    if (res < 0)
        return res;
    else
        return cmd.getId();
}

/////////////////////////////////////////////////////////////////////////

static bool is_wifi_interface(const char *name)
{
    if (strncmp(name, "wlan", 4) != 0 && strncmp(name, "p2p", 3) != 0) {
        /* not a wifi interface; ignore it */
        return false;
    } else {
        return true;
    }
}

static int get_interface(const char *name, interface_info *info)
{
    strlcpy(info->name, name, (IFNAMSIZ + 1));
    info->id = if_nametoindex(name);
    // printf("found an interface : %s, id = %d", name, info->id);
    return WIFI_SUCCESS;
}

wifi_error wifi_init_interfaces(wifi_handle handle)
{
    hal_info *info = (hal_info *)handle;

    struct dirent *de;

    DIR *d = opendir("/sys/class/net");
    if (d == 0)
        return WIFI_ERROR_UNKNOWN;

    int n = 0;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.')
            continue;
        if (is_wifi_interface(de->d_name) ) {
            n++;
        }
    }

    closedir(d);

    d = opendir("/sys/class/net");
    if (d == 0)
        return WIFI_ERROR_UNKNOWN;

    info->interfaces = (interface_info **)malloc(sizeof(interface_info *) * n);
    if (info->interfaces == NULL) {
        printf("%s: Error info->interfaces NULL", __func__);
        return WIFI_ERROR_OUT_OF_MEMORY;
    }

    int i = 0;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.')
            continue;
        if (is_wifi_interface(de->d_name)) {
            interface_info *ifinfo
                = (interface_info *)malloc(sizeof(interface_info));
            if (ifinfo == NULL) {
                printf("%s: Error ifinfo NULL", __func__);
                while (i > 0) {
                    free(info->interfaces[i-1]);
                    i--;
                }
                free(info->interfaces);
                return WIFI_ERROR_OUT_OF_MEMORY;
            }
            if (get_interface(de->d_name, ifinfo) != WIFI_SUCCESS) {
                free(ifinfo);
                continue;
            }
            ifinfo->handle = handle;
            info->interfaces[i] = ifinfo;
            i++;
        }
    }

    closedir(d);

    info->num_interfaces = n;

    return WIFI_SUCCESS;
}

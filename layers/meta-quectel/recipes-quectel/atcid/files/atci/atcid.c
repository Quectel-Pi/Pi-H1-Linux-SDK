/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein is
 * confidential and proprietary to MediaTek Inc. and/or its licensors. Without
 * the prior written permission of MediaTek inc. and/or its licensors, any
 * reproduction, modification, use or disclosure of MediaTek Software, and
 * information contained herein, in whole or in part, shall be strictly
 * prohibited.
 *
 * MediaTek Inc. (C) 2010. All rights reserved.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER
 * ON AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL
 * WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR
 * NONINFRINGEMENT. NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH
 * RESPECT TO THE SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY,
 * INCORPORATED IN, OR SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES
 * TO LOOK ONLY TO SUCH THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO.
 * RECEIVER EXPRESSLY ACKNOWLEDGES THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO
 * OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES CONTAINED IN MEDIATEK
 * SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK SOFTWARE
 * RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S
 * ENTIRE AND CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE
 * RELEASED HEREUNDER WILL BE, AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE
 * MEDIATEK SOFTWARE AT ISSUE, OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE
 * CHARGE PAID BY RECEIVER TO MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek
 * Software") have been modified by MediaTek Inc. All revisions are subject to
 * any receiver's applicable license agreements with MediaTek Inc.
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <libevdev/libevdev.h>
#include <time.h>
#include <sys/epoll.h>
#include <string.h>
#include <errno.h>

#define KEY1_CODE (114)
#define KEY2_CODE (115)
#define KEY_POWER_CODE (116)
#define HOLD_SECONDS (3)

#define NUM_DEVICES 3
const char *DEVICE_PATHS[NUM_DEVICES] = {
    "/dev/input/event1",
    "/dev/input/event2",
    "/dev/input/event3"
};

int key_power_down = 0;
int key1_down = 0;
int key2_down = 0;

int watch_key_init(void) 
{
    struct libevdev *devs[NUM_DEVICES] = {NULL};
	struct epoll_event ev, events[NUM_DEVICES];
	time_t press_time = 0;
    int fds[NUM_DEVICES];
    int epfd;


    for (int i = 0; i < NUM_DEVICES; ++i) 
	{
        fds[i] = open(DEVICE_PATHS[i], O_RDONLY | O_NONBLOCK);
        if (fds[i] < 0) {
            perror(DEVICE_PATHS[i]);
            return 1;
        }

        if (ioctl(fds[i], EVIOCGRAB, 1) < 0) {
            fprintf(stderr, "Failed to grab %s: %s\n", DEVICE_PATHS[i], strerror(errno));
            close(fds[i]);
            return 1;
        }
        if (libevdev_new_from_fd(fds[i], &devs[i]) < 0) {
            perror("libevdev_new_from_fd");
            return 1;
        }
        printf("Listening on %s (%s)\n", DEVICE_PATHS[i], libevdev_get_name(devs[i]));
    }

    epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1 failed");
        return 1;
    }
    
    for (int i = 0; i < NUM_DEVICES; ++i) {
        ev.events = EPOLLIN;
        ev.data.u32 = i; 
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, fds[i], &ev) < 0) {
            perror("epoll_ctl");
            return 1;
        }
    }

    while (1) 
	{
        int nfds = epoll_wait(epfd, events, NUM_DEVICES, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }
        for (int n = 0; n < nfds; ++n) 
		{
            int idx = events[n].data.u32;
            struct input_event iev;
            while (libevdev_next_event(devs[idx], LIBEVDEV_READ_FLAG_NORMAL, &iev) == 0) 
			{
				 //power key check
				 if (iev.type == EV_KEY && iev.code == KEY_POWER_CODE) 
				 {
					if (iev.value == 1) 
					{  // key down
						press_time = time(NULL);
						key_power_down = 1;
                        printf("Power key down.\n");
					} 
					else if (iev.value == 0 && key_power_down) {  // key up
						time_t now = time(NULL);
						int held = (int)(now - press_time);
						key_power_down = 0;

						//if (held >= HOLD_SECONDS) {
						//	printf("Power key held %d seconds. Shutting down...\n", held);
						//	(void)system("sync");
						//	(void)system("shutdown -h now");
						//	goto cleanup;
						//} 
						//else 
						{
							printf("Power key up.\n");
						}
					}
				}
				
				//another two keys check
				else if(iev.type == EV_KEY && (iev.code == KEY1_CODE || iev.code == KEY2_CODE))
				{
					if (iev.code == KEY1_CODE) {
						if (iev.value == 1) { // key1 down
							key1_down = 1;
							printf("key 1 down.\n");
						} else if (iev.value == 0 && key1_down) { // key1 up
							key1_down = 0;
							printf("key 1 up.\n");
						}
					} else if (iev.code == KEY2_CODE) {
						if (iev.value == 1) { // key2 down
							key2_down = 1;
							printf("key 2 down.\n");
						} else if (iev.value == 0 && key2_down) { // key2 up
							key2_down = 0;
							printf("key 2 up.\n");
						}
					}
				}
            }
        }
    }

cleanup:
    for (int i = 0; i < NUM_DEVICES; ++i) {
        if (devs[i]) {
            libevdev_free(devs[i]);
        }
        if (fds[i] >= 0) {
            close(fds[i]);
        }
    }
    close(epfd);
    return 0;
}

#include "atcid.h"
#include "atcid_serial.h"

#ifdef ANDROID
#include <cutils/sockets.h>
#endif

#include <netinet/in.h>
#include <sys/socket.h>

#include "quectel/inc/ql_main_uart.h"
#include "quectel/inc/ql_common_api.h"

#include <sys/shm.h>

/*misc global vars */
Serial serial;

#define PROP_ATM_MODE "ro.boot.atm"
#define BUILD_TYPE_PROP "ro.vendor.build.type"

#define BOOTMODE_PATH "/proc/boot_mode"


#define UNKNOWN_BOOT -1
#define NORMAL_BOOT 0
#define META_BOOT 1

#define MAX_RETRY_COUNT 10 //Add by Quectel

static int *sim_slot = NULL;
extern int adb_socket_listen(int socketFd);
extern int get_control_socket(char* name);


void quec_set_sim_slot(int slot)
{
    *sim_slot = slot;
}

int quec_get_sim_slot()
{
    return *sim_slot;
}

void *watch_key_thread_func(void *arg)
{
    watch_key_init();
    return NULL;
}
/*
* Purpose:  The main program loop
* Return:    0
*/
int main() {
    int i = 0;
    int retry_count = 0;
    LOGATCI(LOG_INFO, "atcid-daemon start!!!");

    pthread_t pid;
    pthread_create(&pid, NULL, watch_key_thread_func, NULL);
    //Initial the parameter for serial dervice
    initSerialDevice(&serial);

    //for CPE / IVT listen TTYGS0
    snprintf(serial.devicename[0], strlen(TTY_GS0) + 1, "%s", TTY_GS0);
    int listenVCOM = 1; //listen ETS port as default
    int bootMode = readSys_int(BOOTMODE_PATH);

    LOGATCI(LOG_INFO, "bootMode = %d", bootMode);

    if (bootMode == META_BOOT) {
        LOGATCI(LOG_INFO, "meta mode for CPE/ IVT, don't listen");
        listenVCOM = 0;
    }

    fct_data_init();
    // int uart_count = 0;
    // for (retry_count = 0; retry_count < MAX_RETRY_COUNT; retry_count++)
    // {
    //     uart_count = ql_open_uart(); // for Quectel AT service listen ttyS1 and virturl COM
    //     if (uart_count <= 0)
    //     {
    //         LOGATCI(LOG_INFO, "!!!!!Quectel: Fail to open custom uart!!!!!!");
    //         sleep(1);
    //     }
    //     else
    //     {
    //         LOGATCI(LOG_INFO, "Quectel: Open custom uart success");
    //         break;
    //     }
    // }

        for (i = 0; i < MAX_DEVICE_VCOM_NUM; i++)
        {
            retry_count = 0;

            while (retry_count < MAX_RETRY_COUNT)
            {
                if ((serial.fd[i] = open_serial_device(&serial, serial.devicename[i])) ==
                    INVALIDE_SOCKET_FD)
                {
                    LOGATCI(LOG_ERR, "Could not open serial device [%d] and start atci service, retry_count:%d", i, retry_count);
                    sleep(1);
                    retry_count++;
                }
                else
                {
                    LOGATCI(LOG_DEBUG, "OPEN SERIAL DEVICE [%d] OK", i);
                    break;
                }
            }

            if (retry_count == MAX_RETRY_COUNT)
            {
                LOGATCI(LOG_ERR, "Max retry count reached for serial device [%d], exiting.", i);
                return ATCID_OPEN_SERIAL_DEV_ERR;
            }
        }


    readerLoop((void*) &serial);

    return 0;
}

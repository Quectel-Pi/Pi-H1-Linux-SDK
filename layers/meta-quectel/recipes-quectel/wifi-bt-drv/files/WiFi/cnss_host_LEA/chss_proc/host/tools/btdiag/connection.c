/*
 Copyright (c) 2013-2020 Qualcomm Technologies, Inc.
 All Rights Reserved.
 Confidential and Proprietary - Qualcomm Technologies, Inc.
 NOT A CONTRIBUTION

 Apache v2 license retained for attribution and notice purposes only.

 Copyright (C) 2009-2012 Broadcom Corporation
 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at:
 http://www.apache.org/licenses/LICENSE-2.0
 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
*/

/* 
Copyright (c) 2013-2020 Qualcomm Technologies, Inc.
All Rights Reserved. 
Confidential and Proprietary - Qualcomm Technologies, Inc.
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <termios.h>
#include <time.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <semaphore.h>

#ifdef BLUEDROID_STACK
#include <dlfcn.h>
#include <pthread.h>
#include <ctype.h>
#include <sys/prctl.h>
#include <sys/capability.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include <private/android_filesystem_config.h>
#include <android/log.h>

#include <hardware/hardware.h>
#include <hardware/bluetooth.h>

#define PID_FILE "/data/.bdt_pid"
#define UNUSED __attribute__((unused))
/* HCI message type definitions (for H4 messages) */
#define HCIT_TYPE_COMMAND   1
#define HCIT_TYPE_ACL_DATA  2
#define HCIT_TYPE_SCO_DATA  3
#define HCIT_TYPE_EVENT     4
#define HCIT_TYPE_LM_DIAG   7
#define HCIT_TYPE_NFC       16

static bt_status_t status;
/* Main API */
static bluetooth_device_t* bt_device;

/*Bluetooth interface.*/
const bt_interface_t* sBtInterface = NULL;
static gid_t groups[] = { AID_NET_BT, AID_INET, AID_NET_BT_ADMIN,
                          AID_SYSTEM, AID_MISC, AID_SDCARD_RW,
                          AID_NET_ADMIN, AID_VPN};

/* Set to 1 when the Bluedroid stack is enabled */
static unsigned char bt_enabled = 0;

/*Save raw data from HCI event*/
struct raw_data{
	unsigned char raw_data[1024];
	int len;
};
struct raw_data raw_data_t;
#endif/*BLUEDROID_STACK*/

#ifdef BLUEZ_STACK
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#endif/*BLUEZ_STACK*/

#ifdef BLUETOPIA_STACK
#include "SS1BTPS.h"
#include "HCITypes.h"
#endif/*BLUETOPIA_STACK*/

#ifdef PLATFORM_ANDROID
#include "connection.h"
#include "hciattach_rome.h"
#endif

#ifdef BT_HCI_UART
#include "connection.h"
#include "hciattach_rome.h"
#endif

#define FLOW_CTL	0x0001
#define ENABLE_PM	1
#define DISABLE_PM	0

#define SOL_HCI 0
#define HCI_MAX_EVENT_SIZE 260
#define HCI_COMMAND_HDR_SIZE 3
#define HCI_FILTER 2

struct uart_t {
	char *type;
	int  init_speed;
	int  speed;
	int  flags;
	char *bdaddr;
	int  (*init) (int fildes, struct uart_t *u, struct termios *ti);
};

/*Define signal variable*/
sem_t sem;



	
static void sig_alarm(int sig)
{
	fprintf(stderr, "Initialization timed out.\n");
	exit(1);
}

unsigned char change_to_tcio_baud(int cfg_baud, int *baud)
{
	if (cfg_baud == 115200)
		*baud = B115200;
	else if (cfg_baud == 4000000)
		*baud = B4000000;
	else if (cfg_baud == 3000000)
		*baud = B3000000;
	else if (cfg_baud == 2000000)
		*baud = B2000000;
	else if (cfg_baud == 1000000)
		*baud = B1000000;
	else if (cfg_baud == 921600)
		*baud = B921600;
	else if (cfg_baud == 460800)
		*baud = B460800;
	else if (cfg_baud == 230400)
		*baud = B230400;
	else if (cfg_baud == 57600)
		*baud = B57600;
	else if (cfg_baud == 19200)
		*baud = B19200;
	else if (cfg_baud == 9600)
		*baud = B9600;
	else if (cfg_baud == 1200)
		*baud = B1200;
	else if (cfg_baud == 600)
		*baud = B600;
	else
	{
		printf( "unsupported baud: %i, use default baud value - B115200\n", cfg_baud);
		*baud = B115200;
		return 1;
	}

	return 0;
}

int set_baud_rate(int fildes, struct termios *ti, int speed)
{
	int tcio_baud;
	
	change_to_tcio_baud(speed, &tcio_baud);

	if (cfsetospeed(ti, tcio_baud) < 0)
		goto fail;

	if (cfsetispeed(ti, tcio_baud) < 0)
		goto fail;

	if (tcsetattr(fildes, TCSANOW, ti) < 0)
		goto fail;

	return 0;
	
fail:
    return -errno;
}

int read_hci_event(int fildes, unsigned char* databuf, int len)
{
	int received_bytes = 0;
	int remain, ret = 0;

	if (len <= 0)
		goto fail;

	while (1) {
	    ret = read(fildes, databuf, 1);
		if (ret <= 0)
			goto fail;
		else if (databuf[0] == 4)
			break;
	}
	received_bytes++;
	
	while (received_bytes < 3) {
		ret = read(fildes, databuf + received_bytes, 3 - received_bytes);
		if (ret <= 0)
		    goto fail;
		received_bytes += ret;
	}

	/* Now we read the parameters. */
	if (databuf[2] < (len - 3))
	    remain = databuf[2];
	else
		remain = len - 3;

	while ((received_bytes - 3) < remain) {
		ret = read(fildes, databuf + received_bytes, remain - (received_bytes - 3));
		
		if (ret <= 0)
			goto fail;
		received_bytes += ret;
	}

	return received_bytes;

fail:
    return -1;	
}

int fluoride_disable_controller_log(int fd)
{
	int ret;
	unsigned char cmd[] = {0x01, 0x17, 0xfc, 0x02, 0x14, 0x00};
	unsigned char evt[64];

	if (fd < 0)
		return -1;

	ret = fluoride_send_hci_cmd(fd, cmd, sizeof(cmd));
	if (ret)
		return -2;

	memset(evt, 0x00,sizeof(evt));
	ret = fluoride_read_raw_data(fd, evt, sizeof(evt));
	if (ret <= 0)
		return -3;

	return 0;
}

int fluoride_usb_connection(char *dev)
{
	int ret;
	ret = open(dev, O_RDWR|O_NOCTTY);
	printf("%s: open(%s, ...) = %d\n", __func__, dev, ret);
	return ret;
}

int fluoride_send_hci_cmd(int fd, unsigned char *rawbuf, int rawdatalen)
{
	int err = 0;

	err = write(fd, rawbuf, rawdatalen);
	if (err != rawdatalen) {
		fprintf(stderr, "%s: Send failed with ret value: %d\n", __FUNCTION__, err);
		goto error;
	}
	err = 0;
//	usleep(HCI_CMD_DELAY_TIME);

error:
	return err;
}

int fluoride_read_raw_data(int fd, unsigned char *buf, int size)
{
	int ret;

#ifdef PLATFORM_ANDROID
	fprintf(stderr,"Wait event \n");
#else
	printf("wait event...\n");
#endif

	ret = read_hci_event(fd, buf, size);
	return ret;
}

static int qca(int fildes, struct uart_t *u, struct termios *ti)
{
    fprintf(stderr,"qca\n %d",fildes);
    return qca_soc_init(fildes, u->speed, u->bdaddr);
}

struct uart_t uart[] = {
    /* QCA ROME */
    { "qca", 115200, 115200, FLOW_CTL, NULL, qca},
	{ NULL, 0 }
};

static struct uart_t * get_type_from_table(char *type)
{
	int i = 0;

	// get type from table
	while(uart[i].type)
	{
	    if (!strcmp(uart[i].type, type))
			return &uart[i];
	    i++;
	}
	
	return NULL;
}

void set_rtscts_flag(struct termios *ti, bool enable)
{
	if (enable)
		ti->c_cflag |= CRTSCTS;
	else
		ti->c_cflag &= ~CRTSCTS;
}

static int initport(char *dev, struct uart_t *u, int sendbreak)
{
	int fildes;
	struct termios term_attr;

	if ((fildes = open(dev, O_RDWR | O_NOCTTY)) == -1)
	{
		printf("Uart: Open serial port failed\n");
		return -1;
	}

	//Flush Terminal Input or Output
	if (tcflush(fildes, TCIOFLUSH) != 0)
		perror("tcflush error");
		
	if (tcgetattr(fildes, &term_attr) != 0) {
		printf("Uart: Get port settings failed\n");
		return -1;
	}

	cfmakeraw(&term_attr);
	
	term_attr.c_cflag |= CLOCAL;
	if (u->flags & FLOW_CTL)
	{
		// flow control via CTS/RTS
		set_rtscts_flag(&term_attr, 1);
	}
	else
		set_rtscts_flag(&term_attr, 0);
	
	if (tcsetattr(fildes, TCSANOW, &term_attr) != 0) {
		printf("Uart: Set port settings failed\n");
		return -1;
	}

	/*
	* 115200 is for the first cmd after controller is just powered up, 
	* it is the default baudrate of controller. 
	* Before downloading,it will be changed to the value of input baudrate.
	*/
	//if (set_baud_rate(fildes, &term_attr, u->init_speed) < 0) {
	if (set_baud_rate(fildes, &term_attr, 115200) < 0) {
		printf("Uart: Set initial baud rate failed\n");
		return -1;
	}
	
	if (tcflush(fildes, TCIOFLUSH) != 0)
		perror("tcflush error");
	
	if (sendbreak) {
		tcsendbreak(fildes, 0);
		usleep(400000);
	}

	if (u->init && u->init(fildes, u, &term_attr) < 0)
		return -1;

	if (tcflush(fildes, TCIOFLUSH) != 0)
		perror("tcflush error");

	return fildes;
}

/*
 *  The value of below parameters are referred to hciattach.c and hciattach_rome.c(BlueZ):
 *    (1)Device type is "qca"
 *    (2)Flow control is turn "ON" 
 *    (3)Waiting time is "120 sec"
 */
int SerialConnection(char *dev_path, char *baudrate)
{
	struct uart_t *u = NULL;
	int opt, i, n, ld, err;
	int to = 10;
	int init_speed = 0;
	int sendbreak = 0;
	pid_t pid;
	struct sigaction sa;
	struct pollfd p;
	sigset_t sigs;
	char dev[PATH_MAX];
	
	/* Set the parameters */
	strcpy(dev, dev_path);
    u = get_type_from_table("qca");  	
	u->speed = atoi(baudrate);    
	u->flags |=  FLOW_CTL;
    to = 120;

	if (!u) {
		fprintf(stderr, "Unknown device type or id\n");
		exit(-1);
	}

	/* If user specified a initial speed, use that instead of
	   the hardware's default */
	if (init_speed)
            u->init_speed = init_speed;

	if (u->speed)
            u->init_speed = u->speed;

	memset(&sa, 0, sizeof(sa));
	sa.sa_flags   = SA_NOCLDSTOP;
	sa.sa_handler = sig_alarm;
	sigaction(SIGALRM, &sa, NULL);

	/* 10 seconds should be enough for initialization */
	alarm(to);

#ifdef USE_VENDOR_LIB
	n = bc_download_fw();
#else
	n = initport(dev, u, sendbreak);
#endif
	if (n < 0) {
		perror("Can't initialize device");
		exit(-1);
	}
	
	#ifdef PLATFORM_ANDROID
	fprintf(stderr,"Download Patch firmware and NVM file completed\n");
	#else
	printf("Download Patch firmware and NVM file completed\n");
	#endif

	alarm(0);

	sem_init(&sem,0,0);

	return n;
}

#ifdef HCI_CMD_NEED_DELAY
int uart_send_hci_cmd(int fd, unsigned char *rawbuf, int rawdatalen)
{
    struct uart_t *u = NULL;
    struct termios term_attr;
    int err = 0;
    int flags;

    err = write(fd, rawbuf, rawdatalen);
    if (err != rawdatalen) {
        fprintf(stderr, "%s: Send failed with ret value: %d\n", __FUNCTION__, err);
        goto error;
    }
    usleep(HCI_CMD_DELAY_TIME);
    sem_post(&sem);

error:
    return err;
}

int uart_read_raw_data(int fd, unsigned char *buf, int size)
{
    int ret;

    #ifdef PLATFORM_ANDROID
    fprintf(stderr,"Wait event \n");
    #else
    printf("wait event...\n");
    #endif
    sem_wait(&sem);

    ret = read_hci_event(fd, buf, size);
    return ret;
}
#endif

#ifdef BLUEDROID_STACK

/*****************************************************************************
**   Logger API
*****************************************************************************/

void bdt_log(const char *fmt_str, ...)
{
    static char buffer[1024];
    va_list ap;

    va_start(ap, fmt_str);
    vsnprintf(buffer, 1024, fmt_str, ap);
    va_end(ap);

    fprintf(stdout, "%s\n", buffer);
}

void check_return_status(bt_status_t status)
{
    if (status != BT_STATUS_SUCCESS)
    {
        bdt_log("HAL REQUEST FAILED status : %d", status);
    }
    else
    {
        bdt_log("HAL REQUEST SUCCESS");
    }
}

static void adapter_state_changed(bt_state_t state)
{
    bdt_log("ADAPTER STATE UPDATED : %s", (state == BT_STATE_OFF)?"OFF":"ON");
    if (state == BT_STATE_ON) {
        bt_enabled = 1;
    } else {
        bt_enabled = 0;
    }
}

extern "C" 
{
static void hci_event_recv(uint8_t evt_code, uint8_t *buf, uint8_t len)
{
	int i = 0;
	uint8_t type = HCIT_TYPE_EVENT;
	int length;
	
	memset(raw_data_t.raw_data,0,sizeof(raw_data_t.raw_data));
	bdt_log("%s: RECVD. EVENT CODE: 0x%x,len: %d", __func__, evt_code,len);
	length = sizeof(evt_code) + sizeof(len) + sizeof(type);
	raw_data_t.raw_data[0] = type;
	raw_data_t.raw_data[1] = evt_code;
	raw_data_t.raw_data[2] =len;
	/*
	 * bluedroid stack set random device address, that will trigger this callback.
	 * ignore random address event here.
	 * set random address:	ocf---0x0005,ogf---0x08,
	 * opcode = (ocf&0x03ff)|(ogf<<10)---0x2005
	 */
	if(len > 3){
		if((buf[1] == 0x05) && (buf[2] == 0x20))
			return;
	}
	for(i = 0; i < len; i++){
		raw_data_t.raw_data[length + i] = buf[i];
		//printf("%02x",raw_data_t.raw_data[length + i]);
	}
	printf("\n");

	raw_data_t.len = length + len;
	/*Send semaphore to thread Notifyobserver,event is update now.*/
	sem_post(&sem);
}
}
/*
 * Send an HCI command to (USB) device.
 *
 */
 
int bluedroid_usb_send_hci_cmd(unsigned char *rawbuf, int rawdatalen)
{
	unsigned char buf[HCI_MAX_EVENT_SIZE];
	unsigned char *ptr = buf;
	unsigned char *param_ptr = NULL;
	hci_event_hdr *hdr;
	int param_len;
	uint16_t ocf, opcode;
	uint8_t ogf;	

	/* calculate OGF and OCF: ogf is bit 10~15 of opcode, ocf is bit 0~9 of opcode */
	errno = 0;	
	opcode = rawbuf[1] | (rawbuf[2] << 8);
	ogf = opcode >> 10;
	ocf = opcode & 0x3ff;
	
	if (errno == ERANGE || (ogf > 0x3f) || (ocf > 0x3ff)) {
		printf("wrong input format\n");
		exit(EXIT_FAILURE);
	}
	
	/* copy parameter to buffer and calculate parameter total length */	
	param_ptr = rawbuf + (1 + HCI_COMMAND_HDR_SIZE);
	//parameter length = HCI raw packet length - HCI_PACKET_TYPE length - HCI_COMMAND_HDR length
	param_len = rawdatalen - (1 + HCI_COMMAND_HDR_SIZE);

	if (param_len)
		bdt_log("%s: No. of Cmd-params: %d", __func__, param_len);
	else
		goto send_cmd;

	/* Allocate memory for the command params */
	if (param_len)
	{
		bdt_log("%s: Sending CMD: 0x%x of param_len: %d", __func__, opcode, param_len);
		memcpy(buf, param_ptr, param_len);	
		status = (bt_status_t)sBtInterface->hci_cmd_send(opcode, param_ptr, param_len);
	} else {
send_cmd:
		bdt_log("%s: Sending CMD: 0x%x ", __func__, opcode);
		status = (bt_status_t)sBtInterface->hci_cmd_send(opcode, NULL, param_len);
	}

	return 0;
}

/*
 * Read an HCI raw data from the given device descriptor.
 *
 * return value: actual recived data length
 */
int bluedroid_read_raw_data(unsigned char *buf, int size)
{
	bdt_log("wait event...");
    sem_wait(&sem);

	size = raw_data_t.len;
	memcpy(buf,raw_data_t.raw_data,size);

	return size;
}

/*****************************************************************************
** Android's init.rc does not yet support applying linux capabilities
*****************************************************************************/
#ifdef PLATFORM_ANDROID
static void config_permissions(void)
{
    struct __user_cap_header_struct header;
    struct __user_cap_data_struct cap[2];

    bdt_log("set_aid_and_cap : pid %d, uid %d gid %d", getpid(), getuid(), getgid());

    header.pid = 0;

    prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0);

    setuid(AID_BLUETOOTH);
    setgid(AID_BLUETOOTH);

    header.version = _LINUX_CAPABILITY_VERSION_3;

    cap[CAP_TO_INDEX(CAP_NET_RAW)].permitted |= CAP_TO_MASK(CAP_NET_RAW);
    cap[CAP_TO_INDEX(CAP_NET_ADMIN)].permitted |= CAP_TO_MASK(CAP_NET_ADMIN);
    cap[CAP_TO_INDEX(CAP_NET_BIND_SERVICE)].permitted |= CAP_TO_MASK(CAP_NET_BIND_SERVICE);
    cap[CAP_TO_INDEX(CAP_SYS_RAWIO)].permitted |= CAP_TO_MASK(CAP_SYS_RAWIO);
    cap[CAP_TO_INDEX(CAP_SYS_NICE)].permitted |= CAP_TO_MASK(CAP_SYS_NICE);
    cap[CAP_TO_INDEX(CAP_SETGID)].permitted |= CAP_TO_MASK(CAP_SETGID);
    cap[CAP_TO_INDEX(CAP_WAKE_ALARM)].permitted |= CAP_TO_MASK(CAP_WAKE_ALARM);

    cap[CAP_TO_INDEX(CAP_NET_RAW)].effective |= CAP_TO_MASK(CAP_NET_RAW);
    cap[CAP_TO_INDEX(CAP_NET_ADMIN)].effective |= CAP_TO_MASK(CAP_NET_ADMIN);
    cap[CAP_TO_INDEX(CAP_NET_BIND_SERVICE)].effective |= CAP_TO_MASK(CAP_NET_BIND_SERVICE);
    cap[CAP_TO_INDEX(CAP_SYS_RAWIO)].effective |= CAP_TO_MASK(CAP_SYS_RAWIO);
    cap[CAP_TO_INDEX(CAP_SYS_NICE)].effective |= CAP_TO_MASK(CAP_SYS_NICE);
    cap[CAP_TO_INDEX(CAP_SETGID)].effective |= CAP_TO_MASK(CAP_SETGID);
    cap[CAP_TO_INDEX(CAP_WAKE_ALARM)].effective |= CAP_TO_MASK(CAP_WAKE_ALARM);

    capset(&header, &cap[0]);
    setgroups(sizeof(groups)/sizeof(groups[0]), groups);
}
#else
static void config_permissions(void)
{
    struct __user_cap_header_struct header;
    struct __user_cap_data_struct cap;

    bdt_log("set_aid_and_cap : pid %d, uid %d gid %d", getpid(), getuid(), getgid());

    header.pid = 0;

    prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0);

    setuid(AID_BLUETOOTH);
    setgid(AID_BLUETOOTH);

    header.version = _LINUX_CAPABILITY_VERSION;

    cap.effective = cap.permitted =  cap.inheritable =
                    1 << CAP_NET_RAW |
                    1 << CAP_NET_ADMIN |
                    1 << CAP_NET_BIND_SERVICE |
                    1 << CAP_SYS_RAWIO |
                    1 << CAP_SYS_NICE |
                    1 << CAP_SETGID;

    capset(&header, &cap);
    setgroups(sizeof(groups)/sizeof(groups[0]), groups);
}
#endif

int HAL_load(void)
{
    int err = 0;

    hw_module_t* module;
    hw_device_t* device;

    bdt_log("Loading HAL lib + extensions");

    err = hw_get_module(BT_HARDWARE_MODULE_ID, (hw_module_t const**)&module);
    if (err == 0)
    {
        err = module->methods->open(module, BT_HARDWARE_MODULE_ID, &device);
        if (err == 0) {
            bt_device = (bluetooth_device_t *)device;
            sBtInterface = bt_device->get_bluetooth_interface();
        }
    }

    bdt_log("HAL library loaded (%s)", strerror(err));

    return err;
}

int HAL_unload(void)
{
    int err = 0;

    bdt_log("Unloading HAL lib");

    sBtInterface = NULL;

    bdt_log("HAL library unloaded (%s)", strerror(err));

    return err;
}
static bt_callbacks_t bt_callbacks = {
    sizeof(bt_callbacks_t),
    adapter_state_changed,
    NULL, /* adapter_properties_cb */
    NULL, /* remote_device_properties_cb */
    NULL, /* device_found_cb */
    NULL, /* discovery_state_changed_cb */
    NULL, /* pin_request_cb  */
    NULL, /* ssp_request_cb  */
    NULL, /* bond_state_changed_cb */
    NULL, /* acl_state_changed_cb */
    NULL, /* thread_evt_cb */
    NULL, /* dut_mode_recv_cb */
    NULL, /* le_test_mode_cb */
    hci_event_recv, /* hci_event_recv_cb */
    NULL /* energy_info_cb */
};

#ifdef PLATFORM_ANDROID
static bool set_wake_alarm(uint64_t delay_millis, bool should_wake, alarm_cb cb, void *data) {
  static timer_t timer;
  static bool timer_created;

  if (!timer_created) {
    struct sigevent sigevent;
    memset(&sigevent, 0, sizeof(sigevent));
    sigevent.sigev_notify = SIGEV_THREAD;
    sigevent.sigev_notify_function = (void (*)(union sigval))cb;
    sigevent.sigev_value.sival_ptr = data;
    timer_create(CLOCK_MONOTONIC, &sigevent, &timer);
    timer_created = true;
  }

  struct itimerspec new_value;
  new_value.it_value.tv_sec = delay_millis / 1000;
  new_value.it_value.tv_nsec = (delay_millis % 1000) * 1000 * 1000;
  new_value.it_interval.tv_sec = 0;
  new_value.it_interval.tv_nsec = 0;
  timer_settime(timer, 0, &new_value, NULL);

  return true;
}

static int acquire_wake_lock(const char *lock_name) {
  return BT_STATUS_SUCCESS;
}

static int release_wake_lock(const char *lock_name) {
  return BT_STATUS_SUCCESS;
}

static bt_os_callouts_t callouts = {
    sizeof(bt_os_callouts_t),
    set_wake_alarm,
    acquire_wake_lock,
    release_wake_lock,
};
#endif

void bdt_init(void)
{
    bdt_log("INIT BT ");
    status = (bt_status_t)sBtInterface->init(&bt_callbacks);

#ifdef PLATFORM_ANDROID
    if (status == BT_STATUS_SUCCESS) {
        status = (bt_status_t)sBtInterface->set_os_callouts(&callouts);
    }
#endif

    check_return_status(status);
}

void bdt_enable(void)
{
    bdt_log("ENABLE BT");
    if (bt_enabled) {
        bdt_log("Bluetooth is already enabled");
        return;
    }
#ifdef ANDROID_M
    status = (bt_status_t)sBtInterface->enable(false);
#else
    status = (bt_status_t)sBtInterface->enable(false);
#endif

    check_return_status(status);
}

void bdt_disable(void)
{
    bdt_log("DISABLE BT");
    if (!bt_enabled) {
        bdt_log("Bluetooth is already disabled");
        return;
    }
    status = (bt_status_t)sBtInterface->disable();

    check_return_status(status);
}

int bluedroid_usb_connection(void)
{
	int opt;
	char cmd[128];
	int args_processed = 0;
	int pid = -1;
	int ret;

	ret = sem_init(&sem,0,0);
	if (ret != 0){
		perror("Semaphore initialization failed");
	}
	
	config_permissions();
	bdt_log("\n:::::::::::::::::::::::::::::::::::::::::::::::::::");
	bdt_log(":: Bluedroid test app starting");

	if ( HAL_load() < 0 ) {
		perror("HAL failed to initialize, exit\n");
		unlink(PID_FILE);
		exit(0);
	}

	/* Automatically perform the init,get interface*/
	bdt_init();

	/*initialize hardware,power on bluetooth,open dev/SS1BTUSB.
	* start read thread.
	*/
	bdt_enable();

	return 1;	
}

extern "C" void bdt_cleanup(void)
{
    bdt_log("CLEANUP");
    sBtInterface->cleanup();
}

extern "C" void bluedroid_clean(void)
{
	sem_destroy(&sem);
	bdt_cleanup();
	HAL_unload();
}
#endif

#ifdef BLUEZ_STACK
/* Get device descriptor */
int USBConnection(char *dev_name)
{
    int dd,dev_id;
	
    dev_id = hci_devid(dev_name);
	if (dev_id < 0) 
	{
		perror("Invalid device");
	    exit(-1);
	}
	
    if (dev_id < 0)
	 	dev_id = hci_get_route(NULL);	
	
	dd = hci_open_dev(dev_id);
	if (dd < 0) {
		perror("Device open failed");
	 	exit(-1);
	}
	
	return dd;
}

/*
 * Send an HCI command to (USB) device.
 *
 */
int usb_send_hci_cmd(char *dev_name, unsigned char *rawbuf, int rawdatalen)
{
    unsigned char buf[HCI_MAX_EVENT_SIZE];
	unsigned char *ptr = buf;
	unsigned char *param_ptr = NULL;
	struct hci_filter flt;
	hci_event_hdr *hdr;
	int i, opt, len, dd, dev_id;
	uint16_t ocf, opcode;
	uint8_t ogf;	
	
	dev_id = hci_devid(dev_name);
	if (dev_id < 0) 
	{
		perror("Invalid device");
		exit(EXIT_FAILURE);
	}
	
    if (dev_id < 0)
		dev_id = hci_get_route(NULL);	
	
	dd = hci_open_dev(dev_id);
	if (dd < 0) {
		perror("Device open failed");
		exit(EXIT_FAILURE);
	}
		
	/* calculate OGF and OCF: ogf is bit 10~15 of opcode, ocf is bit 0~9 of opcode */
	errno = 0;	
	opcode = rawbuf[1] | (rawbuf[2] << 8);
	ogf = opcode >> 10;
	ocf = opcode & 0x3ff;
	
	if (errno == ERANGE || (ogf > 0x3f) || (ocf > 0x3ff)) {
		printf("wrong input format\n");
		exit(EXIT_FAILURE);
	}
	
	/* copy parameter to buffer and calculate parameter total length */	
	param_ptr = rawbuf + (1 + HCI_COMMAND_HDR_SIZE);
	//parameter length = HCI raw packet length - HCI_PACKET_TYPE length - HCI_COMMAND_HDR length
	len = rawdatalen - (1 + HCI_COMMAND_HDR_SIZE);
	memcpy(buf, param_ptr, len);	
	
	/* Setup filter */
	hci_filter_clear(&flt);
	hci_filter_set_ptype(HCI_EVENT_PKT, &flt);
	hci_filter_all_events(&flt);
	if (setsockopt(dd, SOL_HCI, HCI_FILTER, &flt, sizeof(flt)) < 0) {
		perror("HCI filter setup failed");
		exit(EXIT_FAILURE);
	}

	if (hci_send_cmd(dd, ogf, ocf, len, buf) < 0) {
		perror("Send failed");
		exit(EXIT_FAILURE);
	}

	hci_close_dev(dd);
	return 0;
}

/*
 * Read an HCI raw data from the given device descriptor.
 *
 * return value: actual recived data length
 */
int read_raw_data(int dd, unsigned char *buf, int size)
{
    struct hci_filter flt;
	int ret;
	
    /* Setup filter */
	hci_filter_clear(&flt);
	hci_filter_set_ptype(HCI_EVENT_PKT, &flt);
	hci_filter_all_events(&flt);
	if (setsockopt(dd, SOL_HCI, HCI_FILTER, &flt, sizeof(flt)) < 0) {
		perror("HCI filter setup failed");
		exit(EXIT_FAILURE);
	}
	
	ret = read(dd, buf, size);
	return ret;
}
#endif/*BLUEZ_STACK*/

#ifdef BLUETOPIA_STACK

#define NO_COMMAND_ERROR                           (-1)  /* Denotes that no   */
                                                         /* command was       */
                                                         /* specified to the  */
                                                         /* parser.           */

#define INVALID_COMMAND_ERROR                      (-2)  /* Denotes that the  */
                                                         /* Command does not  */
                                                         /* exist for         */
                                                         /* processing.       */

#define EXIT_CODE                                  (-3)  /* Denotes that the  */
                                                         /* Command specified */
                                                         /* was the Exit      */
                                                         /* Command.          */

#define FUNCTION_ERROR                             (-4)  /* Denotes that an   */
                                                         /* error occurred in */
                                                         /* execution of the  */
                                                         /* Command Function. */

#define TO_MANY_PARAMS                             (-5)  /* Denotes that there*/
                                                         /* are more          */
                                                         /* parameters then   */
                                                         /* will fit in the   */
                                                         /* UserCommand.      */

#define INVALID_PARAMETERS_ERROR                   (-6)  /* Denotes that an   */
                                                         /* error occurred due*/
                                                         /* to the fact that  */
                                                         /* one or more of the*/
                                                         /* required          */
                                                         /* parameters were   */
                                                         /* invalid.          */

#define UNABLE_TO_INITIALIZE_STACK                 (-7)  /* Denotes that an   */
                                                         /* error occurred    */
                                                         /* while Initializing*/
                                                         /* the Bluetooth     */
                                                         /* Protocol Stack.   */

#define INVALID_STACK_ID_ERROR                     (-8)  /* Denotes that an   */
                                                         /* occurred due to   */
                                                         /* attempted         */
                                                         /* execution of a    */
                                                         /* Command when a    */
                                                         /* Bluetooth Protocol*/
                                                         /* Stack has not been*/
                                                         /* opened.           */
/*Define signal variable*/
sem_t sem_read_hci_event;

/*Save raw data from HCI event*/
struct raw_data_t{
	unsigned char data[HCI_EVENT_MAX_SIZE];
	int len;
};
struct raw_data_t hci_event_raw_data;

static unsigned int       BluetoothStackID;         /* Variable which holds the Handle */
                                                    /* of the opened Bluetooth Protocol*/
                                                    /* Stack.                          */
static char *HCIVersionStrings[] =
{
   "1.0b",
   "1.1",
   "1.2",
   "2.0",
   "2.1",
   "3.0",
   "4.0",
   "4.1",
   "4.2",
   "5.0",
   "Unknown (greater 5.0)"
} ;

#define NUM_SUPPORTED_HCI_VERSIONS     (sizeof(HCIVersionStrings)/sizeof(char *) - 1)

static void BTPSAPI HCI_Event_Callback(unsigned int BluetoothStackID, HCI_Event_Data_t *HCI_Event_Data, unsigned long CallbackParameter)
{
    printf("\r\nReceived one hci event\r\n");
}

static void BTPSAPI HCI_ACL_Data_Callback(unsigned int BluetoothStackID, Word_t Connection_Handle, Word_t Flags, Word_t ACLDataLength, Byte_t *ACLData, unsigned long CallbackParameter)
{
    printf("\r\nReceived %u from 0x%04X\r\n", ACLDataLength, Connection_Handle);
}

   /* The following function is responsible for opening the SS1         */
   /* Bluetooth Protocol Stack.  This function accepts a pre-populated  */
   /* HCI Driver Information structure that contains the HCI Driver     */
   /* Transport Information.  This function returns zero on successful  */
   /* execution and a negative value on all errors.                     */
static int OpenStack(HCI_DriverInformation_t *HCI_DriverInformation)
{
   int           Result;
   int           ret_val = 0;
   HCI_Version_t HCIVersion;

   /* First check to see if the Stack has already been opened.          */
   if(!BluetoothStackID)
   {
      /* Next, makes sure that the Driver Information passed appears to */
      /* be semi-valid.                                                 */
      if(HCI_DriverInformation)
      {
         /* Initialize the Stack                                        */
         Result = BSC_Initialize(HCI_DriverInformation, BSC_INITIALIZE_FLAG_NO_L2CAP | BSC_INITIALIZE_FLAG_NO_SCO | BSC_INITIALIZE_FLAG_NO_SDP | BSC_INITIALIZE_FLAG_NO_RFCOMM | BSC_INITIALIZE_FLAG_NO_GAP | BSC_INITIALIZE_FLAG_NO_SPP | BSC_INITIALIZE_FLAG_NO_GOEP | BSC_INITIALIZE_FLAG_NO_OTP);

         /* Next, check the return value of the initialization to see if*/
         /* it was successful.                                          */
         if(Result > 0)
         {
            /* The Stack was initialized successfully, inform the user  */
            /* and set the return value of the initialization function  */
            /* to the Bluetooth Stack ID.                               */
            if(HCI_DriverInformation->DriverType == hdtUSB)
               printf("Stack Initialization on USB Successful.\r\n");
            else
               printf("Stack Initialization on not USB!!!\r\n");

            BluetoothStackID         = Result;

            if(!HCI_Version_Supported(BluetoothStackID, &HCIVersion))
            {
               printf("Device Chipset Version: %s\r\n", (HCIVersion <= NUM_SUPPORTED_HCI_VERSIONS)?HCIVersionStrings[HCIVersion]:HCIVersionStrings[NUM_SUPPORTED_HCI_VERSIONS]);
            }
#if 0
            /* Now that we have initialized the Bluetooth Stack ID, we  */
            /* need to initialize all other variables so that they      */
            /* flag that we have found no devices on Inquiry, no device */
            /* has attempted to connect to us, and there are currently  */
            /* no outstanding connections (ACL and/or SCO).             */
            NumberofValidResponses   = 0;

            ConnectionRequestIndexID = -1;

            DebugID                  = 0;

            ACLConnectionHandle      = HCI_CONNECTION_HANDLE_INVALID_VALUE;
            SCOConnectionHandle      = HCI_CONNECTION_HANDLE_INVALID_VALUE;
#endif
            /* Next, let's register for HCI Event and HCI ACL Data      */
            /* Callbacks.                                               */
            Result = HCI_Register_Event_Callback(BluetoothStackID, HCI_Event_Callback, 0);

            if(Result >= 0)
            {
               printf("Return Value is %d HCI_Register_Event_Callback() SUCCESS.\r\n", Result);

               Result = HCI_Register_ACL_Data_Callback(BluetoothStackID, HCI_ACL_Data_Callback, 0);

               if(Result >= 0)
                  printf("Return Value is %d HCI_Register_ACL_Data_Callback() SUCCESS.\r\n", Result);
               else
               {
                  printf("Return Value is %d HCI_Register_ACL_Data_Callback() FAILURE.\r\n", Result);

                  ret_val = UNABLE_TO_INITIALIZE_STACK;
               }
            }
            else
            {
               printf("Return Value is %d HCI_Register_Event_Callback() FAILURE.\r\n", Result);

               ret_val = UNABLE_TO_INITIALIZE_STACK;
            }
         }
         else
         {
            /* The Stack was NOT initialized successfully, inform the   */
            /* user and set the return value of the initialization      */
            /* function to an error.                                    */
            if(HCI_DriverInformation->DriverType == hdtUSB)
               printf("Stack Initialization on USB Failed: %d.\r\n", Result);
            else
            {
               printf("Stack Initialization on not USB \r\n");
            }

            BluetoothStackID = 0;

            ret_val          = UNABLE_TO_INITIALIZE_STACK;
         }
      }
      else
      {
         /* One or more of the necessary parameters are invalid.        */
         ret_val = INVALID_PARAMETERS_ERROR;
      }
   }
   else
   {
      /* A valid Stack ID already exists, inform to user.               */
      printf("Stack Already Initialized.\r\n");
   }

   return(ret_val);
}

   /* The following function is responsible for closing the SS1         */
   /* Bluetooth Protocol Stack.  This function requires that the        */
   /* Bluetooth Protocol stack previously have been initialized via the */
   /* OpenStack() function.  This function returns zero on successful   */
   /* execution and a negative value on all errors.                     */
int CloseStack(void)
{
    int ret_val;

    /* First check to see if the Stack has been opened.                  */
    if(BluetoothStackID)
    {
        /* Simply close the Stack                                         */
        BSC_Shutdown(BluetoothStackID);

        printf("Stack Shutdown Successfully.\r\n");

        /* Flag that the Stack is no longer initialized.                  */
        BluetoothStackID = 0;

        /* Flag success to the caller.                                    */
        ret_val          = 0;
    }
    else
    {
        /* A valid Stack ID does not exist, inform to user.               */
        printf("Stack not Initialized.\r\n");

        ret_val = UNABLE_TO_INITIALIZE_STACK;
    }

    sem_destroy(&sem_read_hci_event);
    return(ret_val);
}


/* Get device descriptor */
int USBConnection(char *dev_name)
{
    int ret=1;
    HCI_DriverInformation_t  HCI_DriverInformation;
    HCI_DriverInformation_t *HCI_DriverInformationPtr;

	
	if(sem_init(&sem_read_hci_event,0,0) != 0)
		perror("Semaphore initialization failed");

    /* The Transport selected was USB, setup the Driver         */
    /* Information Structure to use USB as the HCI Transport.   */
    HCI_DRIVER_SET_USB_INFORMATION(&HCI_DriverInformation);

    HCI_DriverInformationPtr = &HCI_DriverInformation;

    /* Check to see if the HCI_Driver Information Strucuture was      */
    /* successfully setup.                                            */
    if(HCI_DriverInformationPtr)
    {
        /* Try to Open the stack and check if it was successful.       */
        if(!OpenStack(HCI_DriverInformationPtr))
        {
            /* The stack was opened successfully.  Next, simply open the*/
            /* User Interface and start the user interaction.           */

            /* Start the User Interface.                                */
            //UserInterface();
 
            printf("Open the stack succesfully.\r\n");
        }
        else
        {
            /* There was an error while attempting to open the Stack.   */
            printf("Unable to open the stack.\r\n");
            ret = -1;
        }
    }
	
	return ret;
}

/*
 * Send an HCI command to (USB) device.
 *
 */
int usb_send_hci_cmd(char *dev_name, unsigned char *rawbuf, int rawdatalen)
{
	unsigned char buf[HCI_EVENT_MAX_SIZE];
	unsigned char *ptr = buf;
	unsigned char *param_ptr = NULL;
	int i, ret_val;
	unsigned char len_hdr, len_cmd, len_evt=255;
	unsigned short ocf, opcode;
	unsigned char ogf, status;
	
	memset(hci_event_raw_data.data,0,sizeof(hci_event_raw_data.data));
		
	/* calculate OGF and OCF: ogf is bit 10~15 of opcode, ocf is bit 0~9 of opcode */
	errno = 0;	
	opcode = rawbuf[1] | (rawbuf[2] << 8);
	ogf = opcode >> 10;
	ocf = opcode & 0x3ff;
	
	if (errno == ERANGE || (ogf > 0x3f) || (ocf > 0x3ff)) {
		printf("wrong input format\n");
		exit(EXIT_FAILURE);
	}
	
	/* copy parameter to buffer and calculate parameter total length */	
	param_ptr = rawbuf + (1 + HCI_COMMAND_HEADER_SIZE);
	len_cmd = rawdatalen - (1 + HCI_COMMAND_HEADER_SIZE);
	memcpy(buf, param_ptr, len_cmd);	
#if 0
    printf("hci cmd:\n");
	for(i = 0; i < rawdatalen; i++){
		printf("%02x ",rawbuf[i]);
    }
#endif

    /* Send the Vendor Specific command to the Controller.            */
    ret_val = HCI_Send_Raw_Command(BluetoothStackID, ogf, ocf, len_cmd, buf, &status, &len_evt, buf, TRUE);
    //printf("\nHCI_Send_Raw_Command ret %d, len_evt %d, buf[0] %02x\n", ret_val, len_evt, buf[0]);
    if((ret_val) || ((len_evt) && (buf[0])))
       ret_val = -1;
    
    //printf("\nhci event: ret_val %d\n", ret_val);
    if (ret_val < 0) {
		perror("Send failed");
		exit(EXIT_FAILURE);
	}

	len_hdr = 1/*hci packet type*/ + HCI_EVENT_HEADER_SIZE + 1 + sizeof(opcode);
	hci_event_raw_data.data[0] = ptHCIEventPacket;
	hci_event_raw_data.data[1] = HCI_EVENT_CODE_COMMAND_COMPLETE;
	hci_event_raw_data.data[2] = 1 + sizeof(opcode) + len_evt;
	hci_event_raw_data.data[3] = ptHCICommandPacket;
	hci_event_raw_data.data[4] = opcode & 0xff;
	hci_event_raw_data.data[5] = (opcode & 0xff00) >> 8;
	for(i = 0; i < len_evt; i++)
        hci_event_raw_data.data[len_hdr+i] = buf[i];
	hci_event_raw_data.len = len_hdr + len_evt;
#if 0
	for(i = 0; i < hci_event_raw_data.len; i++)
		printf("%02x ",hci_event_raw_data.data[i]);
    printf("\n");
#endif

	/*Send semaphore to thread Notifyobserver,event is update now.*/
	sem_post(&sem_read_hci_event);
	return 0;
}

/*
 * Read an HCI raw data from the given device descriptor.
 *
 * return value: actual recived data length
 */
int read_raw_data(int dd, unsigned char *buf, int size)
{
	printf("wait event...\n");
    sem_wait(&sem_read_hci_event);

	size = hci_event_raw_data.len;
	memcpy(buf, hci_event_raw_data.data, size);

	return size;
}
#endif/*BLUETOPIA_STACK*/

#ifdef BT_HCI_UART 
int USBConnection(char *dev_name)
{
    printf("Dummy function for UART interface");
    return 0;
}
int usb_send_hci_cmd(char *dev_name, unsigned char *rawbuf, int rawdatalen)
{
    printf("Dummy function for UART interface");
    return 0;
}
int read_raw_data(int dd, unsigned char *buf, int size)
{
    printf("Dummy function for UART interface");
    return 0;
}
#endif/*BT_HCI_UART */


/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 * Copyright 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/******************************************************************************
 *
 *  Filename:      userial_vendor.c
 *
 *  Description:   Contains vendor-specific userial functions
 *
 ******************************************************************************/

#define LOG_TAG "bt_vendor"

#include <sys/socket.h>
#ifdef ANDROID
#include <utils/Log.h>
#else
#include <stdint.h>
#include <stdio.h>
#include <string.h>
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

#include <stdlib.h>
//#include <termios.h>
#ifndef ANDROID
#include <sys/termios.h>
#endif
#include <limits.h>
#include <stdbool.h>
#include <string.h>
#include <sys/ioctl.h>

//#include "bt_vendor_qcom.h"
#include <string.h>

#include "hci_uart.h"
#include "hw_rome.h"

#if 1
#include <syslog.h>
#define ALOGV(fmt, arg...) syslog(LOG_ERR, LOG_TAG fmt, ##arg)
#define ALOGD(fmt, arg...) syslog(LOG_ERR, LOG_TAG fmt, ##arg)
#define ALOGI(fmt, arg...) syslog(LOG_ERR, LOG_TAG fmt, ##arg)
#define ALOGW(fmt, arg...) syslog(LOG_ERR, LOG_TAG fmt, ##arg)
#define ALOGE(fmt, arg...) syslog(LOG_ERR, LOG_TAG fmt, ##arg)
#else
#if 0
#define ALOGV(fmt, arg...) \
  do {                     \
    printf(fmt, ##arg);    \
    printf("\n");          \
  } while (0)
#define ALOGD(fmt, arg...) \
  do {                     \
    printf(fmt, ##arg);    \
    printf("\n");          \
  } while (0)
#define ALOGI(fmt, arg...) \
  do {                     \
    printf(fmt, ##arg);    \
    printf("\n");          \
  } while (0)
#define ALOGW(fmt, arg...) \
  do {                     \
    printf(fmt, ##arg);    \
    printf("\n");          \
  } while (0)
#define ALOGE(fmt, arg...) \
  do {                     \
    printf(fmt, ##arg);    \
    printf("\n");          \
  } while (0)
#else
#define ALOGV(fmt, arg...) printf(fmt, ##arg)
#define ALOGD(fmt, arg...) printf(fmt, ##arg)
#define ALOGI(fmt, arg...) printf(fmt, ##arg)
#define ALOGW(fmt, arg...) printf(fmt, ##arg)
#define ALOGE(fmt, arg...) printf(fmt, ##arg)

#endif
#endif

/******************************************************************************
**  Constants & Macros
******************************************************************************/

#ifndef VNDUSERIAL_DBG
#define VNDUSERIAL_DBG TRUE
#endif

#if (VNDUSERIAL_DBG == TRUE)
#define VNDUSERIALDBG(param, ...) \
  { ALOGI(param, ##__VA_ARGS__); }
#else
#define VNDUSERIALDBG(param, ...) \
  {}
#endif

#define RESERVED(p) \
  if (p) ALOGI("%s: reserved param", __FUNCTION__);
size_t strlcpy(char *dst, const char *src, size_t size) {
  const char *s = src;
  size_t n = size;

  if (n && --n) do {
      if (!(*dst++ = *src++)) break;
    } while (--n);

  if (!n) {
    if (size) *dst = '\0';
    while (*src++)
      ;
  }
  return src - s - 1;
}
/******************************************************************************
**  Global variables
******************************************************************************/
vnd_userial_cb_t vnd_userial;
int unified_hci;
/*****************************************************************************
**   Functions
*****************************************************************************/

/*******************************************************************************
**
** Function        userial_to_tcio_baud
**
** Description     helper function converts USERIAL baud rates into TCIO
**                  conforming baud rates
**
** Returns         TRUE/FALSE
**
*******************************************************************************/
uint8_t userial_to_tcio_baud(uint8_t cfg_baud, uint32_t *baud) {
  if (cfg_baud == USERIAL_BAUD_115200)
    *baud = B115200;
  else if (cfg_baud == USERIAL_BAUD_4M)
    *baud = B4000000;
  else if (cfg_baud == USERIAL_BAUD_3M)
    *baud = B3000000;
  else if (cfg_baud == USERIAL_BAUD_2M)
    *baud = B2000000;
  else if (cfg_baud == USERIAL_BAUD_1M)
    *baud = B1000000;
  else if (cfg_baud == USERIAL_BAUD_921600)
    *baud = B921600;
  else if (cfg_baud == USERIAL_BAUD_460800)
    *baud = B460800;
  else if (cfg_baud == USERIAL_BAUD_230400)
    *baud = B230400;
  else if (cfg_baud == USERIAL_BAUD_57600)
    *baud = B57600;
  else if (cfg_baud == USERIAL_BAUD_19200)
    *baud = B19200;
  else if (cfg_baud == USERIAL_BAUD_9600)
    *baud = B9600;
  else if (cfg_baud == USERIAL_BAUD_1200)
    *baud = B1200;
  else if (cfg_baud == USERIAL_BAUD_600)
    *baud = B600;
  else {
    ALOGE("userial vendor open: unsupported baud idx %i", cfg_baud);
    *baud = B115200;
    return FALSE;
  }

  return TRUE;
}

/*******************************************************************************
**
** Function        userial_to_baud_tcio
**
** Description     helper function converts TCIO baud rate into integer
**
** Returns         uint32_t
**
*******************************************************************************/
int userial_tcio_baud_to_int(uint32_t baud) {
  int baud_rate = 0;

  switch (baud) {
    case B600:
      baud_rate = 600;
      break;
    case B1200:
      baud_rate = 1200;
      break;
    case B9600:
      baud_rate = 9600;
      break;
    case B19200:
      baud_rate = 19200;
      break;
    case B57600:
      baud_rate = 57600;
      break;
    case B115200:
      baud_rate = 115200;
      break;
    case B230400:
      baud_rate = 230400;
      break;
    case B460800:
      baud_rate = 460800;
      break;
    case B921600:
      baud_rate = 921600;
      break;
    case B1000000:
      baud_rate = 1000000;
      break;
    case B2000000:
      baud_rate = 2000000;
      break;
    case B3000000:
      baud_rate = 3000000;
      break;
    case B4000000:
      baud_rate = 4000000;
      break;
    default:
      ALOGE("%s: unsupported baud %d", __FUNCTION__, baud);
      break;
  }

  ALOGI("%s: Current Baudrate = %d bps", __FUNCTION__, baud_rate);
  return baud_rate;
}

#if (BT_WAKE_VIA_USERIAL_IOCTL == TRUE)
/*******************************************************************************
**
** Function        userial_ioctl_init_bt_wake
**
** Description     helper function to set the open state of the bt_wake if ioctl
**                  is used. it should not hurt in the rfkill case but it might
**                  be better to compile it out.
**
** Returns         none
**
*******************************************************************************/
void userial_ioctl_init_bt_wake(int fd) {
  uint32_t bt_wake_state;

  /* assert BT_WAKE through ioctl */
  ioctl(fd, USERIAL_IOCTL_BT_WAKE_ASSERT, NULL);
  ioctl(fd, USERIAL_IOCTL_BT_WAKE_GET_ST, &bt_wake_state);
  VNDUSERIALDBG("userial_ioctl_init_bt_wake read back BT_WAKE state=%i",
                bt_wake_state);
}
#endif  // (BT_WAKE_VIA_USERIAL_IOCTL==TRUE)

/*****************************************************************************
**   Userial Vendor API Functions
*****************************************************************************/

/*******************************************************************************
**
** Function        userial_vendor_init
**
** Description     Initialize userial vendor-specific control block
**
** Returns         None
**
*******************************************************************************/
void userial_vendor_init(char *uart_device) {
  vnd_userial.fd = -1;
  if (uart_device != NULL) {
    snprintf(vnd_userial.port_name, VND_PORT_NAME_MAXLEN, "%s", uart_device);
  } else {
    snprintf(vnd_userial.port_name, VND_PORT_NAME_MAXLEN, "%s",
             BT_HS_UART_DEVICE);
  }
}

/*******************************************************************************
**
** Function        userial_vendor_open
**
** Description     Open the serial port with the given configuration
**
** Returns         device fd
**
*******************************************************************************/
int userial_vendor_open(tUSERIAL_CFG *p_cfg) {
  uint32_t baud;
  uint8_t data_bits;
  uint16_t parity;
  uint8_t stop_bits;
  int ret;

  vnd_userial.fd = -1;

  if (!userial_to_tcio_baud(p_cfg->baud, &baud)) {
    return -1;
  }

  if (p_cfg->fmt & USERIAL_DATABITS_8)
    data_bits = CS8;
  else if (p_cfg->fmt & USERIAL_DATABITS_7)
    data_bits = CS7;
  else if (p_cfg->fmt & USERIAL_DATABITS_6)
    data_bits = CS6;
  else if (p_cfg->fmt & USERIAL_DATABITS_5)
    data_bits = CS5;
  else {
    ALOGE("userial vendor open: unsupported data bits");
    return -1;
  }

  if (p_cfg->fmt & USERIAL_PARITY_NONE)
    parity = 0;
  else if (p_cfg->fmt & USERIAL_PARITY_EVEN)
    parity = PARENB;
  else if (p_cfg->fmt & USERIAL_PARITY_ODD)
    parity = (PARENB | PARODD);
  else {
    ALOGE("userial vendor open: unsupported parity bit mode");
    return -1;
  }

  if (p_cfg->fmt & USERIAL_STOPBITS_1)
    stop_bits = 0;
  else if (p_cfg->fmt & USERIAL_STOPBITS_2)
    stop_bits = CSTOPB;
  else {
    ALOGE("userial vendor open: unsupported stop bits");
    return -1;
  }

  ALOGI("userial vendor open: opening %s", vnd_userial.port_name);

  if ((vnd_userial.fd = open(vnd_userial.port_name, O_RDWR | O_NOCTTY)) == -1) {
    ALOGE("userial vendor open: unable to open %s: %s(%d)",
          vnd_userial.port_name, strerror(errno), errno);
    return -1;
  }
  ALOGI("%s: open(%s, ...) = %d", __func__, vnd_userial.port_name,
        vnd_userial.fd);

  ret = tcflush(vnd_userial.fd, TCIOFLUSH);
  ALOGI("%s: tcflush(%d, ...) = %d", __func__, vnd_userial.fd, ret);

  ret = tcgetattr(vnd_userial.fd, &vnd_userial.termios);
  ALOGI("%s: tcgetattr(%d, ...) = %d", __func__, vnd_userial.fd, ret);
  cfmakeraw(&vnd_userial.termios);

  /* Set UART Control Modes */
  vnd_userial.termios.c_cflag |= CLOCAL;
  vnd_userial.termios.c_cflag |= (CRTSCTS | stop_bits);
  //vnd_userial.termios.c_cflag &= ~CRTSCTS;

  ret = tcsetattr(vnd_userial.fd, TCSANOW, &vnd_userial.termios);
  ALOGI("%s: tcsetattr(%d, ...) = %d", __func__, vnd_userial.fd, ret);

  /* set input/output baudrate */
  cfsetospeed(&vnd_userial.termios, baud);
  cfsetispeed(&vnd_userial.termios, baud);
  ret = tcsetattr(vnd_userial.fd, TCSANOW, &vnd_userial.termios);
  ALOGI("%s: 2nd tcsetattr(%d, ...) = %d", __func__, vnd_userial.fd, ret);

  ret = tcflush(vnd_userial.fd, TCIOFLUSH);
  ALOGI("%s: 2nd tcflush(%d, ...) = %d", __func__, vnd_userial.fd, ret);

#if (BT_WAKE_VIA_USERIAL_IOCTL == TRUE)
  userial_ioctl_init_bt_wake(vnd_userial.fd);
#endif

  ALOGI("device fd = %d open", vnd_userial.fd);

  return vnd_userial.fd;
}

/*******************************************************************************
**
** Function        userial_vendor_close
**
** Description     Conduct vendor-specific close work
**
** Returns         None
**
*******************************************************************************/
void userial_vendor_close(void) {
  int result;

  if (vnd_userial.fd == -1) return;

#if (BT_WAKE_VIA_USERIAL_IOCTL == TRUE)
  /* de-assert bt_wake BEFORE closing port */
  ioctl(vnd_userial.fd, USERIAL_IOCTL_BT_WAKE_DEASSERT, NULL);
#endif

  ALOGI("device fd = %d close", vnd_userial.fd);

  if ((result = close(vnd_userial.fd)) < 0)
    ALOGE("close(fd:%d) FAILED result:%d", vnd_userial.fd, result);

  vnd_userial.fd = -1;
}

/*******************************************************************************
**
** Function        userial_vendor_set_baud
**
** Description     Set new baud rate
**
** Returns         None
**
*******************************************************************************/
void userial_vendor_set_baud(uint8_t userial_baud) {
  uint32_t tcio_baud;

  VNDUSERIALDBG("## userial_vendor_set_baud: %d", userial_baud);

  userial_to_tcio_baud(userial_baud, &tcio_baud);

  cfsetospeed(&vnd_userial.termios, tcio_baud);
  cfsetispeed(&vnd_userial.termios, tcio_baud);
  tcsetattr(
      vnd_userial.fd, TCSADRAIN,
      &vnd_userial.termios); /* don't change speed until last write done */
  //    tcflush(vnd_userial.fd, TCIOFLUSH);
}

/*******************************************************************************
**
** Function        userial_vendor_get_baud
**
** Description     Get current baud rate
**
** Returns         int
**
*******************************************************************************/
int userial_vendor_get_baud(void) {
  if (vnd_userial.fd == -1) {
    ALOGE("%s: uart port(%s) has not been opened", __FUNCTION__,
          BT_HS_UART_DEVICE);
    return -1;
  }

  return userial_tcio_baud_to_int(cfgetispeed(&vnd_userial.termios));
}

/*******************************************************************************
**
** Function        userial_vendor_ioctl
**
** Description     ioctl inteface
**
** Returns         None
**
*******************************************************************************/
int userial_vendor_ioctl(userial_vendor_ioctl_op_t op, int *p_data) {
  int err = -1;

  switch (op) {
#if (BT_WAKE_VIA_USERIAL_IOCTL == TRUE)
    case USERIAL_OP_ASSERT_BT_WAKE:
      VNDUSERIALDBG("## userial_vendor_ioctl: Asserting BT_Wake ##");
      err = ioctl(vnd_userial.fd, USERIAL_IOCTL_BT_WAKE_ASSERT, NULL);
      break;

    case USERIAL_OP_DEASSERT_BT_WAKE:
      VNDUSERIALDBG("## userial_vendor_ioctl: De-asserting BT_Wake ##");
      err = ioctl(vnd_userial.fd, USERIAL_IOCTL_BT_WAKE_DEASSERT, NULL);
      break;

    case USERIAL_OP_GET_BT_WAKE_STATE:
      err = ioctl(vnd_userial.fd, USERIAL_IOCTL_BT_WAKE_GET_ST, p_data);
      break;
#endif  //  (BT_WAKE_VIA_USERIAL_IOCTL==TRUE)
    case USERIAL_OP_FLOW_ON:
      ALOGI("## userial_vendor_ioctl: UART Flow On ");
      *p_data |= TIOCM_RTS;
      err = ioctl(vnd_userial.fd, TIOCMSET, p_data);
      break;

    case USERIAL_OP_FLOW_OFF:
      ALOGI("## userial_vendor_ioctl: UART Flow Off ");
      ioctl(vnd_userial.fd, TIOCMGET, p_data);
      *p_data &= ~TIOCM_RTS;
      err = ioctl(vnd_userial.fd, TIOCMSET, p_data);
      break;

    default:
      break;
  }

  return err;
}

/*******************************************************************************
**
** Function        userial_set_port
**
** Description     Configure UART port name
**
** Returns         0 : Success
**                 Otherwise : Fail
**
*******************************************************************************/
int userial_set_port(char *p_conf_name, char *p_conf_value, int param) {
  int len;
  RESERVED(p_conf_name);
  RESERVED(param);
  len = strlcpy(vnd_userial.port_name, p_conf_value, VND_PORT_NAME_MAXLEN);
  if (len >= VND_PORT_NAME_MAXLEN) {
    ALOGE("source string is too long\n");
    return -1;
  }
  return 0;
}

/*******************************************************************************
**
** Function        read_hci_event
**
** Description     Read HCI event during vendor initialization
**
** Returns         int: size to read
**
*******************************************************************************/
int read_hci_event(int fd, unsigned char *buf, int size) {
  int remain, r;
  int count = 0;
  unsigned char wake_byte;
  unsigned short int opcode;

  if (size <= 0) {
    ALOGE("Invalid size arguement!");
    return -1;
  }

  ALOGI("%s: Wait for Command Compete Event from SOC", __FUNCTION__);

  /* The first byte identifies the packet type. For HCI event packets, it
   * should be 0x04, so we read until we get to the 0x04. */
  while (1) {
    r = read(fd, buf, 1);
    if (r <= 0) return -1;
    if (buf[0] == 0x04) break;
#if 0				
	    if (!is_controller_usb_intf()) {
		    if (buf[0] == 0xFD) {
			    ALOGI("%s: Got FD , responding with FC", __func__);
			    wake_byte = 0xFC;
			    write(fd, &wake_byte, 1);
		    }
	    }
#endif
  }
  count++;

  /* The next two bytes are the event code and parameter total length. */
  while (count < 3) {
    r = read(fd, buf + count, 3 - count);
    if (r <= 0) return -1;
    count += r;
  }
  /* Now we read the parameters. */
  if (buf[2] < (size - 3))
    remain = buf[2];
  else
    remain = size - 3;

  while ((count - 3) < remain) {
    r = read(fd, buf + count, remain - (count - 3));
    if (r <= 0) return -1;
    count += r;
  }

  if (unified_hci && (buf[EVENTCODE_OFFSET] == VSEVENT_CODE)) {
    ALOGE("%s: Unexpected event recieved rather than CC", __func__);
    return 0;
  }

  if (buf[1] == VSEVENT_CODE) {
    /*Handle controller log function*/
  } else if (buf[1] == EVT_CMD_COMPLETE) {
    ALOGD("%s: Expected CC", __func__);
    if (count > UNIFIED_HCI_CC_MIN_LENGTH) {
      opcode = (buf[4] | (buf[5] << 8));
      if (((HCI_VS_WIPOWER_CMD_OPCODE == opcode) &&
           (UNIFIED_HCI_CODE == buf[6])) ||
          ((HCI_VS_GET_VER_CMD_OPCODE == opcode) &&
           (buf[7] == EDL_PATCH_VER_REQ_CMD))) {
        unified_hci = 1;
        ALOGI("HCI Unified command interface supported");
      }
    }
    if (unified_hci) {
      get_vs_hci_event(buf);
    }
  } else {
    ALOGE("%s: Unexpected event : protocol byte: %d", __func__, buf[1]);
    count = -1;
  }
  return count;
}

int read_new_hci_event(int fd, unsigned char *buf, int size) {
  int remain, r;
  int count = 0;
  unsigned char wake_byte;

  if (size <= 0) {
    ALOGE("Invalid size arguement!");
    return -1;
  }

  ALOGI("%s: Wait for Command Compete Event from SOC", __FUNCTION__);

  /* The first byte identifies the packet type. For HCI event packets, it
   * should be 0x04, so we read until we get to the 0x04. */
  while (1) {
    r = read(fd, buf, 1);
    if (r <= 0) return -1;
    if (buf[0] == 0x04) break;
#if 0				
	    if (!is_controller_usb_intf()) {
		    if (buf[0] == 0xFD) {
			    ALOGI("%s: Got FD , responding with FC", __func__);
			    wake_byte = 0xFC;
			    write(fd, &wake_byte, 1);
		    }
	    }
#endif
  }
  count++;

  /* The next two bytes are the event code and parameter total length. */
  while (count < 3) {
    r = read(fd, buf + count, 3 - count);
    if (r <= 0) return -1;
    count += r;
  }
  /* Now we read the parameters. */
  if (buf[2] < (size - 3))
    remain = buf[2];
  else
    remain = size - 3;

  while ((count - 3) < remain) {
    r = read(fd, buf + count, remain - (count - 3));
    if (r <= 0) return -1;
    count += r;
  }
  return count;
}

int read_cmd_compl_event(int fd, unsigned char *buf, int size) {
  int tot_len = -1;
  do {
    tot_len = read_new_hci_event(fd, buf, size);
    if (tot_len < 0) {
      ALOGE("%s: Error while reading the hci event", __func__);
      break;
    }

    if (buf[1] == EVT_CMD_COMPLETE) {
      ALOGD("%s: Cmd Cmpl received for opcode %0x", __func__,
            (buf[4] | (buf[5] << 8)));
      break;
    } else {
      ALOGE("%s: Unexpected event %0x received", __func__, buf[1]);
    }
  } while (buf[1] != EVT_CMD_COMPLETE);

  return tot_len;
}

int userial_clock_operation(int fd, int cmd) {
  int ret = 0;

  switch (cmd) {
    case USERIAL_OP_CLK_ON:
    case USERIAL_OP_CLK_OFF:
      ioctl(fd, cmd);
      break;
    case USERIAL_OP_CLK_STATE:
      ret = ioctl(fd, cmd);
      break;
  }

  return ret;
}

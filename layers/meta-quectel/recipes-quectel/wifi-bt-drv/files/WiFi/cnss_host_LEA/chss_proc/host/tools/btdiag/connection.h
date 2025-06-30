/* 
Copyright (c) 2013-2020 Qualcomm Technologies, Inc.
All Rights Reserved. 
Confidential and Proprietary - Qualcomm Technologies, Inc.
*/
#include <termios.h>

#define HCI_UART_H4	            0

#define HCI_UART_RAW_DEVICE	    0

/*
* Add delay 200ms after hci cmd sent to assure event be arrived.
*/
//#define HCI_CMD_NEED_DELAY
#define HCI_CMD_DELAY_TIME     200000

#ifdef __cplusplus
extern "C"
{
#endif
int read_hci_event(int fd, unsigned char* buf, int size);
int qca_soc_init(int fd, int speed, char *bdaddr);
int fluoride_usb_connection(char *dev);
int fluoride_send_hci_cmd(int fd, unsigned char *rawbuf, int rawdatalen);
int fluoride_read_raw_data(int fd, unsigned char *buf, int size);

#ifdef __cplusplus
}
#endif

#ifdef BLUEZ_STACK
#include <stdint.h>
/* BD Address */
/*
typedef struct {
	uint8_t b[6];
} __attribute__((packed)) bdaddr_t;
*/
struct hci_filter {
	uint32_t type_mask;
	uint32_t event_mask[2];
	uint16_t opcode;
};
#endif
#ifdef BLUEDROID_STACK
#ifdef __cplusplus
extern "C"
{
#endif
void bdt_cleanup(void);
void bluedroid_clean(void);
int bluedroid_usb_connection(void);
int bluedroid_read_raw_data(unsigned char *buf, int size);
int bluedroid_usb_send_hci_cmd(unsigned char *rawbuf, int rawdatalen);
int fluoride_disable_controller_log(int fd);
#ifdef __cplusplus
}
#endif
#endif

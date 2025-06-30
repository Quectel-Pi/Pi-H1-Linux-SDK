/* 
Copyright (c) 2013-2020 Qualcomm Technologies, Inc.
All Rights Reserved. 
Confidential and Proprietary - Qualcomm Technologies, Inc.
*/  

/* uart.h - header file for uart.c */

#ifndef _UART_H_
#define _UART_H_

#include <stdio.h>
#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <limits.h>     //PATH_MAX
#include <time.h>
#include <string.h>

#define NO_BLOCK_MODE		O_NONBLOCK
#define BLOCK_MODE		(!O_NONBLOCK)

#define UART_DEVICE_BAUDRATE_DEFAULT		"115200"
#define UART_DEVICE_NAME		"/dev/ttyUSB0"
#define UARTRXBUFFER	4096
#define UARTTXBUFFER	512
#define MBUFFER			2048*2
#define DIAG_TERMINAL_CHAR		0x7E



//#define UART_DEBUG
/*
typedef enum 
{
    SERIAL,
    ETHERNET,
    INVALID
} ConnectionType;
*/
typedef	enum
{
	IPADDRESS  = 1,
	PORT	   = 2,
	IOTYPE	   = 3,
	DEVICE	   = 4,
	BAUDRATE   = 5,
}ParameterType;

struct uart_t {
	char device[PATH_MAX];
	int  baudrate;
	int fd;
	int rx_len;
	char rx_buffer[UARTRXBUFFER];
	char tx_buffer[UARTTXBUFFER];
};



// ----------------------------------------------------------------

int SetUartParameter(struct uart_t *u, char *p, ParameterType which);
int InitUartPort(const struct uart_t *u);
int UartRead(struct uart_t* pUartInfo, char *buf, int len);
int UartWrite(struct uart_t* pUartInfo, const char *buf, int len);

#endif //_Qcmbr_H_


/* 
Copyright (c) 2013-2020 Qualcomm Technologies, Inc.
All Rights Reserved. 
Confidential and Proprietary - Qualcomm Technologies, Inc.
*/  

/* uart.c - uart function */


#include "uart.h"

struct uart_t uart_port ={0};

/* 
*	Only one to be DEFINE
*	Default set to BLOCK_MODE
*/
//#define WHICH_MODE NO_BLOCK_MODE
#define WHICH_MODE BLOCK_MODE



int SetUartParameter(struct uart_t *u, char *p, ParameterType which)
{

	if (u == NULL){
		printf("uart: uart_t is null\n");
		exit(1);
	}

	if (p == NULL){
		printf("uart: Parameter is null\n");
		exit(1);
	}

	switch (which){
		case DEVICE:
			if (strlen(p) > sizeof(u->device)){
				printf("uart: device name patch is too large\n");
				exit(1);
			}
			memset(u->device, 0, strlen(p));
			memcpy(u->device, p, strlen(p));
			break;
		case BAUDRATE:
			u->baudrate = atoi(p);
			break;
		default:
			printf("uart: wrong Parameter indicator\n");
			exit(1);
	}

	return 0;
	
}


int InitUartPort(const struct uart_t *u)
{
	int fd;
	struct termios oldtio, newtio;
	unsigned int baud;

	if (u->device[0] == 0) {
		printf("Uart: none serial port been set, please set a port to use. [/dev/ttyUSBx]\n");
		exit(1);
	}

	printf("Uart: using device=%s, bandrate=%d to init uart port.\n", u->device, u->baudrate);
	
 /* 
 *	WHICH_MODE is a flag to let read/write work without blocking
 *	Default set to BLOCK mode.
 */
	if ((fd = open(u->device, O_RDWR | O_NOCTTY)) == -1){
		printf("%s: Open serial port %s failed. return=%d, errno=%d - (%s)\n", __func__, u->device, fd, errno, strerror(errno));
		return -1;
	}
/*
#define NCCS 19
struct termios {
	tcflag_t c_iflag;		 // input mode flags 
	tcflag_t c_oflag;		 // output mode flags 
	tcflag_t c_cflag;	       // control mode flags 
	tcflag_t c_lflag;		 // local mode flags 
	cc_t c_cc[NCCS];		 // control characters 
	cc_t c_line;			 // line discipline (== c_cc[19]) 
	speed_t c_ispeed;		 // input speed 
	speed_t c_ospeed;		 // output speed 
};
*/
#ifdef UART_DEBUG
	memset(&oldtio, 0, sizeof(oldtio));
	if (tcgetattr(fd, &oldtio) == 0){
		printf("Uart before setting: %u\n", sizeof(oldtio));
		DispHexString(&oldtio, sizeof(oldtio));
		printf("Uart: c_ispeed = %d, c_ospeed = %d,%d\n", 
				oldtio.c_ispeed, 
				oldtio.c_ospeed,
				EXTB);
		printf("Uart: c_iflag = 0x%x, c_oflag = 0x%x, c_cflag = 0x%x, c_lflag = 0x%x, VTIME = %d\n", 
				oldtio.c_iflag, 
				oldtio.c_oflag, 
				oldtio.c_cflag, 
				oldtio.c_lflag,
				oldtio.c_cc[VTIME]);
		
	}
#endif
	memset(&oldtio, 0, sizeof(newtio));
	memset(&newtio, 0, sizeof(newtio));
	if (tcgetattr(fd, &newtio) != 0)
		return -1;
	oldtio = newtio;				// Need to restore the old value when we fail later, but we don't do it now.
	
	switch(u->baudrate){
		case 2400:
		baud = B2400;
		break;
		case 4800:
		baud = B4800;
		break;
		case 9600:
		baud = B9600;
		break;
		case 115200:
		baud = B115200;
		break;
		default:
		printf("Uart: Your input baudrate is %d, out of support range. Default to %d\n", u->baudrate, 115200);
		printf("Uart: Current support baudrate:%d, %d, %d, %d\n", 2400, 4800, 9600, 115200);
		baud = B115200;
		break;
	}
	
	cfsetispeed(&newtio, baud);
	cfsetospeed(&newtio, baud);

	newtio.c_cflag &= ~(PARENB | CSIZE | CSTOPB | PARODD | CRTSCTS);	// 
	newtio.c_cflag |= (CS8 | CLOCAL | CREAD);							// 8 bits data, Ignore Modem ctrl, Enable rx

/* 
*	Choosing Raw Input 
*	Raw input is unprocessed. Input characters are passed through exactly as they are received, 
*	when they are received. Generally you'll deselect the ICANON, ECHO, ECHOE, and ISIG options when using raw input
*/
	newtio.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG | ECHOK);			
	
/* 
*	Choosing Canonical Input 
* 	Canonical input is line-oriented. Input characters are put into a buffer 
*	which can be edited interactively by the user until a CR (carriage return) or LF (line feed) character is received.
*/																	
//	newtio.c_lflag |= (ICANON | ECHO | ECHOE | ISIG | ECHOK);				

	newtio.c_iflag &= ~(INLCR | ICRNL | IGNCR | IXON | IXOFF);
	newtio.c_iflag |= IGNBRK;

	newtio.c_oflag &= ~(OPOST | ONLCR | OCRNL);
	
/*Be careful to set these two parameter*/
//	newtio.c_cc[VTIME] = 5;  
//   	newtio.c_cc[VMIN] = 10; 
	
/*
TCSANOW:	exe now, but not wait data receive or send complete.
TCSADRAIN:	exe when all data receive or send complete
TCSAFLUSH:	Flush input and output buffers and make the change
*/
	if (tcflush(fd, TCIOFLUSH) != 0)	// Flush Terminal Input or Output
		perror("tcflush error");
	
	if (tcsetattr(fd, TCSANOW, &newtio) != 0){
		printf("Uart: Set port settings failed\n");
		return -1;
	}	
#ifdef UART_DEBUG
	memset(&newtio, 0, sizeof(newtio));
	if (tcgetattr(fd, &newtio) == 0){
		printf("Uart after setting: %u\n", sizeof(newtio));
		DispHexString(&newtio, sizeof(newtio));
		printf("Uart: c_ispeed = %d, c_ospeed = %d,%d\n",
				newtio.c_ispeed, 
				newtio.c_ospeed,
				EXTB);
		printf("Uart: c_iflag = 0x%x, c_oflag = 0x%x, c_cflag = 0x%x, c_lflag = 0x%x, VTIME = %d\n",
				newtio.c_iflag, 
				newtio.c_oflag, 
				newtio.c_cflag, 
				newtio.c_lflag,
				newtio.c_cc[VTIME]);
		
	}
#endif
	return fd;
}


/**************************************************************************
* UartRead - read len bytes into *buf from the socket in pUartInfo
*
* This routine calls read for for reading
* 
* RETURNS: length read
*/
//#define UART_DEBUG
int UartRead(struct uart_t* pUartInfo, char *buf, int len)
{
	unsigned long noblock;
	int nread;
	int ncopy;
	int error;
	int i;
	
	ncopy = 0;
	// THE Data lenth already read
	nread = UARTRXBUFFER - pUartInfo->rx_len;

	nread = read(pUartInfo->fd, &(pUartInfo->rx_buffer[pUartInfo->rx_len]), nread);
	if(nread <= 0){
		printf("%s: Read from uart fail. Return=%d, errno=%d - (%s)\n", __func__, nread, errno, strerror(errno));
		if (WHICH_MODE == NO_BLOCK_MODE){
			if (nread == -1){
				if (errno == EAGAIN)
					return 0;
			} 
			return nread;
		} else{
			return -1;
		}
	}

	pUartInfo->rx_len += nread;
	
	// check to see if reveive the terminate char '~'.
	for(i = 0; i < pUartInfo->rx_len; i++) {
		if((pUartInfo->rx_buffer[i] == DIAG_TERMINAL_CHAR)){
			i++;
			if(len < i) {
				ncopy = len - 1;
			} else {
				ncopy = i;
			}
			// copy data to user buffer
			memcpy(buf, pUartInfo->rx_buffer, ncopy);
			buf[ncopy] = 0;
			// compress the internal data
			memcpy(pUartInfo->rx_buffer, &pUartInfo->rx_buffer[i], pUartInfo->rx_len - i);
			pUartInfo->rx_len -= i;
			return ncopy;
		}
	}
		// there is no terminate char sent by QDART
	if(pUartInfo->rx_len > 0 && i == pUartInfo->rx_len){
		ncopy = pUartInfo->rx_len;
		if (ncopy < UARTRXBUFFER){
			memcpy(buf, pUartInfo->rx_buffer, ncopy);
			buf[ncopy] = 0;
		}else{
		//TODO: What should we do??  
			printf("Uart: ncopy >= sizeof(buf)\n");
		}
	}

	// no message, but buffer is full. This is a problem. delete allof the existing data.
	if(i >= UARTRXBUFFER){
		pUartInfo->rx_len = 0;
	}

	return ncopy;
}

/**************************************************************************
* UartWrite - write len bytes into the uart, from *buf
*
* This routine calls a OS specific routine for uart writing
* 
* RETURNS: length read
*/
int UartWrite(struct uart_t* pUartInfo, const char *buf, int len)
{
	int dwWritten;
	int bytes,cnt;
	const char* bufpos; 
	int tmp_len;
	int error;
	
	error = 0;

	tmp_len = len;
	bufpos = buf;
	dwWritten = 0;

	while (tmp_len){
		if(tmp_len < UARTTXBUFFER) 
			bytes = tmp_len;
		else
			bytes = UARTTXBUFFER;

		cnt = write(pUartInfo->fd, bufpos, bytes);
		if(cnt <= 0){
			if (cnt == -1){
				error++;
				if(error > 100000){
					break;
				}
				cnt=0;
				sleep(10);
				continue;
			}
			else{
				return -1;
			}
		}
		error = 0;
		dwWritten += cnt;
		tmp_len  -= cnt;
		bufpos += cnt;
	}

	if(dwWritten != len){
		dwWritten = 0;
	}

	return dwWritten;
}


/* 
Copyright (c) 2013 Qualcomm Atheros, Inc.
All Rights Reserved. 
Qualcomm Atheros Confidential and Proprietary. 
*/  

/* socket.h - header file for socket.c */

#ifndef INC_SOCKET_H
#define INC_SOCKET_H

#ifdef _USE_SOCKET_FUNCTIONS_IN_EXE_
#define PARSEDLLSPEC
#else
	#define PARSEDLLSPEC
#endif



#define MSOCKETBUFFER 4096

#ifdef LINUX_X86
#define PARSEDLLSPEC
#define closesocket     close
#endif

struct _Socket
{
	char hostname[128];
	unsigned int port_num;
	unsigned int ip_addr;
	int inHandle;
	int outHandle;
	int  sockfd;
	unsigned int sockDisconnect;
	unsigned int sockClose;
	int nbuffer;
	char buffer[MSOCKETBUFFER];
};

extern int SocketRead(struct _Socket *pSockInfo, unsigned char *buf, int len);
extern int SocketWrite(struct _Socket *pSockInfo, unsigned char *buf, int len);
extern void SocketClose(struct _Socket *pSockInfo);
extern struct _Socket *SocketConnect(char *pname, unsigned int port);
extern struct _Socket *SocketAccept(struct _Socket *pSockInfo, unsigned long noblock);
extern struct _Socket *SocketListen(unsigned int port);

#endif


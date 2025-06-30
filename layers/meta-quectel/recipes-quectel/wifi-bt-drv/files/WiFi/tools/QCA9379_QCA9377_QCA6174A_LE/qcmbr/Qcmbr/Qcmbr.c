
/* 
Copyright (c) 2013 Qualcomm Atheros, Inc.
All Rights Reserved. 
Qualcomm Atheros Confidential and Proprietary. 
*/  

/* qcmbr.c - Qcmbr main */

 #define HANDLE int

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <string.h>
#include "Socket.h"

#include "Qcmbr.h"

#ifdef LINUX_X86
extern char *ifa_name;
#endif

#if defined(LINUX) || defined(__APPLE__)
#define SformatOutput snprintf
#define SformatInput sscanf
#else
#define SformatOutput(buffer, size, format, ...) _snprintf_s(buffer, size, _TRUNCATE, format, __VA_ARGS__) 
#define SformatInput sscanf
#endif

#ifndef QCMBR_UART_QDART
const char *QCMBR_VERSION = "QCMBR VERSION : 002.000.000.000";
#else
const char *QCMBR_VERSION = "QCMBR VERSION : 003.000.000.000";
#endif

#ifdef QCMBR_UART_QDART
ConnectionType connectionTypeOptions;
char Device[PATH_MAX] = "";
char IOTypeString[10] = ""; 
char* pPosition = NULL;
char baudrate[10] = "115200";    //default: 1152000

extern struct uart_t uart_port;
#endif

HANDLE ghMutex = 0;
int    gFromQ = 0;
// When verbose mode is enabled only then display all the details on packets.
// could be utilized to disable all prints. For now, starting with the place
// which has the most impact.
int    verbose = 0;


#define MAX_HS_WIDTH    16
#define DbgPrint    printf
void DispHexString(unsigned char *pkt_buffer, int recvsize)
{
  int i, j, k;

  if (verbose == 1)
  {
	  for (i=0; i<recvsize; i+=MAX_HS_WIDTH) {
		DbgPrint("[%4.4d] ",i);
		for (j=i, k=0; (k<MAX_HS_WIDTH) && ((j+k)<recvsize); k++)
		  DbgPrint("0x%2.2X ",(pkt_buffer[j+k])&0xFF);
		for (; (k<MAX_HS_WIDTH ); k++)
		  DbgPrint("     ");
		DbgPrint(" ");
		for (j=i, k=0; (k<MAX_HS_WIDTH) && ((j+k)<recvsize); k++)
		  DbgPrint("%c",(pkt_buffer[j+k]>32)?pkt_buffer[j+k]:'.');
		DbgPrint("\n");
	  }
  }
}

void receiveCmdReturn1(void *buf)
{
    A_UINT8 *reply = (A_UINT8*)buf;
    A_UINT32 length =0;

    length = *(A_UINT32 *)&(reply[0]);

    printf("receiveCmdReturn1() TLV length got %lu\n",length);
    DispHexString((unsigned char *)buf,length);
}


int ConnectHostAndPort(char *buffer, char *host, int *port)
{
	char *scolon;
	int ngot;
	//
	// if there is a colon, then it's in format host:port
	//
	scolon=strchr(buffer,':');
	if(scolon!=0)
	{
		strncpy(host,buffer,scolon-buffer);
		host[scolon-buffer]=0;
		scolon++;
		ngot=SformatInput(scolon," %d ",port);
	}
	//
	// we need to figure out if it is a host or a port
	//
	else
	{
		if(strchr(buffer,'.')!=0 || isalpha(buffer[0]))
		{
			strcpy(host,buffer);
			ngot=1;
		}
		else
		{
			ngot=SformatInput(buffer," %d ",port);
		}
	}

	return ngot!=1;
}


int main(int narg, char *arg[]) 
{
	int iarg;
	int console;
   	int instance=0;
   	FILE *lf;
   	int port;
   	char dutIP[MBUFFER];
	int dutPort=NART_PORT;
	const char parameter_gap = '-';
	
      
    	console=0;
    	port = -1;

#ifdef QCMBR_UART_QDART
	memset(&uart_port, 0, sizeof(uart_port));
#endif


	for(iarg = 1; iarg < narg; iarg++) {
		if (strcmp(arg[iarg], "-q") == 0) {
           	gFromQ = 1;
		   	printf("Called from QDart.\n");
        } else if (strncmp(arg[iarg], "-port", sizeof( "-port" )) == 0) {
            if(iarg + 1 < narg) {
                sscanf( arg[iarg+1], " %d ",(int*) &port);
                iarg++;
            }
        } else if ( strncmp(arg[iarg], "-console", sizeof( "-console" ) ) == 0) {
			console=1;
		} else if ( strncmp(arg[iarg], "-instance", sizeof( "-instance" ) ) == 0) {
			if(iarg<narg-1) {
				iarg++;
				sscanf( arg[iarg]," %d ",(int*)&instance);
			} else {
				printf( "Error - NULL instance.\n" );
			}
		} else if (strncmp(arg[iarg], "-log", sizeof( "-log" )) == 0) {            
			if(iarg < narg - 1) {
				iarg++;
				lf = fopen(arg[iarg], "a+");
				if(lf==0) {
					printf( "Can't open log file %s.\n", arg[iarg] );
				}
				else {
					fprintf( lf, "File %s\n", arg[iarg] );
				}			
            }
			else {
				printf( "Error - No log file specified.\n" );
			}
		} else if(strncmp(arg[iarg],"-dutIP", sizeof( "-dutIP" ) )==0) {
			if(iarg < narg - 1) {
				iarg++;
				if(ConnectHostAndPort(arg[iarg],dutIP,&dutPort))
				{
				    printf( "Error -dutIP not specified correctly\n" );
				}
			} else {
				printf( "Error - No dutIP specified.\n" );
			}
		} else if (strncmp(arg[iarg],"-help", sizeof( "-help" )) == 0) {
			printf( "-console\r\n" );
			printf( "-log [log file name]\r\n" );
			printf( "-port [port number]\r\n" );
			printf( "-instance [device index]\r\n" );
			printf( "-dutIP [DutIP:DutPort]\r\n" );
			printf( "-v :   Verbose mode\r\n");
			printf( "-i :   [e | s] Interface used to connect with QDART. Uart:[s], Ethernet:[e].\n");
			printf( "-d :   Uart device port name.\r\n");
			printf( "-b :   Uart port baudrate.\r\n");
			exit(0);
		} else if (strncmp(arg[iarg], "-v", sizeof( "-v" )) == 0) {
			printf( "Enabling verbose mode\n" );
			verbose = 1;
		} 
#ifdef QCMBR_UART_QDART
/*
 *	./Qcmbr -i s -d /dev/ttyUSB$x -b 115200
*/
		else if (strncmp(arg[iarg], "-i", strlen("-i")) == 0) {	
			if (strlen(arg[iarg]) > 2) {
				pPosition = arg[iarg] + 2;
				if (strchr(pPosition, parameter_gap)) {
					printf("Wrong parameter input format, please use blank between two '-' parameter\n");
					exit(1);
				}
				goto next1;
			}
			if (iarg < narg - 1) {
				iarg++;
				pPosition = arg[iarg];
			next1:
				memcpy(IOTypeString, pPosition, strlen(pPosition));
				IOTypeString[strlen(pPosition)] = '\0';	
				printf("IOType = %s\n", IOTypeString);		
			} else {
				printf("Please select an interface Uart:[s], Ethernet:[e].\n");
				exit(1);
			}
		} else if (strncmp(arg[iarg], "-d", strlen("-d")) == 0) {
			if (strlen(arg[iarg]) > 2) {
				pPosition = arg[iarg] + 2;
				if (strchr(pPosition, parameter_gap)) {
					printf("Wrong parameter input format, please use blank between two '-' parameter\n");
					exit(1);
				}
				goto next2;
			}
			if (iarg < narg - 1) {
				iarg++;
				pPosition = arg[iarg];
			next2:
				memcpy(Device, pPosition, strlen(pPosition));
				Device[strlen(pPosition)] = '\0';	
				SetUartParameter(&uart_port, Device, DEVICE);
				printf("DEVICE = %s\n", Device);		
			} else {
				printf("Please select an uart device note.\n");
				exit(1);
			}
		} else if (strncmp(arg[iarg], "-b", strlen("-b")) == 0) {
			if (strlen(arg[iarg]) > 2) {
				pPosition = arg[iarg] + 2;
				if (strchr(pPosition, parameter_gap)) {
					printf("Wrong parameter input format, please use blank between two '-' parameter\n");
					exit(1);
				}
				goto next3;
			}
			if (iarg < narg - 1) {
				iarg++;
				pPosition = arg[iarg];
			next3:
				memcpy(baudrate, pPosition, strlen(pPosition));
				baudrate[strlen(pPosition)] = '\0';	
				SetUartParameter(&uart_port, baudrate, BAUDRATE);
				printf("baudrate = %s\n", baudrate);
			} else {
				printf("Please select uart port baudrate.\n");
				exit(1);
			}
		}
#endif	
		else {
			printf( "Error - Unknown parameter %s.\n", arg[iarg]);
		}
	}

    	if(instance == 0) {
		if(port<0) {
			port=2390;
		} 
	} else if(instance == 1) {
		if(port<0) {
#ifndef __APPLE__	
			port=2391;
#else
			port=2390;
#endif			
		}
    	} else {
#ifndef __APPLE__	
        printf("Only instance 0 and 1 are supported\n");
		exit(1);
#else
		if(port<0) {
			port=2390;
		} 	
#endif	
    	}

#ifdef QCMBR_UART_QDART
        if((!strcmp(IOTypeString, "s")) || (!strcmp(IOTypeString, "S"))) {            
            	connectionTypeOptions = SERIAL;
		printf("UART Mode\n");
        } else if ((!strcmp(IOTypeString, "e")) || (!strcmp(IOTypeString, "E"))) {
            	connectionTypeOptions = ETHERNET;
		printf("ETHERNET Mode\n");
        } else {
        	printf("Invalid entry, valid options are : [SERIAL] [ETHERNET]\n");
		printf("Default to ETHERNET Mode\n");
		connectionTypeOptions = ETHERNET;
        }
#endif

#ifdef QCMBR_UART_QDART
	if (connectionTypeOptions == SERIAL) {
		if (uart_port.device[0] == 0)
			SetUartParameter(&uart_port, UART_DEVICE_NAME, DEVICE);
		if (uart_port.baudrate == 0)
			SetUartParameter(&uart_port, UART_DEVICE_BAUDRATE_DEFAULT, BAUDRATE);
		
		uart_port.fd = InitUartPort(&uart_port);
		
		if (uart_port.fd < 0) {
			printf("Uart: Init serial port fail!\n");
			exit(1);
		}
	} else {
		// NOTHING TO DO
	}
#endif


#ifdef QCMBR_UART_QDART
	if (connectionTypeOptions == SERIAL) 
		Qcmbr_Run_Uart();
	else if (connectionTypeOptions == ETHERNET)
		Qcmbr_Run(port);
#else
		Qcmbr_Run(port);
#endif

	exit(0);
	
}

static struct _Socket *_ListenSocket;	// this is the socket on which we listen for client connections

static struct _Socket *_ClientSocket[MCLIENT];	// these are the client sockets


#define MCOMMAND 50

static int _CommandNext=0;		// index of next command to perform
static int _CommandRead=0;		// index of slot for next command read from socket

static char *_Command[MCOMMAND];
static char _CommandClient[MCOMMAND];

static int ClientAccept()
{
    int it;
    int noblock;
    struct _Socket *TryClientSocket;
    static int OldestClient = 0;

	if(_ListenSocket!=0)
	{
		//
		// If we have no clients, we will block waiting for a client.
		// Otherwise, just check and go on.
		//
		noblock=0;
		for(it=0; it<MCLIENT; it++)
		{
			if(_ClientSocket[it]!=0)
			{
				noblock=1;
				break;
			}
		}
		if(noblock==0)
		{
			printf("\nReady for Client(QDart) Connection!" );
		}
		//
		// Look for new client
		//
		for(it=0; it<MCLIENT; it++)
		{
			if(_ClientSocket[it]==0)
			{
				_ClientSocket[it]=SocketAccept(_ListenSocket,noblock);// don't block
				if(_ClientSocket[it]!=0)
				{
					printf("Client connection is established at [%d]!\n",it);
					return it;
				}
			}
		}
        // In case all clients have been used up, but there is another client want to connect, give away the oldest one
        if (it == MCLIENT)
        {
            TryClientSocket = SocketAccept(_ListenSocket,noblock);
            if (TryClientSocket)
            {
                SocketClose(_ClientSocket[OldestClient]);
                _ClientSocket[OldestClient] = TryClientSocket;
                
                return it;
            }
        }
	}
	return -1;
}


static void ClientClose(int client)
{
	if(client>=0 && client<MCLIENT && _ClientSocket[client]!=0)
	{
		SocketClose(_ClientSocket[client]);
		_ClientSocket[client]=0;
	}
}

int CommandRead()
{
	unsigned char buffer[MBUFFER];
	int nread;
	//int ntotal;
	int it;
    int diagPacketReceived = 0;
    unsigned int cmdLen = 0;

	//
	// look for new clients
	//
	ClientAccept();
	//
	// try to read everything on the client socket
	//
	//ntotal=0;
	while(_CommandNext!=_CommandRead || _Command[_CommandRead]==0)
	{
		if(_ListenSocket==0)
		{
		}
		else
		{
			//
			// read commands from each client in turn
			//
			for(it=0; it<MCLIENT; it++)
			{
				if(_ClientSocket[it]!=0)
				{
					nread = (int)SocketRead(_ClientSocket[it],buffer,MBUFFER-1);
					if(nread>0)
					{
						printf( "SocketRead() return %d bytes\n", nread );
						buffer[nread]=0;
                        			DispHexString(buffer,nread);
						
						if(nread>1 && buffer[nread-1]==DIAG_TERM_CHAR)
						{
							diagPacketReceived = 1;
							buffer[nread-1]=0;
							cmdLen = nread-0;
 						}
						//Needed for linux path
						if(nread>2 && buffer[nread-2]==DIAG_TERM_CHAR)
						{
							diagPacketReceived = 1;
							buffer[nread-2]=0;
							cmdLen = nread - 1;
						}
						//
						// check to see if we received a diag packet
						//
						if(diagPacketReceived) {
							if(processDiagPacket(it,(unsigned char *)buffer,cmdLen)) {
								printf( "\n--processDiagPacket-succeed------ Wait For Next Diag Packet ----------------\n\n" );
								continue;
							}
							else
							{
								printf( "\n--processDiagPacket-failed------- Wait For Next Diag Packet ----------------\n\n" );
							}
						}
					}
					else if(nread<0)
					{
						printf("Closing connection <-- Remote connection closed.[%d]\n",gFromQ);
						ClientClose(it);
						exit(1);
					}
				}
			}
		}
	}
	return 0; //ntotal;
}

#ifdef QCMBR_UART_QDART
int CommandRead_Uart(void)
{
	unsigned char buffer[MBUFFER];
	int nread;
	int it = 0; //1 USE this parameter to compatible wtih history code
    	int diagPacketReceived = 0;
    	unsigned int cmdLen = 0;

	// try to read everything from the uart
	while(1) {
		
		nread = UartRead(&uart_port, buffer, sizeof(buffer) - 1); // read QDART commands from uart
		
		if(nread > 0) {
			
		//	printf( "UartRead() receive %d bytes from QDART\n", nread);
			buffer[nread] = 0;
		    	DispHexString(buffer, nread);
				
			if(nread > 1 && buffer[nread - 1] == DIAG_TERM_CHAR) {//1EVERY QDRAT cmd has a 0x7e(~) at last??
				diagPacketReceived = 1;
				buffer[nread - 1] = 0;
				cmdLen = nread - 0;
			}
			//Needed for linux path
			if(nread > 2 && buffer[nread - 2] == DIAG_TERM_CHAR) {//1WHY ????
				diagPacketReceived = 1;
				buffer[nread-2] = 0;
				cmdLen = nread - 1;
			}
			// check to see if we received a diag packet from DDART
			if(diagPacketReceived) {
				
				diagPacketReceived = 0;
				if(processDiagPacket(it, (unsigned char *)buffer, cmdLen)) {
					printf( "\n--processDiagPacket-succeed------ Wait For Next Diag Packet ----------------\n\n" );
					continue;
				}
				else {
					printf( "\n--processDiagPacket-failed------- Wait For Next Diag Packet ----------------\n\n" );
				}
			}
		}else if (nread == 0) {
			// Till Now, THE ONLY CONDITION to get here is that : you set WHICH_MDOE TO NO_BLOCK_MODE.
			// If no data in UART, read func will return with -1 and errno == EAGAIN. IT is the only condition, we allow to get here to conitune next read.
		}
		else if (nread < 0) {
			printf("%s : Read UART FAIL.\n", __func__);
			exit(1);
		}
	}
	return 0; //ntotal;
}
#endif
int CommandNext(unsigned char *command, int max, int *client)
{
	int length;

#ifdef QCMBR_UART_QDART	
	if (connectionTypeOptions == SERIAL) {
		if(CommandRead_Uart() < 0)
			return -1;
	}
	else if (connectionTypeOptions == ETHERNET) {
		if(CommandRead() < 0) {
			return -1;
		}
	}
#else
	if(CommandRead() < 0) {
		return -1;
	}
#endif
	
	//
	// if we have a command, return it
	//
	if(_Command[_CommandNext]!=0)
	{
		length = sizeof( _Command[_CommandNext]); //length=Slength(_Command[_CommandNext]);
		if(length>max)
		{
			_Command[_CommandNext][max]=0;
			length=max;
		}

		if ( command != NULL )
		{
			if ( _Command[_CommandNext] == NULL )
			{
				strncpy( (char*) command,_Command[_CommandNext], 
						( sizeof( command ) > sizeof( _Command[_CommandNext] ) ) ? 
								sizeof( _Command[_CommandNext] ) : ( sizeof( command ) ) );
			}
		}
		*client=_CommandClient[_CommandNext];
		if ( _Command[_CommandNext] != NULL ) { free( _Command[_CommandNext] ); }
		_Command[_CommandNext]=0;
		_CommandNext=(_CommandNext+1)%MCOMMAND;

		printf("> %s\n",command);

		return length;
	}
	return 0;
}

int SendItDiag(int client, unsigned char *buffer, int length)
{
	int nwrite;

	if(_ListenSocket==0|| (client>=0 && client<MCLIENT && _ClientSocket[client]!=0))
	{
		if ( _ListenSocket == 0 )
		{
			//printf("%s",response);
		}
		else
		{
			SocketWriteEnableMode( 1 );
			nwrite=SocketWrite(_ClientSocket[client],buffer,length);
			SocketWriteEnableMode( 0 );
			
			if(nwrite<0)
			{
				printf( "Error - Call to SocketWrite() failed.\n" );
				ClientClose(client);
				return -1;
			}	
		}
	    return 0;
	}
	else
	{
		return -1;
	}

}
#ifdef QCMBR_UART_QDART
int SendItDiag_Uart(int client, char *buffer, int length)
{
	int nwrite;
#ifdef UART_DEBUG
	{
		// DEBUG USE
	}
#endif
	nwrite = UartWrite(&uart_port, buffer, length);
			
	if(nwrite < 0) {
		printf( "Error - UartWrite failed.\n" );
		return -1;
	}	
	return 0;
}
#endif
void Qcmbr_Run(int port)
{
	unsigned char buffer[MBUFFER];
	int nread;
	int client;
	int it;

	SetStrTerminationChar( DIAG_TERM_CHAR );
	SocketWriteEnableMode( 0 );

    //
    // open listen socket
    //
	if(port>0)
	{
		_ListenSocket=SocketListen(port);
		if(_ListenSocket==0)
		{
			printf( "Can't open control process listen port %d.", port );
			exit(-1);
		}
	}
	else
	{
		//
		// this means accept commands from the keyboard
		// and display result by typing in the console window
		//
		_ListenSocket= 0;
	}

	// Clear the client socket records
	for ( it=0; it < MCLIENT; it++ )
	{
		_ClientSocket[it] = 0;
	}

    //
    // wait for commands or new clients
    //
	//while(1)
	//{
		ClientAccept();
	
	    while(1)
		{
			nread=CommandNext(buffer,MBUFFER-1,&client);

			//
			// Got data. Process it.
			//
		    if(nread>0)
			{
				// PASS TO DIAG PACKET HANDLER
				processDiagPacket( 0, buffer, nread);
			}
			//
			// Got error. Probably lost command module. Redo socket accept.
			//
			else if(nread<0)
			{
				printf("%s --> enter here 0\n", __func__);
			}
			//
			// slow down
			//
			else
			{
				Sleep(0);
			}
		}
	//}
}
#ifdef QCMBR_UART_QDART
void Qcmbr_Run_Uart(void)
{
	unsigned char buffer[MBUFFER];
	int nread;
	int client; // not use in uart mode, just to compatible with ethernet mode
	int it;
	
	while(1) {		
		
		nread = CommandNext(buffer, MBUFFER - 1 , &client);
		
		printf("We should not get here!%s:%d\n", __func__, __LINE__);
		
		// Got data. Process it.
		if(nread > 0) { // PASS TO DIAG PACKET HANDLER				
			processDiagPacket( 0, buffer, nread);
		}
		// Got error. Probably lost command module.
		else if(nread < 0) {
		}
		// slow down
		else {
			Sleep(0);
		}
	}
}
#endif

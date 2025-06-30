/* 
Copyright (c) 2013-2020 Qualcomm Technologies, Inc.
All Rights Reserved. 
Confidential and Proprietary - Qualcomm Technologies, Inc.
*/  

/*
 *  This app makes a DIAG bridge for standalone BT SoC devices to communicate with QMSL/QRCT
 *  QPST is a requirement to make connection with QMSL/QRCT 
 *  Requires at least four command line arguments:
 *  1. Server IP address
 *  2. Server port number
 *  3. Connection type, SERIAL or USB
 *  4. Connection details
 *      a. COMPORT number for SERIAL
 *      b. USB connection string for USB mode
 *  5. Baudrate, optional and used only in SERIAL mode, default is 115200
 *  6. SERIAL port handshaking, optional and used only in SERIAL mode, default is NONE, valid options are NONE, XONXOFF, RequestToSend, RequestToSendXONXOFF
 */
#include <stdio.h>      //printf
#include <string.h>     //strlen
#include <sys/socket.h> //socket
#include <arpa/inet.h>  //inet_addr

#include <stdbool.h>
#include <limits.h>     //PATH_MAX
#include <stdlib.h>     //atoi
#include <pthread.h>    //thread
#include "global.h"
#ifdef PLATFORM_ANDROID
#include "connection.h"
#endif
#ifdef BT_HCI_UART
#include "connection.h"
#endif

#ifdef BLUEZ_STACK
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#endif

#define BTDIAG_VERSION "3.0.0.4"

int sock;
int client_sock;
bool legacyMode = 1;
ConnectionType connectionTypeOptions;
ConnectionType QDARTConnectionType;
extern struct uart_t uart_port;
int debug_print = 0;
#define MAX_HS_WIDTH	16

char Device[PATH_MAX] = "";
int descriptor = 0; 
void *NotifyObserver(void * arguments);

struct arg_struct {
    int desc;
    unsigned char* buf;
	int size;
};

void DispHexString(unsigned char *pkt_buffer, int recvsize)
{
	int i, j, k;

	if (debug_print == 1)
	{
		for (i=0; i<recvsize; i+=MAX_HS_WIDTH) {
			printf("[%4.4d] ",i);
			for (j=i, k=0; (k<MAX_HS_WIDTH) && ((j+k)<recvsize); k++)
				printf("0x%2.2X ",(pkt_buffer[j+k])&0xFF);
			for (; (k<MAX_HS_WIDTH ); k++)
				printf("     ");
			printf(" ");
			for (j=i, k=0; (k<MAX_HS_WIDTH) && ((j+k)<recvsize); k++)
				printf("%c",(pkt_buffer[j+k]>32)?pkt_buffer[j+k]:'.');
			printf("\n");
		}
	}
}

int Diag_Write_Uart(BYTE *buffer, int length)
{
	int nwrite;
	nwrite = UartWrite(&uart_port, buffer, length);

	if(nwrite < 0) {
		printf( "Error - UartWrite failed.\n" );
		return -1;
	}
	return 0;
}

int Diag_Read_Uart(void)
{
	//unsigned char buffer[MBUFFER];
	BYTE incomingDataBytes[UARTRXBUFFER];
	int nread;
	int it = 0;
	int i;
	int diagPacketReceived = 0;
	unsigned int cmdLen = 0;

	// try to read everything from the uart
	while(1) {
		memset(incomingDataBytes, 0, UARTRXBUFFER);
		nread = UartRead(&uart_port, incomingDataBytes, sizeof(incomingDataBytes) - 1); // read QDART commands from uart
		if(nread > 0) {
			incomingDataBytes[nread] = 0;
			//DispHexString(incomingDataBytes, nread);

			if(nread > 1 && incomingDataBytes[nread - 1] == DIAG_TERMINAL_CHAR) {//1EVERY QDRAT cmd has a 0x7e(~) at last??
				diagPacketReceived = 1;
			}
			//Needed for linux path
			if(nread > 2 && incomingDataBytes[nread - 2] == DIAG_TERMINAL_CHAR) {//1WHY ????
				diagPacketReceived = 1;
			}
			// check to see if we received a diag packet from QDART
			if(diagPacketReceived) {

				diagPacketReceived = 0;
				if(processDiagPacket(incomingDataBytes)) {
#ifdef PLATFORM_ANDROID
					fprintf(stderr, "\n--processDiagPacket-succeed------ Wait For Next Diag Packet ----------------\n\n");
#else
					printf( "\n--processDiagPacket-succeed------ Wait For Next Diag Packet ----------------\n\n" );
#endif
					continue;
				}
				else {
#ifdef PLATFORM_ANDROID
					fprintf(stderr, "\n--processDiagPacket-failed------ Wait For Next Diag Packet ----------------\n\n");
#else
					printf( "\n--processDiagPacket-failed------- Wait For Next Diag Packet ----------------\n\n" );
#endif
				}
			}
		}else if (nread == 0) {
			// Till Now, THE ONLY CONDITION to get here is that : you set WHICH_MDOE TO NO_BLOCK_MODE.
			// If no data in UART, read func will return with -1 and errno == EAGAIN. IT is the only condition, we allow to get here to conitune next read.
		}
		else if (nread < 0) {
#ifdef PLATFORM_ANDROID
			fprintf(stderr, "%s : Read UART FAIL.\n", __func__ );
#else
			printf("%s : Read UART FAIL.\n", __func__);
#endif
			close(uart_port.fd);
			exit(1);
		}
	}
	close(uart_port.fd);
	return 0; //ntotal;
}

void *NotifyObserver(void * arguments)
{
	struct arg_struct *args = arguments;
	int i, ret, recv_byte_len, data_len, DataToSendasLogPacketFor1366_Length = 0;
	unsigned char* tempbuf;

	while (1)
	{
		switch (connectionTypeOptions)
		{
			case SERIAL:
#ifdef HCI_CMD_NEED_DELAY
				ret = uart_read_raw_data(args->desc, args->buf, args->size);
#else
				ret = read_hci_event(args->desc, args->buf, args->size);
#endif
				recv_byte_len = ret;
				break;
			case USB:
#ifdef BLUEDROID_STACK
				ret = bluedroid_read_raw_data(args->buf, args->size);
#elif defined(FLUORIDE_STACK)
				ret = fluoride_read_raw_data(args->desc, args->buf, args->size);
				recv_byte_len = ret;
				break;
#else
				ret = read_raw_data(args->desc, args->buf, args->size);
#endif
				//USB get returned data is raw data, the first byte of raw data is "HCI_PACKET_TYPE" header. (its value should be "HCI_EVENT_PKT" here).
				//The HCI data follow it, so we shift one byte to get HCI data.
				tempbuf = args->buf;
				args->buf = (args->buf + 1);
				recv_byte_len = ret - 1;
				break;
		}

		if (ret < 0) {
#ifdef PLATFORM_ANDROID
			fprintf(stderr, "%s: Failed to send hci command to Controller, ret = %d\n", __FUNCTION__, ret);
#else
			printf("%s: Failed to send hci command to Controller, ret = %d\n", __FUNCTION__, ret);
#endif
			return 0;
		}
		else
		{
			switch (connectionTypeOptions)
			{
				case SERIAL:
#ifdef PLATFORM_ANDROID
					fprintf(stderr,"Data In(from DUT)(hex) - Event: ");
#else
					printf("Data In(from DUT)(hex) - Event: ");
#endif
					for (i=0; i<recv_byte_len; i++)
					{
#ifdef PLATFORM_ANDROID
						fprintf(stderr,"%02x ", args->buf[i]);
#else
						printf("%02x ", args->buf[i]);
#endif
					}
					printf("\n\n");
					break;
				case USB:
#ifdef PLATFORM_ANDROID
					fprintf(stderr,"Data In(from DUT)(hex) - Event: ");
#else
					printf("Data In(from DUT)(hex) - Event: ");
#endif
					for (i=0; i<ret; i++)
					{
#ifdef PLATFORM_ANDROID
						fprintf(stderr,"%02x ", tempbuf[i]);
#elif defined(FLUORIDE_STACK)
						printf("%02x ", args->buf[i]);
#else
						printf("%02x ", tempbuf[i]);
#endif
					}
					printf("\n\n");
					break;
			}
		}

		//if (packetData.type == PacketDataType.Data)
		{

			switch (connectionTypeOptions)
			{
				case SERIAL:
					break;
				case USB:
					break;
			}

#ifndef FLUORIDE_STACK
			if (connectionTypeOptions == USB)
				data_len = recv_byte_len + 1;
			else
#endif
				data_len = recv_byte_len;

			BYTE DataToSendasLogPacketFor1366[15 + data_len];
			DataToSendasLogPacketFor1366_Length = 15 + data_len;
			memset(DataToSendasLogPacketFor1366, 0, DataToSendasLogPacketFor1366_Length);

			BYTE DataToSendasLogPacketHeader[] = { 0x10, 00, 0x15, 00, 0x15, 00, 0x7C, 0x11, 00, 00, 0xC7, 0xE5, 0x62, 0x2A, 0xC5, 0xFF, 01, 00 };
			BYTE DataToSendasLogPacketHeaderFor1366[] = { 0x10, 00, 0x12, 00, 0x12, 00, 0x66, 0x13, 0xE5, 0x62, 0x2A, 0xC5, 0xFF, 01, 00 };

			//for 0x1366
			for (i = 0; i < 15; i++)
			{
				DataToSendasLogPacketFor1366[i] = DataToSendasLogPacketHeaderFor1366[i];
			}

#ifndef FLUORIDE_STACK
			if (connectionTypeOptions == USB)
			{
				DataToSendasLogPacketFor1366[15] = 4;
			}

			if (connectionTypeOptions == USB)
			{
				//adding BT HCI event to the DIAG log packet
				for (i = 0; i < recv_byte_len; i++)
				{
					DataToSendasLogPacketFor1366[16 + i] = args->buf[i];
				}
			}
			else
#endif
			{
				//adding BT HCI event to the DIAG log packet
				for (i = 0; i < recv_byte_len; i++)
				{
					DataToSendasLogPacketFor1366[15 + i] = args->buf[i];
				}
			}

			//fixing the exact length in the DIAG log packet for 0x1366
			int lengthOfDiagPacketFor1366 = DataToSendasLogPacketFor1366_Length - 4;
			DataToSendasLogPacketFor1366[2] = (BYTE)lengthOfDiagPacketFor1366;
			DataToSendasLogPacketFor1366[4] = (BYTE)lengthOfDiagPacketFor1366;

			DiagPrintToStream(DataToSendasLogPacketFor1366, DataToSendasLogPacketFor1366_Length, true);
			//debug message commented out
			//Console.WriteLine("1366 (from DUT): " + Conversions.ByteArrayTo0xHexString(DataToSendasLogPacketFor1366) + "\r\n");
		}
	}
}

int main(int argc , char *argv[])
{
    struct sockaddr_in server, client;
	int clientaddr_size, read_size;
    char message[1000] , server_reply[2000];
    char ipAddress[255] = "localhost";
    int portNumber = 2390;            //default: 2390
    char IOTypeString[10] = "";      
    char baudrate[10] = "115200";    //default: 115200
	char QDARTIOTypeString[10] = "";
	char ComName[20] = "";
	char ComBaudrate[10] = "";
    //int handShakeMode = 0;            //default: NONE
    int i, ret;
	bool isPatchandNvmRequired = 0;
	
    //getting all the input arguments and splitting based on "="
    char tempStr[20] = "";
    char* pPosition = NULL;

    //these variables will be used by child thread and should never released
    struct arg_struct args;
    unsigned char databuf[HCI_MAX_EVENT_SIZE];

	memset(&uart_port, 0, sizeof(uart_port));

	//get argument values and check if minimum agruments are provided
	if (argc < 6)
	{
		printf("Btdiag version: %s\n\n", BTDIAG_VERSION);
		printf("Require at least 5 arguments.\nExample of UDT(server) mode, Btdiag usage:\n");
		printf("UDT:	User defined transport.\n");
		printf("PORT:	Socket port number.\n");
		printf("IOTYPE:	Bluetooth interface type.\n");
		printf("QdartIOType:	Interface used to connect with QDART,may be uart or ethernet.\n");
		printf("BT-DEVICE:		bluetooth device.\n");
		printf("BT-BAUDRATE:	Bluetooth serial baudrate.\n");
		printf("QDART-DEVICE:	QDART serial device.\n");
		printf("QDART-BAUDRATE:	QDART serial baudrate.\n");

		printf("- BT FTM mode for serial and connect with QDART by serial:\n");
		printf("./Btdiag UDT=yes PORT=2390 IOType=SERIAL QDARTIOType=serial BT-DEVICE=/dev/ttyUSB0 BT-BAUDRATE=115200 QDART-DEVICE=/dev/ttyUSB1 QDART-BAUDRATE=115200\n\n");

		printf("- BT FTM mode for serial and connect with QDART by ethernet:\n");
		printf("./Btdiag UDT=yes PORT=2390 IOType=SERIAL QDARTIOType=ethernet BT-DEVICE=/dev/ttyUSB0 BT-BAUDRATE=115200\n\n");

		printf("- BT FTM mode for USB and connect with QDART by serial::\n");
		#ifdef BLUEDROID_STACK
		printf("./Btdiag UDT=yes PORT=2390 IOType=USB QDARTIOType=serial BT-DEVICE=SS1BTUSB0 QDART-DEVICE=/dev/ttyUSB0 QDART-BAUDRATE=115200\n\n");
		#else
		printf("./Btdiag UDT=yes PORT=2390 IOType=USB QDARTIOType=serial BT-DEVICE=hci0 QDART-DEVICE=/dev/ttyUSB0 QDART-BAUDRATE=115200\n\n");
		#endif

		printf("- BT FTM mode for USB and connect with QDART by ethernet::\n");
		#ifdef BLUEDROID_STACK
		printf("./Btdiag UDT=yes PORT=2390 IOType=USB QDARTIOType=ethernet BT-DEVICE=SS1BTUSB0\n\n");
		#else
		printf("./Btdiag UDT=yes PORT=2390 IOType=USB QDARTIOType=ethernet BT-DEVICE=hci0\n\n");
		#endif
		return 1;
    }
	//Check UDT
	if (strstr(argv[1], "UDT"))
    {
        pPosition = strchr(argv[1], '=');
        if (0 == strcmp(pPosition+1, "yes"))
		{
		    #ifdef PLATFORM_ANDROID
		    fprintf(stderr,"Run in UDT mode\n");
		    #else
		    printf("Run in UDT mode\n");
		    #endif
			legacyMode = 0;
		}
        else
        {
           #ifdef PLATFORM_ANDROID
            fprintf(stderr,"Run in legacy mode\n");
            #else
            printf("Run in legacy mode\n");
            #endif
        }
    }
	
    if (legacyMode)
    {	    
        //IPADDRESS
        if (NULL == strstr(argv[1], "IPADDRESS"))
        {
            #ifdef PLATFORM_ANDROID
            fprintf(stderr,"\nMissing QPST server ipaddress, Example: Btdiag.exe IPADDRESS=192.168.1.1\n");
            #else
            printf("\nMissing QPST server ipaddress, Example: Btdiag.exe IPADDRESS=192.168.1.1\n");
            #endif
            return 1;
        }
        else
        {
            pPosition = strchr(argv[1], '=');
            memcpy(ipAddress, pPosition+1, strlen(pPosition+1));
        }
        
        //PORT
        if (NULL == strstr(argv[2], "PORT"))
        {
            #ifdef PLATFORM_ANDROID
            fprintf(stderr,"\nMissing QPST server port number\n");
            #else
            printf("\nMissing QPST server port number\n");
            #endif
            return 1;
        }
        else
        {
            pPosition = strchr(argv[2], '=');
            memcpy(tempStr, pPosition+1, strlen(pPosition+1));
            portNumber = atoi(tempStr);
        }

        //IOType
        if (NULL == strstr(argv[3], "IOType"))
        {
            #ifdef PLATFORM_ANDROID
            fprintf(stderr,"\nMissing IOType option, valid options are SERIAL, USB\n");
            #else
            printf("\nMissing IOType option, valid options are SERIAL, USB\n");
            #endif
            return 1;
        }
        else
        {
            pPosition = strchr(argv[3], '=');
            memcpy(IOTypeString, pPosition+1, strlen(pPosition+1));
        }

        //get connection type
        if(!strcmp(IOTypeString, "SERIAL"))
        {            
            connectionTypeOptions = SERIAL;
        }
        else if (!strcmp(IOTypeString, "USB"))
        {
            connectionTypeOptions = USB;
        }
        else
        {
            #ifdef PLATFORM_ANDROID
            fprintf(stderr,"Invalid entry, valid options are : SERIAL or USB\n");
            #else
            printf("Invalid entry, valid options are : SERIAL or USB\n");
            #endif
            return -1;
        }

		//get QDART connect type
		if (NULL == strstr(argv[4], "QDARTIOType"))
        {
            #ifdef PLATFORM_ANDROID
            fprintf(stderr,"\nMissing IOType option, valid options are SERIAL, USB\n");
            #else
            printf("\nMissing IOType option, valid options are SERIAL, USB\n");
            #endif
            return 1;
        }
        else
        {
            pPosition = strchr(argv[4], '=');
            memcpy(QDARTIOTypeString, pPosition+1, strlen(pPosition+1));
        }

		if(!strcmp(QDARTIOTypeString, "serial"))
        {            
            QDARTConnectionType = SERIAL;
        }
        else if (!strcmp(QDARTIOTypeString, "ethernet"))
        {
            QDARTConnectionType = ETHERNET;
        }
        else
        {
            #ifdef PLATFORM_ANDROID
            fprintf(stderr,"Invalid entry, valid options are : serial or ethernet\n");
            #else
            printf("Invalid entry, valid options are : serial or ethernet\n");
            #endif
            return 1;
        }
	
		if (NULL == strstr(argv[5], "BT-DEVICE"))
        {
            #ifdef PLATFORM_ANDROID
            fprintf(stderr,"\nSpecifies the serial device to attach is not valid\n");
            #else
            printf("\nSpecifies the serial device to attach is not valid\n");
            #endif
            return 1;
        }
        else
        {
            pPosition = strchr(argv[5], '=');
            memcpy(Device, pPosition+1, strlen(pPosition+1));
        }

        if (connectionTypeOptions == SERIAL)
        {
			if (argc > 6)
			{
				//bluetooth serial baudrate.
                if (NULL == strstr(argv[6], "BT-BAUDRATE"))
                {
                    #ifdef PLATFORM_ANDROID
                    fprintf(stderr,"\nMissing baudrate option\n");
                    #else
                    printf("\nMissing baudrate option\n");
                    #endif
                    return 1;
                }
                else
                {
                    pPosition = strchr(argv[6], '=');
                    memcpy(baudrate, pPosition+1, strlen(pPosition+1));
                }
			}
			else 
            {
            #ifdef PLATFORM_ANDROID
            fprintf(stderr,"\nMissing baudrate option\n");
            #else
            printf("\nMissing baudrate option\n");
            #endif
                return 1;
            }

			if ( QDARTConnectionType == SERIAL )
			{
				if (argc > 7)
	            {
	            	//connect COM number between QDART and btdiag.
					if (NULL == strstr(argv[7], "QDART-DEVICE"))
	                {
	                     #ifdef PLATFORM_ANDROID
                            fprintf(stderr,"\nMissing QDART connect type serial device option\n");
                            #else
                            printf("\nMissing QDART connect type serial device option\n");
                            #endif
	                    return 1;
	                }
	                else
	                {
	                    pPosition = strchr(argv[7], '=');
	                    memcpy(ComName, pPosition+1, strlen(pPosition+1));
	                }
					//connect COM baudrate between QDART and btdiag.
					if (NULL == strstr(argv[8], "QDART-BAUDRATE"))
	                {
	                     #ifdef PLATFORM_ANDROID
                            fprintf(stderr,"\nMissing QDART connect type serial baudrate option\n");
                            #else
                            printf("\nMissing QDART connect type serial baudrate option\n");
                            #endif
	                    return 1;
	                }
	                else
	                {
	                    pPosition = strchr(argv[8], '=');
	                    memcpy(ComBaudrate, pPosition+1, strlen(pPosition+1));
	                }
				}
				else 
           		{
                            #ifdef PLATFORM_ANDROID
                            fprintf(stderr,"\nMissing QDART serial option\n");
                            #else
                            printf("\nMissing QDART serial option\n");
                            #endif
                            return 1;
            	}
		}
        //require to download patch and NVM files
	        isPatchandNvmRequired = 1;
        }
		else if (connectionTypeOptions == USB)
		{
        	//btdiag can communicate with QDART by serial or ethernet,extract the parameters.
			if ( QDARTConnectionType == SERIAL )
			{
				if (argc > 5)
	            {
	            	//connect COM number between QDART and btdiag.
					if (NULL == strstr(argv[6], "QDART-DEVICE"))
	                {
	                    #ifdef PLATFORM_ANDROID
                            fprintf(stderr,"\nMissing QDART connect type serial device option\n");
                            #else
                            printf("\nMissing QDART connect type serial device option\n");
                            #endif
	                    return 1;
	                }
	                else
	                {
	                    pPosition = strchr(argv[6], '=');
	                    memcpy(ComName, pPosition+1, strlen(pPosition+1));
	                }
					//connect COM baudrate between QDART and btdiag.
					if (NULL == strstr(argv[7], "QDART-BAUDRATE"))
	                {
	                    #ifdef PLATFORM_ANDROID
                            fprintf(stderr,"\nMissing QDART connect type serial baudrate option\n");
                            #else
                            printf("\nMissing QDART connect type serial baudrate option\n");
                            #endif
	                    return 1;
	                }
	                else
	                {
	                    pPosition = strchr(argv[7], '=');
	                    memcpy(ComBaudrate, pPosition+1, strlen(pPosition+1));
	                }
				}
				else 
            	{
                    #ifdef PLATFORM_ANDROID
                    fprintf(stderr,"\nMissing QDART serial option\n");
                    #else
                    printf("\nMissing QDART serial option\n");
                    #endif
                    return 1;
            	}
			}
        }
		if ( QDARTConnectionType == SERIAL )
		{
			if (uart_port.device[0] == 0)
				SetUartParameter(&uart_port, ComName, DEVICE);
			if (uart_port.baudrate == 0)
				SetUartParameter(&uart_port, ComBaudrate, BAUDRATE);
			
			uart_port.fd = InitUartPort(&uart_port);
			
			if (uart_port.fd < 0) {
                            #ifdef PLATFORM_ANDROID
                            fprintf(stderr,"Uart: Init serial port fail!\n");
                            #else
                            printf("Uart: Init serial port fail!\n");
                            #endif
				exit(1);
			}
			Diag_Read_Uart();
		}
		else if ( QDARTConnectionType == ETHERNET )
		{
	        //transport packet by ethernet,start TCP client and creat socket
	        sock = socket(AF_INET , SOCK_STREAM , 0);
	        if (sock == -1)
	        {
	        #ifdef PLATFORM_ANDROID
                fprintf(stderr,"Could not create socket");
                #else
                printf("Could not create socket");
                #endif
	        }
	        #ifdef PLATFORM_ANDROID
               fprintf(stderr,"Socket created\n");
               #else
               puts("Socket created\n");
               #endif

	        server.sin_addr.s_addr = inet_addr(ipAddress);
	        server.sin_family = AF_INET;
	        server.sin_port = htons( portNumber );

	        //Connect to remote server
	        #ifdef PLATFORM_ANDROID
               fprintf(stderr,"Connecting...\n");
               #else
	        printf("Connecting.....\n");
	        #endif
	        if (connect(sock , (struct sockaddr *)&server , sizeof(server)) < 0)
	        {
	            printf("\n\nFailed to connect to QPST, please make sure QPST is open and configured with above listed IP address and port number\n");
	            perror("connect failed. Error");
	            return 1;
	        }
	        else
	        {
	            #ifdef PLATFORM_ANDROID
                    fprintf(stderr,"Connected\n");
                    #else
                    puts("Connected\n");
                    #endif
	        }

	        //Buffer for data from stream
	        BYTE rxbuf[TCP_BUF_LENGTH];
	        memset(rxbuf, 0, TCP_BUF_LENGTH);

	        //Listening loop
	        while(1)
	        {
	            #ifdef PLATFORM_ANDROID
                   fprintf(stderr,"Waiting for QPST connection... \n");
                   #else
	            printf("Waiting for QPST connection... \n");
	            #endif

	            //starting and making connection to SoC DUT
	            switch (connectionTypeOptions)
	            {
	                case SERIAL:
		    	    descriptor = SerialConnection(Device, baudrate);
	                break;
	                case USB:
	                #ifdef BLUEDROID_STACK
	                descriptor = bluedroid_usb_connection();
	                fprintf(stderr,"%d descriptor",descriptor);
	                #else
	                descriptor = USBConnection(Device);
	                #endif
	                break;
	            }

	            if (descriptor <= 0)
	            {
	                #ifdef PLATFORM_ANDROID
	                fprintf(stderr,"\nFailed to connect to external device on\n");
	                #else
	                printf("\nFailed to connect to external device on\n");
	                #endif
	                return 1;
	            }
	            else
	            {
	                #ifdef PLATFORM_ANDROID
	                fprintf(stderr,"Connected to SoC DUT!\n");
	                #else
	                printf("Connected to SoC DUT!\n");
	                #endif
                    }
	            /* Create thread to recive HCI event */
	            {
	                //Check: move it to subfunction.
	                //Create a thread for receiving HCI event from DUT.
	                int err;
	                pthread_t thread;

	                if (connectionTypeOptions == SERIAL)
	                    args.desc = descriptor;
	                else if (connectionTypeOptions == USB)
	                    args.desc = descriptor;

	                args.buf = databuf;
	                args.size = HCI_MAX_EVENT_SIZE;

	                err = pthread_create(&thread, NULL, &NotifyObserver, (void *)&args);
	                if (err != 0)
	                {
	                    printf("\nCan't create thread :[%s]", strerror(err));
	                    break;
	                }
	                else
	                {
	                    #ifdef PLATFORM_ANDROID
	                    fprintf(stderr,"\nThread created successfully\n");
	                    #else
	                    printf("\nThread created successfully\n");
	                    #endif
	                 }
	            }

	            // Loop to receive all the data sent by the QMSL/QRCT(via QPST), as long as it is greater than 0
	            int lengthOfStream;
	            while ((lengthOfStream = recv(sock, rxbuf, TCP_BUF_LENGTH, 0)) != 0)
	            {
	                processDiagPacket(rxbuf);

	                //initiate buf
	                memset(rxbuf, 0, TCP_BUF_LENGTH);
	            }

	            //checking if server is still on, if not close the application
	            if (lengthOfStream == 0)
	            {
	                #ifdef PLATFORM_ANDROID
	                fprintf(stderr,"\n>>>QPST/Server is closed, closing application\n");
	                #else
	                printf("\n>>>QPST/Server is closed, closing application\n");
	                #endif
	                close(sock);
	                return 0;
	            }
	        }
		}
    }
	else  //UDT mode: User Defined Transport mode
    {
        //PORT
        if (NULL == strstr(argv[2], "PORT"))
        {
            #ifdef PLATFORM_ANDROID
            fprintf(stderr,"\nMissing QPST server port number\n");
            #else
            printf("\nMissing QPST server port number\n");
            #endif
            return 1;
        }
        else
        {
            pPosition = strchr(argv[2], '=');
            memcpy(tempStr, pPosition+1, strlen(pPosition+1));
            portNumber = atoi(tempStr);
        }

        //IOType
        if (NULL == strstr(argv[3], "IOType"))
        {
            #ifdef PLATFORM_ANDROID
            fprintf(stderr,"\nMissing IOType option, valid options are SERIAL, USB\n");
            #else
            printf("\nMissing IOType option, valid options are SERIAL, USB\n");
            #endif
            return 1;
        }
        else
        {
            pPosition = strchr(argv[3], '=');
            memcpy(IOTypeString, pPosition+1, strlen(pPosition+1));
        }

        //get connection type
        if(!strcmp(IOTypeString, "SERIAL"))
        {            
            connectionTypeOptions = SERIAL;
        }
        else if (!strcmp(IOTypeString, "USB"))
        {
            connectionTypeOptions = USB;
        }
        else
        {
            #ifdef PLATFORM_ANDROID
            fprintf(stderr,"Invalid entry, valid options are : SERIAL or USB\n");
            #else
            printf("Invalid entry, valid options are : SERIAL or USB\n");
            #endif
            return -1;
        }
		//get QDART connect type
		if (NULL == strstr(argv[4], "QDARTIOType"))
        {
            #ifdef PLATFORM_ANDROID
            fprintf(stderr,"\nMissing IOType option, valid options are SERIAL, USB\n");
            #else
            printf("\nMissing IOType option, valid options are SERIAL, USB\n");
            #endif
            return 1;
        }
        else
        {
            pPosition = strchr(argv[4], '=');
            memcpy(QDARTIOTypeString, pPosition+1, strlen(pPosition+1));
        }
		if(!strcmp(QDARTIOTypeString, "serial"))
        {            
            QDARTConnectionType = SERIAL;
        }
        else if (!strcmp(QDARTIOTypeString, "ethernet"))
        {
            QDARTConnectionType = ETHERNET;
        }
        else
        {
            printf("Invalid entry, valid options are : serial or ethernet\n");
            return 1;
        }
		if (NULL == strstr(argv[5], "BT-DEVICE"))
        {
            #ifdef PLATFORM_ANDROID
            fprintf(stderr,"\nSpecifies the serial device to attach is not valid\n");
            #else
            printf("\nSpecifies the serial device to attach is not valid\n");
            #endif
            return 1;
        }
        else
        {
            pPosition = strchr(argv[5], '=');
            memcpy(Device, pPosition+1, strlen(pPosition+1));
        }

        if (connectionTypeOptions == SERIAL)
        {
			if (argc > 6)
			{
				//bluetooth serial baudrate.
                if (NULL == strstr(argv[6], "BT-BAUDRATE"))
                {
                    #ifdef PLATFORM_ANDROID
                    fprintf(stderr,"\nMissing BT baudrate option\n");
                    #else
                    printf("\nMissing BT baudrate option\n");
                    #endif
                    return 1;
                }
                else
                {
                    pPosition = strchr(argv[6], '=');
                    memcpy(baudrate, pPosition+1, strlen(pPosition+1));
                }
			}
			else 
            {
                #ifdef PLATFORM_ANDROID
                fprintf(stderr,"\nMissing BT baudrate option\n");
                #else
                printf("\nMissing BT baudrate option\n");
                #endif
                return 1;
            }

			if ( QDARTConnectionType == SERIAL )
			{
				if (argc > 7)
	            {
	            	//serial COM number between QDART and btdiag.
					if (NULL == strstr(argv[7], "QDART-DEVICE"))
	                {
	                    printf("\nMissing QDART connect type serial option\n");
	                    return 1;
	                }
	                else
	                {
	                    pPosition = strchr(argv[7], '=');
	                    memcpy(ComName, pPosition+1, strlen(pPosition+1));
	                }
					//serial baudrate between QDART and btdiag.
					if (NULL == strstr(argv[8], "QDART-BAUDRATE"))
	                {
	                    #ifdef PLATFORM_ANDROID
                           fprintf(stderr,"\nMissing QDART connect type serial baudrate option\n");
                           #else
                           printf("\nMissing QDART connect type serial baudrate option\n");
                           #endif
	                    return 1;
	                }
	                else
	                {
	                    pPosition = strchr(argv[8], '=');
	                    memcpy(ComBaudrate, pPosition+1, strlen(pPosition+1));
	                }
				}
				else 
           		{
                    #ifdef PLATFORM_ANDROID
                    fprintf(stderr,"\nMissing QDART serial option\n");
                    #else
                    printf("\nMissing QDART serial option\n");
                    #endif
                    return 1;
            	}
		}
	        //require to download patch and NVM files
	        isPatchandNvmRequired = 1;
        }
		else if (connectionTypeOptions == USB)
		{
        	//btdiag can communicate with QDART by serial or ethernet,extract the parameters.
			if ( QDARTConnectionType == SERIAL )
			{
				if (argc > 5)
	            {
	            	//connect COM number between QDART and btdiag.
					if (NULL == strstr(argv[6], "QDART-DEVICE"))
	                {
	                    printf("\nMissing QDART connect type COM option\n");
	                    return 1;
	                }
	                else
	                {
	                    pPosition = strchr(argv[6], '=');
	                    memcpy(ComName, pPosition+1, strlen(pPosition+1));
	                }
					//connect COM baudrate between QDART and btdiag.
					if (NULL == strstr(argv[7], "QDART-BAUDRATE"))
	                {
	                    #ifdef PLATFORM_ANDROID
                           fprintf(stderr,"\nMissing QDART connect type COM baudrate option\n");
                           #else
                           printf("\nMissing QDART connect type COM baudrate option\n");
                           #endif
	                    return 1;
	                }
	                else
	                {
	                    pPosition = strchr(argv[7], '=');
	                    memcpy(ComBaudrate, pPosition+1, strlen(pPosition+1));
	                }
				}
				else 
           		{
                    #ifdef PLATFORM_ANDROID
                    fprintf(stderr,"\nMissing QDART serial option\n");
                    #else
                    printf("\nMissing QDART serial option\n");
                    #endif
                    return 1;
            	}
			}
        }

        //starting and making connection to SoC DUT
        switch (connectionTypeOptions)
        {
            case SERIAL:
            descriptor = SerialConnection(Device, baudrate);
            break;
            case USB:
	        #ifdef BLUEDROID_STACK
	        descriptor = bluedroid_usb_connection();
		#elif defined(FLUORIDE_STACK)
		descriptor = fluoride_usb_connection(Device);
		fluoride_disable_controller_log(descriptor);
	        #else
            descriptor = USBConnection(Device);
	        #endif
            break;
        }

        if (descriptor <= 0)
        {
            #ifdef PLATFORM_ANDROID
            fprintf(stderr,"\nFailed to connect to external device on\n");
            #else
            printf("\nFailed to connect to external device on\n");
            #endif
            return 1;
        }
        else
        {
            #ifdef PLATFORM_ANDROID
            fprintf(stderr,"Connected to SoC DUT!\n");
            #else
            printf("Connected to SoC DUT!\n");
            #endif
        }

        /* Create thread to recive HCI event */
        {
            //Check: move it to subfunction.
            //Create a thread for receiving HCI event from DUT.
            int err;
            pthread_t thread;

            if (connectionTypeOptions == SERIAL)
                args.desc = descriptor;
            else if (connectionTypeOptions == USB)
                args.desc = descriptor;

            args.buf = databuf;
            args.size = HCI_MAX_EVENT_SIZE;

            err = pthread_create(&thread, NULL, &NotifyObserver, (void *)&args);
            if (err != 0)
            {
                printf("\nCan't create thread :[%s]", strerror(err));
                //break;
            }
            else
            {
                #ifdef PLATFORM_ANDROID
                fprintf(stderr,"\nThread created successfully\n");
                #else
                printf("\nThread created successfully\n");
                #endif
            }
        }

		if ( QDARTConnectionType == SERIAL )
		{
			if (uart_port.device[0] == 0)
				SetUartParameter(&uart_port, ComName, DEVICE);
			if (uart_port.baudrate == 0)
				SetUartParameter(&uart_port, ComBaudrate, BAUDRATE);
			
			uart_port.fd = InitUartPort(&uart_port);
			
			if (uart_port.fd < 0) {
                    #ifdef PLATFORM_ANDROID
                    fprintf(stderr,"Uart: Init serial port fail!\n");
                    #else
                    printf("Uart: Init serial port fail!\n");
                    #endif
				exit(1);
			}
			Diag_Read_Uart();
		}
		else if ( QDARTConnectionType == ETHERNET )
		{
	        /* start TCP server */
	        //Create socket
	        sock = socket(AF_INET , SOCK_STREAM , 0);
	        if (sock == -1)
	        {
	            #ifdef PLATFORM_ANDROID
	            fprintf(stderr,"Could not create socket");
	            #else
	            printf("Could not create socket\n");
	            #endif
	        }
	        #ifdef PLATFORM_ANDROID
	        fprintf(stderr,"Socket created\n");
	        #else
	        printf("Socket created\n");
	        #endif

	        server.sin_addr.s_addr = INADDR_ANY; //inet_addr(ipAddress);
	        server.sin_family = AF_INET;
	        server.sin_port = htons( portNumber );
			
			//For fixing "Address already in use" issue. 
			//(When client keeps connection to server, terminate server and reopen it will make "Address already in use" issue occur.)
			int opt = 1;
			setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	        //Bind
	        if( bind(sock,(struct sockaddr *)&server , sizeof(server)) < 0)
	        {
	            //print the error message
	            perror("bind failed. Error");
	            //TODO:check
	            close(sock);
	            return 1;
	        }
	        #ifdef PLATFORM_ANDROID
	        fprintf(stderr,"bind done\n");
	        #else
	        printf("bind done\n");
	        #endif

	        //Listen
	        listen(sock , 3);

	        //Accept and incoming connection
	        #ifdef PLATFORM_ANDROID
	        fprintf(stderr,"Waiting for incoming connections... \n");
	        #else
	        printf("Waiting for incoming connections... \n");
	        #endif
	        clientaddr_size  = sizeof(struct sockaddr_in);

			//accept connection from an incoming client
	        client_sock = accept(sock, (struct sockaddr *)&client, (socklen_t*)&clientaddr_size);
	        if (client_sock < 0)
	        {
	            perror("accept failed");
	            return 1;
	        }
	        #ifdef PLATFORM_ANDROID
	        fprintf(stderr,"Connected!");
	        #else
	        printf("Connected!");
	        #endif

			//Buffer for data from stream
	        BYTE incomingDataBytes[TCP_BUF_LENGTH];
	        memset(incomingDataBytes, 0, TCP_BUF_LENGTH); 


	        // Loop to receive all the data sent by the QMSL/QRCT(via QPST), as long as it is greater than 0
	        int lengthOfStream;
	        while ((lengthOfStream = recv(client_sock, incomingDataBytes, TCP_BUF_LENGTH, 0)) != 0)
	        {
	            if (lengthOfStream > 0)
	            processDiagPacket(incomingDataBytes);

	            //initiate incomingDataBytes buf
	            memset(incomingDataBytes, 0, TCP_BUF_LENGTH);
	        }

	        if(lengthOfStream == 0)
	        {
	            #ifdef PLATFORM_ANDROID
	            fprintf(stderr,"Client disconnected");
	            #else
	            puts("Client disconnected");
	            #endif
	            fflush(stdout);
	        }
	        else if(lengthOfStream == -1)
	        {
	            perror("recv failed");
	        }
		}
    }
    #ifdef BLUEDROID_STACK
    bluedroid_clean( );
    #endif
    #ifdef BLUETOPIA_STACK
    CloseStack();
    #endif
    return 0;
}

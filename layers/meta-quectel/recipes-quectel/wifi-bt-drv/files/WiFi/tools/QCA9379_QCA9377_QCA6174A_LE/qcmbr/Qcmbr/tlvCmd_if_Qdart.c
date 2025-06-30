/* 
Copyright (c) 2013 Qualcomm Atheros, Inc.
All Rights Reserved. 
Qualcomm Atheros Confidential and Proprietary. 
*/  

/* tlvCmd_if_Qdart.c - Interface to DevdrvIf.DLL ( DevdrvIf.DLL access device driver ) */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "Qcmbr.h"

//  Remote error number and error string
A_INT32 remoteMdkErrNo = 0;
A_CHAR remoteMdkErrStr[SIZE_ERROR_BUFFER];

#ifdef LINUX_X86

#ifndef ANDROID_X86
#include <ifaddrs.h>
#endif
char *ifa_name = NULL;
#endif

// holds the cmd replies sent over channel
static CMD_REPLY cmdReply;
static A_BOOL cmdInitCalled = FALSE;

/**************************************************************************
* receiveCmdReturn - Callback function for calling cmd_init().
*       Note: We are keeping the calling convention.
*       Note: cmd_init() is a library function from DevdrvIf.DLL.
*		Note: Qcmbr does not ( need to ) do anything to the data or care
*			  what is in the data.
*/
void receiveCmdReturn(void *buf)
{
        printf("CallBack-receiveCmdReturn bufAddr[%8.8X]\n",(unsigned int)buf);
	if ( buf == NULL )
	{
	}
	// Dummy call back function
}

#ifdef LINUX_X86

#ifndef ANDROID_X86
void get_wifi_ifname()
{
    struct ifaddrs *addrs, *tmp;

    getifaddrs(&addrs);
    tmp = addrs;

    while(tmp) {
        if (tmp->ifa_addr && tmp->ifa_addr->sa_family == AF_PACKET &&
            memcmp(tmp->ifa_name, "wlan", 4) == 0) {
            ifa_name = malloc(strlen(tmp->ifa_name) + 1);
            memset(ifa_name, 0x0, strlen(tmp->ifa_name) + 1);
            if (ifa_name) {
                memcpy(ifa_name, tmp->ifa_name, strlen(tmp->ifa_name));
                printf("%s (%d)\n", ifa_name, strlen(ifa_name));
                break;
            }
        }

        tmp = tmp->ifa_next;
    }

    freeifaddrs(addrs);
}
#endif

#endif

#ifdef QCMBR_ENABLE_SOFTMAC

#define CMD_STREAM_VER1                       1
#define CMD_STREAM_VER_LATEST                 CMD_STREAM_VER1
#define CMD_PAYLOAD_LEN_MAX                   1 * 512
#define CMD_STERAM_HEADER_EXTENDED_RESERVED     0x00000080
#define CMD_STREAM_HEADER_EXTENDED_BYTESTREAM_BYPASS     0x00000001
#define MAX_LEN 128
#define SOFT_MAC "/lib/firmware/ath6k/AR6004/hw3.0/softmac.bin"
#define ATH_MAC_LEN 6
#define MAX_NUM_VALUES  ATH_MAC_LEN
#define M_SET_MAC_ADDR_CMD_ID 197
#define M_GET_MAC_ADDR_CMD_ID 94
#define COMMS_ERR_SHIFT 0
#define _OP_GENERIC_NART_RSP 0x9
#define _OP_TEST_CONFIG 255
#define KEY_SIZE_MAX 16
#define TC_CMD_TLV_ID 0x5
#define NART_CMDID 0x8

enum{
    RET_FALSE = 0,  
    RET_TRUE,       
};
#define __ATTRIB_PACK __attribute__ ((packed))
typedef struct _testFlowCmdStreamHeader {
    unsigned int    cmdId;  // reserve this for compatibility with TCMD..
    unsigned char     version;
    unsigned char     header;
    unsigned short    length; // payload length
    unsigned short    checkSum;
    unsigned short    headerDepValue;
    unsigned int    headerExtended;
}__ATTRIB_PACK _TESTFLOW_CMD_STREAM_HEADER;

typedef struct _testFlowCmdStream {
    _TESTFLOW_CMD_STREAM_HEADER cmdStreamHeader;
    unsigned char                     payload[CMD_PAYLOAD_LEN_MAX];
}__ATTRIB_PACK _TESTFLOW_CMD_STREAM;

typedef struct _oneCmdHeader {
    unsigned char     opCode;
    unsigned char     numOfParms;
    unsigned short    headerReserved;
}__ATTRIB_PACK  _ONE_CMD_HEADER;

typedef struct _nartCmdParm {
    unsigned int  commandId;
    unsigned int  param1;
    unsigned int  param2;
    unsigned int  param3;
    unsigned char   data[ 200 ];
}__ATTRIB_PACK _CMD_NARTCMD_PARMS;
static _CMD_NARTCMD_PARMS _nartcmdParms;

typedef struct _parmBinTemplate {
    unsigned int offset;
    unsigned int len;
}__ATTRIB_PACK _PARM_BIN_TEMPLATE;

typedef enum {
    _PARM_RESERVED = 0,
    _PARM_U8,
    _PARM_U16,
    _PARM_U32,
    _PARM_S8,
    _PARM_S16,
    _PARM_S32,
    _PARM_DATA,
} _PARM_TYPE;

typedef struct _parmVal {
    unsigned short    val16;
    unsigned int    val32;
}__ATTRIB_PACK  _PARM_VAL;

typedef struct _parmOneOf {
    unsigned char     parmCode;
    unsigned char     parmType;
    union {
        unsigned char addr[MAX_NUM_VALUES];
        _PARM_VAL value;
    } parmValue;
} __ATTRIB_PACK _PARM_ONEOF;

typedef enum {
    TESTFLOW_CMD=0,
    TESTFLOW_PARM,
    TESTFLOW_CONFIG,
    TESTFLOW_DONE,
} TESTFLOW_PARSER_STATE;

typedef struct _testFlowTxtParmTemplate {
    char     parm[KEY_SIZE_MAX];
    unsigned long parmType;
    unsigned long offset;
    size_t   len;
}__ATTRIB_PACK _TESTFLOW_TXT_PARM_TEMPLATE;

typedef struct _testFlowCmdParmTemplate {
     A_UINT32 numParms;
    _TESTFLOW_TXT_PARM_TEMPLATE *parmTemplate;
}__ATTRIB_PACK _TESTFLOW_CMD_PARM_TEMPLATE;

#define NUM_OF__nartRspParm sizeof(_nartRspParm_txt_template)/sizeof(_TESTFLOW_TXT_PARM_TEMPLATE)
_TESTFLOW_TXT_PARM_TEMPLATE _nartRspParm_txt_template[4/*NUM_OF__nartRspParm*/];
static  _TESTFLOW_CMD_STREAM testCmdStream;
static unsigned char curCmdOpcode;
static TESTFLOW_PARSER_STATE parserState;
static unsigned int numOfTemplateParms,parmIdx;
_TESTFLOW_TXT_PARM_TEMPLATE *parmTemplate;
static unsigned char *cmdStream;
static unsigned char g_responseCharBuf[MAX_RESP_DATA_SIZE];
static unsigned int g_responseSize;
static A_INT32 cmdStreamPos=0;
_PARM_BIN_TEMPLATE _nartCmdParm_bin_template[] = {
    {0, 4},
    {4, 4},
    {8, 4},
    {12, 4},
    {16, 200},
};

static int verifyChecksum(unsigned short* stream, unsigned int len)
{
    unsigned short i,sum=0;
    for (i=0;i<len;i++) { sum ^= *stream++; }
    if (0xffff != sum) return(RET_FALSE);
    return(RET_TRUE);
}

static unsigned short computeChecksumOnly(unsigned short *pHalf, unsigned short length)
{
    unsigned short sum = 0, i;
    for (i = 0; i < length; i++) { sum ^= *pHalf++; }
    return(sum);
}

static int cmdParmsParser_common(unsigned char *cmdParmBuf, unsigned char numOfParms, void*cmdParms, _PARM_BIN_TEMPLATE *_parm_bin_template)
{
    _PARM_ONEOF   *oneParm;
    unsigned int pos, offset,shift;
    size_t   size;
    unsigned int  parmCode, parmType;
    unsigned char *parmValStart;
    A_INT32 i;

    pos=0;
    for (i=0;i<numOfParms;i++) {
        oneParm = (_PARM_ONEOF *)(&cmdParmBuf[pos]);
        parmCode = oneParm->parmCode;
        parmType = oneParm->parmType;

        offset = _parm_bin_template[parmCode].offset;
        size   = _parm_bin_template[parmCode].len;
        shift = 0;

        if ((unsigned char)_PARM_U8 == parmType || _PARM_S8 == parmType)
            parmValStart = (unsigned char *)&(oneParm->parmValue.addr[0]);
        else if ((unsigned char)_PARM_DATA == parmType) {
            parmValStart = (unsigned char *)&cmdParmBuf[pos + sizeof(_PARM_ONEOF)];
            shift = size = oneParm->parmValue.value.val16;
        }
        else if ((unsigned char)_PARM_U16 == parmType || _PARM_S16 == parmType)
            parmValStart = (unsigned char *)&(oneParm->parmValue.value.val16);
        else
            parmValStart = (unsigned char *)&(oneParm->parmValue.value.val32);

        memcpy((void*)(((unsigned char *)cmdParms)+offset), (void*)parmValStart, size);

        //printf("tx pC %d pT %d of %d si %d\n", parmCode, parmType, offset, size);

        pos += sizeof(_PARM_ONEOF);
        pos += shift;
    }

    return(RET_TRUE);
}
	
static unsigned int WriteMacAddressToFile(unsigned char *buf)
{
    int fd = -1;
    int count = 0;

    fd = open(SOFT_MAC, O_WRONLY |O_CREAT | O_TRUNC, S_IRWXU | S_IRWXG | S_IRWXO);
    if(fd == -1)
    {
        printf("open file failed!\n");
        return RET_FALSE;
    }
    count = write(fd, buf, ATH_MAC_LEN);
    if(count != ATH_MAC_LEN)
    {
        printf("write mac to file failed !\n");
        return RET_FALSE;
    }
    close(fd); 
    return RET_TRUE;
}
static unsigned int processTlvSetMacAddrCmd (_CMD_NARTCMD_PARMS *pCmd, unsigned char *replyBuf, unsigned int *replyLength)
{
    return WriteMacAddressToFile(pCmd->data);
}

static unsigned int GetMacAddrFromFile(unsigned char *buf)
{
    int fd = -1;
    int count = 0;

    if( access(SOFT_MAC, R_OK) == -1 )
        return RET_FALSE;
    fd = open(SOFT_MAC, O_RDWR);
    if(fd == -1)
    {
        printf("open file failed!\e");
        return RET_FALSE;
    }
    count = read(fd, buf, ATH_MAC_LEN);
    if(count < 0)
    {
        printf("read mac from file failed!\n");
        return RET_FALSE;
    }
    else if((count != ATH_MAC_LEN))
    {
        printf("read mac valid, and re-read from otp!\n");
        return RET_FALSE;
    }
    close(fd);
    return RET_TRUE;
}

static unsigned int processTlvNVGetMacAddrCmd(_CMD_NARTCMD_PARMS *pCmd, unsigned char *replyBuf, unsigned int *replyLength)
{
    int ret;
    *replyLength = 0;
    ret = GetMacAddrFromFile(replyBuf);
    *replyLength += 6;

    return ret;
}

_TESTFLOW_TXT_PARM_TEMPLATE _nartRspParm_txt_template[] = {
    {"commandId", 3, 0, 4},
    {"status", 3, 4, 4},
    {"length", 3, 8, 4},
    {"data", 1, 12, 200},
};

_TESTFLOW_CMD_PARM_TEMPLATE TestFlowCmd2ParmTemplate = {
    NUM_OF__nartRspParm,
     _nartRspParm_txt_template
};


static int addCommand(unsigned char opCode)
{
    curCmdOpcode = (unsigned char)opCode;

    // special handling of test config
    //
    if (_OP_TEST_CONFIG == curCmdOpcode) {
        // finish all config settings until next "op"
        parserState = TESTFLOW_CONFIG;
    }
    else {
        if (TESTFLOW_CONFIG == parserState) {
            parserState = TESTFLOW_CMD;
            // no current or last command to complete, simply
            // flow to cmd state
        }

        // catch above two cases, during state transition ...
        if (TESTFLOW_CMD == parserState) {
            // beginning of a test flow command
            parmTemplate         = TestFlowCmd2ParmTemplate.parmTemplate;
            numOfTemplateParms   = TestFlowCmd2ParmTemplate.numParms;
            parmIdx = 0;
            // moving to
            parserState = TESTFLOW_PARM;

        }
    }
    return RET_TRUE;
}


static int createCommand(unsigned char opCode)
{
    int ret = TRUE;

    memset((void*)&testCmdStream, 0, sizeof(testCmdStream));

    testCmdStream.cmdStreamHeader.cmdId = TC_CMD_TLV_ID;
    testCmdStream.cmdStreamHeader.version = CMD_STREAM_VER1;
    testCmdStream.cmdStreamHeader.header = 0;
    testCmdStream.cmdStreamHeader.headerDepValue = 0;
    testCmdStream.cmdStreamHeader.headerExtended  = 0;
    testCmdStream.cmdStreamHeader.checkSum = 0;

    cmdStreamPos = sizeof(_ONE_CMD_HEADER);
    curCmdOpcode = _OP_TEST_CONFIG;
    parmIdx =0;
    parserState = TESTFLOW_CMD;
    parmTemplate = NULL;
    numOfTemplateParms = 0;

    ret = addCommand(opCode);

    return ret;
}

static int searchTestFlowParmTemplate(char *key, _TESTFLOW_TXT_PARM_TEMPLATE *parmTemplate, unsigned int numOfTemplateParms, unsigned short *parmCode, unsigned short *parmType, unsigned short *parmSize)
{
    int i;
    for (i=0;i<numOfTemplateParms;i++) {
        if (0 == strcmp(key, parmTemplate[i].parm)) {
            *parmCode=i;
            *parmType = parmTemplate[i].parmType;
            *parmSize = parmTemplate[i].len;
            return(RET_TRUE);
        }
    }
    return(RET_FALSE);
}


static int addParameterToCommandWithLen(unsigned char *key, unsigned char *value, unsigned int dataLen)
{
   unsigned short parmCode, parmType, parmSize;
   unsigned int i=0,len;
   _PARM_ONEOF parmOne;

   if (TESTFLOW_CONFIG == parserState) {
       if (0 == strcmp((char*)key, "header")) {
           testCmdStream.cmdStreamHeader.header = (unsigned char)value[0];
       }
       else if (0 == strcmp((char*)key, "headerDepValue")) {
           memcpy(&testCmdStream.cmdStreamHeader.headerDepValue,value,sizeof(unsigned short));
       }
       else if (0 == strcmp((char*)key, "headerExtended")) {
           memcpy(&testCmdStream.cmdStreamHeader.headerExtended,value,sizeof(unsigned int));
       }
       else {
           printf("Error reading test config %s\n", key);
       }
   }
   else if (TESTFLOW_PARM == parserState) {
if (searchTestFlowParmTemplate((char*)key, parmTemplate, numOfTemplateParms, &parmCode, &parmType, &parmSize)) {
           parmOne.parmCode  = (unsigned char)parmCode;
           parmOne.parmType  = (unsigned char)parmType;

           if (_PARM_U8 == parmType || _PARM_S8 == parmType ) {
               if ( parmSize <= MAX_NUM_VALUES ) {
                   for (i=0;i<parmSize;i++) {
                       parmOne.parmValue.addr[i] = (unsigned char)value[i];
                   }
               }
               else {
                   if ( dataLen ) {
                       if ( dataLen > parmSize ) {
                           printf("Given dataLen %d is greater than parmSize %d\n",(int)dataLen,(int)parmSize);
                           return RET_FALSE;
                       }
                   }
                   else {
                       dataLen = parmSize;
                   }

                   parmOne.parmType = _PARM_DATA;
                   parmOne.parmValue.value.val16 = dataLen;
                   parmOne.parmValue.value.val32 = 0;
               }
           }
           else if (_PARM_U16 == parmType || _PARM_S16 == parmType ) {
               memcpy(&parmOne.parmValue.value.val16,value,sizeof(unsigned short));
           }
           else if (_PARM_U32 == parmType || _PARM_S32 == parmType ) {
               memcpy(&parmOne.parmValue.value.val32,value,sizeof(unsigned int));
           }
           else {
               printf("Unknown parm type %d\n", parmType);
               return RET_FALSE;
           }

           if ( parmOne.parmType == _PARM_DATA )
               len = cmdStreamPos + sizeof(_PARM_ONEOF) + dataLen;
           else
               len = cmdStreamPos + sizeof(_PARM_ONEOF);

           if (len > CMD_PAYLOAD_LEN_MAX) {
printf("len is greater than CMD PAYLOAD MAX %d\n",(int)len);
               return RET_FALSE;
           }

           memcpy((void*)&(testCmdStream.payload[cmdStreamPos]), (void*)&parmOne,sizeof(_PARM_ONEOF));
           cmdStreamPos = cmdStreamPos + sizeof(_PARM_ONEOF);

           if ( parmOne.parmType == _PARM_DATA ) {
               for (i=0;i<parmSize;i++) {
                   testCmdStream.payload[cmdStreamPos + i] = (unsigned char)value[i];
               }
               cmdStreamPos = cmdStreamPos + dataLen;
           }

           parmIdx++;
       }
   }
   else {
       printf("Error: encounter parm while not in parm parsing state, %s %d\n", key, parserState);
       return RET_FALSE;
   }

    return RET_TRUE;
}

static int addParameterToCommand(unsigned char *key, unsigned char *value)
{
    return (addParameterToCommandWithLen(key,value,0));
}


static int commandComplete(unsigned char **rCmdStream, unsigned int *cmdStreamLen )
{
    unsigned short sum;
    A_BOOL ret = RET_TRUE;
    _ONE_CMD_HEADER cmdHeader;
    cmdHeader.opCode = curCmdOpcode;
    cmdHeader.numOfParms = parmIdx;
    cmdHeader.headerReserved = 0;

    memcpy((void*)&(testCmdStream.payload[0]), (void*)&cmdHeader, sizeof(_ONE_CMD_HEADER));

    // flows to
    parserState = TESTFLOW_DONE;

    // get the whole cmd stream ready.
    // either pass the whole stream down the host/dut interface, or write to a binary file
    cmdStream = (unsigned char *)&testCmdStream;
    testCmdStream.cmdStreamHeader.length = cmdStreamPos;
    *cmdStreamLen = testCmdStream.cmdStreamHeader.length +sizeof(_TESTFLOW_CMD_STREAM_HEADER);

    // computer checksum
    sum = computeChecksumOnly((unsigned short*)cmdStream, (*cmdStreamLen)/2);
    testCmdStream.cmdStreamHeader.checkSum = 0xFFFF ^ sum;


    *rCmdStream = cmdStream;
    return ret;
}


static int sendNartTlvResponse (unsigned int cmdId,unsigned char *buf, unsigned int length, unsigned int status)
{
    A_BOOL ret = RET_FALSE;
    unsigned char *rCmdStream = NULL;
    unsigned int cmdStreamLen=0;

    ret = createCommand(_OP_GENERIC_NART_RSP);

    if ( ret == RET_TRUE )
    {
        ret = addParameterToCommand((unsigned char*)"commandId",(unsigned char*)&cmdId);

        if ( ret == RET_FALSE )
            printf("add commandId failed\n");

        ret = addParameterToCommand((unsigned char*)"status",(unsigned char*)&status);

        if ( ret == RET_FALSE )
            printf("add status failed\n");

        ret = addParameterToCommand((unsigned char*)"length",(unsigned char*)&length);

        if ( ret == RET_FALSE )
            printf("add length failed\n");

        if (length)
        {
            ret = addParameterToCommandWithLen((unsigned char*)"data",(unsigned char*)buf,length);

            if ( ret == RET_FALSE )
                printf("add data failed\n");
        }

        if ( ret == RET_TRUE )
        {
            ret = commandComplete(&rCmdStream, &cmdStreamLen );

            printf("stream length ..%d\n",cmdStreamLen);

            if ( ret == RET_TRUE )
            {
                 g_responseSize = cmdStreamLen;
                 memset(g_responseCharBuf, 0, sizeof(g_responseCharBuf));
                 memcpy(g_responseCharBuf, rCmdStream, cmdStreamLen);


                 // Now let's send the response..
                 //wmi_tc_cmds_reply_event(dev->wmi,cmdStreamLen,_OP_GENERIC_NART_RSP,(A_INT8*)rCmdStream);
            }
         }
         else
         {
             printf("add Param failed sendNartResponse\n");
             return RET_FALSE;
         }
    }
    return RET_TRUE;

}

static int processNartTlvCommand (_CMD_NARTCMD_PARMS *pCmd)
{
    unsigned int cmdId = pCmd->commandId;
    unsigned int status = 0;
    unsigned int replyLength;
    unsigned char replyBuf[256];

    replyLength = 0;
    replyBuf[0] = 0;
    
    switch (cmdId) {
         case M_SET_MAC_ADDR_CMD_ID:
            status = processTlvSetMacAddrCmd(pCmd,replyBuf,&replyLength);
            break;
        case M_GET_MAC_ADDR_CMD_ID:
            status = processTlvNVGetMacAddrCmd(pCmd,replyBuf,&replyLength);
            break;
        default:
            return RET_FALSE;
   }
   if(status != RET_TRUE)
        return status;

   status = status << COMMS_ERR_SHIFT;

   return sendNartTlvResponse(cmdId,replyBuf,replyLength,status);
}

/*
    parse TLV stream, if return false, we must send stream to firmware,
    if return true, we can fill the response, and return to QCMBR.
*/
static int pre_parse_cmd( unsigned char *tlvString, unsigned int length, unsigned char* responseCharBuf, unsigned int *responseSize )
{
    int ret;
    unsigned char *payload = NULL;
    unsigned short payloadLen=0;
    unsigned short payloadPos = 0;
    unsigned char *stream = tlvString;
    unsigned int readStreamLen = length;
    unsigned short streamLen;
    _TESTFLOW_CMD_STREAM *cmdStream;
    unsigned short *pHalf=(unsigned short*)stream;
    _ONE_CMD_HEADER *oneCmdHeader;
    int numOfParms;
    unsigned char *cmdParmBuf;
    _CMD_NARTCMD_PARMS *pCmdParms;

    
    _TESTFLOW_CMD_STREAM_HEADER *pCmdStreamHeader = (_TESTFLOW_CMD_STREAM_HEADER *)&(stream[0]);
    if(pCmdStreamHeader->cmdId != TC_CMD_TLV_ID)
        return RET_FALSE;

    // check the binary cmd stream version
    if (pCmdStreamHeader->version > CMD_STREAM_VER_LATEST) {
            //_printf("unsupported cmd stream version %d\n", pCmdStreamHeader->version);
            return RET_FALSE;
    }
    cmdStream=(_TESTFLOW_CMD_STREAM *)stream;
    streamLen = sizeof(_TESTFLOW_CMD_STREAM_HEADER) + cmdStream->cmdStreamHeader.length;
    if (readStreamLen != streamLen) {
        //_printf("Incorrect stream length %d, should be %d\n", readStreamLen, streamLen);
        return RET_FALSE;
    }
     if (!verifyChecksum(pHalf, streamLen/2)) {
        //_printf("Incorrect checksum\n");
        return RET_FALSE;
    }
    //cmdParmBuf = stream + sizeof(_TESTFLOW_CMD_STREAM_HEADER);
    payloadLen = (unsigned short)(cmdStream->cmdStreamHeader.length);
    payload = cmdStream->payload;
    payloadPos = 0;
    oneCmdHeader = (_ONE_CMD_HEADER*)&(payload[payloadPos]);
    if(oneCmdHeader->opCode == NART_CMDID)
    {
        numOfParms = oneCmdHeader->numOfParms;
        payloadPos += sizeof(_ONE_CMD_HEADER);
        cmdParmBuf = &(payload[payloadPos]); 
        pCmdParms = (_CMD_NARTCMD_PARMS *)&_nartcmdParms;
        memset((void*)pCmdParms,0, sizeof(_CMD_NARTCMD_PARMS));
        ret = cmdParmsParser_common(cmdParmBuf, numOfParms, pCmdParms, _nartCmdParm_bin_template);
        if(ret != RET_TRUE)
                return ret;
        ret = processNartTlvCommand(pCmdParms);
        if(ret != RET_TRUE)
                return ret;
        *(unsigned int *)responseCharBuf = g_responseSize;
        *responseSize = g_responseSize + 4;
        memcpy(responseCharBuf + 4, g_responseCharBuf, g_responseSize);

        return RET_TRUE;
    }
    return RET_FALSE;
}

#endif
/*============================================*/

/**************************************************************************
* artSendCmd2 - This function sends the TLV command passing from host (caller)
*				to the device interface function.
*				
*/


A_BOOL artSendCmd2( A_UINT8 *pCmdStruct, A_UINT32 cmdSize, unsigned char* responseCharBuf, unsigned int *responseSize )
{
    int		errorNo;
    char buf[MBUFFER + 8];

    extern void receiveCmdReturn1(void *buf);
    extern void DispHexString(A_UINT8 *pCmdStruct,A_UINT32 cmdSize);
    DispHexString(pCmdStruct,cmdSize);

    memset(buf, 0, sizeof(buf));

    if (cmdInitCalled == FALSE)
    {
#ifdef LINUX_X86
#ifndef ANDROID_X86
        get_wifi_ifname();
#endif
        if (ifa_name != NULL)
            errorNo = cmd_init(ifa_name,receiveCmdReturn1);
        else
            errorNo = cmd_init("wlan0",receiveCmdReturn1);
#else
        errorNo = cmd_init("wifi0",receiveCmdReturn1);
#endif

    	cmdInitCalled = TRUE;
    }

    memcpy(&buf[8],pCmdStruct,cmdSize);

    printf( "arSendCmd2->cmd_send2 RspLen [%d]\n", *responseSize );
#ifdef QCMBR_ENABLE_SOFTMAC
    if(pre_parse_cmd(pCmdStruct, cmdSize, responseCharBuf, responseSize) == RET_FALSE)
#endif
        cmd_send2( buf, cmdSize, responseCharBuf, responseSize );

    remoteMdkErrNo = 0;
    errorNo = (A_UINT16) (cmdReply.status & COMMS_ERR_MASK) >> COMMS_ERR_SHIFT;
    if (errorNo == COMMS_ERR_MDK_ERROR)
    {
        remoteMdkErrNo = (cmdReply.status & COMMS_ERR_INFO_MASK) >> COMMS_ERR_INFO_SHIFT;
        strncpy(remoteMdkErrStr,(const char *)cmdReply.cmdBytes,SIZE_ERROR_BUFFER);
	printf("Error: COMMS error MDK error for command DONT_CARE\n" );
        return TRUE;
    }

    // check for a bad status in the command reply
    if (errorNo != CMD_OK)
	{
	printf("Error: Bad return status (%d) in client command DONT_CARE response!\n", errorNo);
        return FALSE;
    }

    return TRUE;
}



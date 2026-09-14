/*

    *Copyright :

    *Copyright (c) 2021, Quectel Wireless Solutions Co., Ltd. All rights reserved.

    *Quectel Wireless Solutions Proprietary and Confidential.
*/
/**
 * @file ql_qtemp_cmd.c
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2021-03-23
 * 
 * 
 */
#include "stdio.h"
#include <stdlib.h>
#include <string.h>
#include "ql_temp_cmd.h"

#include "../atcid.h"
#include "../atcid_util.h"
#include "../atcid_serial.h"
#include "../at_tok.h"

#define  RESULT_SIZE       700

static char* quec_get_line_from_file(const char* file_name, int line, char* buf, int len)
{
  FILE* file = fopen(file_name, "r");
  char* ret;
  int i;
  
  if (file == NULL)
    return NULL;

  while (line-- > 0)
  {
    ret = fgets(buf, len, file);
    if (ret == NULL)
      break;
  }
  
  ret = fgets(buf, len, file);
  fclose(file);

  if (ret == NULL)
  {
    return NULL;
  }
    
  for (i=0; i<len; i++)
  {
    if (buf[i] == '\r' || buf[i] == '\n')
    {
      buf[i] = 0;
      return ret;
    }
  }

  buf[len-1] = 0;
  
  return ret;
}



static char *quec_get_internal_temp(unsigned char from,unsigned char end)
{
   char temp[10] = {0};// temperature
   char type[20] = {0};// sensor name 
   char filename[64]= {0};
   unsigned char thermal_zone_start = from;
   unsigned char thermal_zone_end  = end;
   int temperature = 0;
   char *result = (char *)malloc(RESULT_SIZE);
   if(!result)
   {
      LOGATCI(LOG_ERR,"Cannot allocate memory !");
      return NULL;
   }

   memset((void *)result,0,RESULT_SIZE);

   if((from < 0) || (end > 20) || (from > end))
   {
      LOGATCI(LOG_ERR,"Invalid argument!");
      free(result);
      return NULL;
   } 
   
   for(unsigned char index = thermal_zone_start; index <= thermal_zone_end; index++)
   {
      memset(type, 0, sizeof(type));
      memset(temp, 0, sizeof(temp));
      memset(filename,0,sizeof(filename));

      snprintf(filename,64-1,"/sys/class/thermal/thermal_zone%d/temp",index);
      quec_get_line_from_file((const char*)filename,0,temp,sizeof(temp)); 
      
      memset(filename,0,sizeof(filename));

      snprintf(filename,64-1,"/sys/class/thermal/thermal_zone%d/type",index);
      quec_get_line_from_file((const char*)filename,0,type,sizeof(type)); 
      
      temperature = atoi(temp);
      
      snprintf(result+strlen(result), RESULT_SIZE - strlen(result), "\r\n+QTEMP:\"%s\",\"%.1f\"\r\n", type, (float)temperature/1000);
   }
   return result; 
}


ATRESPONSE_t QL_AT_QTEMP_Handle(char* cmdline, ATOP_t at_op, char* response)
{
    char  *data= NULL;

    LOGATCI(LOG_ERR,"handle cmdline:%s,at_op = %x", cmdline,(int)at_op);
    switch (at_op) 
    {
        case AT_ACTION_OP:
            data = quec_get_internal_temp(0,20);
            if(!data)
            {
               goto  error;
            }
            strncpy(response,(const char *)data,strlen(data)+1);
            free(data);
            return AT_OK;
        
        case AT_READ_OP:
        case AT_TEST_OP:
        default:
            break;
    }
    return AT_OK;
error:
    return AT_ERROR;
}




#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <unistd.h>
#include <linux/reboot.h>
#include <sys/reboot.h>
#include <dirent.h>
#include <pthread.h>
#include <errno.h>

#include "ql_common_api.h"
#include "scripts/audio_loop_sh.h"
#include "scripts/gpio_led_on_sh.h"
#include "scripts/gpio_led_off_sh.h"

#include "../atcid.h"
#include "../atcid_util.h"
#include "../atcid_serial.h"
#include "../at_tok.h"

#include "quectel-buildconfig-gen.h"
#include "ql_at_factory.h"
#include "ql_atcmd_info.h"

#define SYS_PCI_DEV_PATH "/sys/bus/pci/devices/"
#define CALI_INFO_NR5G_CMD "AT+EGMC=1,\"query_rf_cal_status\",\"NR\""
#define MAC_BIN_FILE  "/var/persist/wlan_mac.bin"

#define FCT_MAX_DATA_SIZE 128
#define MAX_RESPONSE_LEN 128
#define MAX_READ_LEN 10000

void quec_set_sim_slot(int);
int quec_get_sim_slot();

extern int sendATCommandToServiceWithResult(char* line);
extern int sendDataToRildSync(char* line, char* response);

extern Quec_wifi_fsg_data  *qwifi_data;
extern long g_baud;
extern int g_databits;
extern int g_stopbits;
extern int g_parity;

extern Serial serial;

/* Hardcoded platform name per release requirement: ATI/AT+QGMR must return
 * "QCS6490" (the SoC platform), NOT the buildconfig project name "QSM565DWF". */
char proj_name[] =  "QCS6490";
char proj_rev[] = QUECTEL_PROJECT_REV;
char proj_svn[] = QUECTEL_CUSTOM_NAME;

static void fillterString(char * str,const char* substr,bool is_remove)
{
    char *pos = strstr(str, substr);
    if (pos != NULL) {
        if(is_remove){
            *pos = '\0';
        }
        else{
            *(pos + strlen(substr)) = '\0';
        }
    }
}

// Extract project name from QUECTEL_PROJECT_REV.
// e.g. "QSM565DWFPARL1A01_BP01.001_Linux6.6.38_V01"       -> "QSM565DWFPARL1A01"
//      "SG565DWFPARD1A01_SRI_BD01BP01K0M01V01_QDP_LP6.6.052.01.001V10" -> "SG565DWFPARD1A01_SRI"
// Logic: scan for '_', stop when the token after '_' is a build/version identifier (BP, BD, BL, LP, V+digit).
static int extract_proj_name(const char *rev, char *output, size_t size) {
    if (!rev || !output || size < 2) return 0;

    const char *p = rev;
    const char *cut = NULL;

    while ((p = strchr(p, '_')) != NULL) {
        const char *next = p + 1;
        if ((next[0] == 'B' && (next[1] == 'P' || next[1] == 'D' || next[1] == 'L')) ||
            (next[0] == 'L' && next[1] == 'P') ||
            (next[0] == 'V' && next[1] >= '0' && next[1] <= '9')) {
            cut = p;
            break;
        }
        p++;
    }

    size_t len = cut ? (size_t)(cut - rev) : strlen(rev);
    if (len >= size) len = size - 1;

    memcpy(output, rev, len);
    output[len] = '\0';
    return 1;
}

int extract_version(const char *input, char *output, size_t size) {
    if (input == NULL || output == NULL || size < 2) {
        return 0; 
    }

    const char *v_pos = strrchr(input, 'V'); 
    
    if (v_pos == NULL || v_pos == input) {
        return 0; 
    }

    size_t version_len = strlen(v_pos);
    
    if (version_len >= size) {
        return 0; 
    }

    strcpy(output, v_pos);
    return 1;
}

ATRESPONSE_t QL_AT_ATI_Handle(char* cmdline, ATOP_t opType, char* response) {
    if (!cmdline || !response) {
        return AT_ERROR;
    }
    LOGATCI(LOG_DEBUG, "[QL_AT_ATI_HANDLE] cmdline %s", cmdline);
    ATRESPONSE_t ret = AT_ERROR;
    
    if (strlen(cmdline) >= 2 && (strcmp(cmdline+2, "i") == 0 || strcmp(cmdline+2, "I") == 0)) {
        switch(opType) {
            case AT_BASIC_OP:
                ;
                char short_rev[128] = {0};
                if (extract_proj_name(proj_rev, short_rev, sizeof(short_rev))) {
                    if (snprintf(response, MAX_RESPONSE_LEN, "Quectel\r\n%s\r\nRevision: %s", proj_name, short_rev) > 0) {
                        ret = AT_OK;
                    }
                }
                break;
            default:
                break;
        }
    } else {
        switch(opType) {
            case AT_BASIC_OP:
                memset(response, 0, MAX_RESPONSE_LEN); 
                ret = AT_OK;
                break;
            default:
                break;
        }
    }
    
    return ret;
}


ATRESPONSE_t QL_AT_QGMR_Handle(char* cmdline, ATOP_t opType, char* response)
{
    //srcstr SG565DWFPARL1A02_BL01BP01K0M02V01_QDP_LP6.6.052.01.003V04
    //dststr SG565DWFPARL1A02_BL01BP01K0M02_QDP_LP6.6.052.01.004
    char tmpstr[512]; strcpy(tmpstr, proj_rev);
    char *p;

    p = tmpstr + strlen(tmpstr);
    if (p && p - 3 > tmpstr && p[-3] == 'V' &&
            '0' <= p[-2] && p[-2] <= '9' && '0' <= p[-1] && p[-1] <= '9') {
        memmove(p - 3, p, strlen(p)); tmpstr[strlen(tmpstr) - 3] = 0; //curr string is SG565DWFPARL1A02_BL01BP01K0M02V01_QDP_LP6.6.052.01.003
    }

    p = strchr(tmpstr, '_'); if (p) p = strchr(p + 1, '_');
    if (p && p - 3 > tmpstr && p[-3] == 'V' &&
            '0' <= p[-2] && p[-2] <= '9' && '0' <= p[-1] && p[-1] <= '9') {
        memmove(p - 3, p, strlen(p)); tmpstr[strlen(tmpstr) - 3] = 0; //curr string is SG565DWFPARL1A02_BL01BP01K0M02_QDP_LP6.6.052.01.003
    }
    strcpy(response, "Quectel\r\n");
    strcat(response, proj_name);
    strcat(response, "\r\nRevision: ");
    strcat(response, tmpstr);
    return AT_OK;
}


ATRESPONSE_t QL_AT_QAPSUB_Handle(char* cmdline, ATOP_t opType, char* response) {
    char version[10] = {0};
    
    if (!extract_version(proj_rev, version, sizeof(version))) {
        LOGATCI(LOG_DEBUG, "[QL_AT_QAPSUB_Handle] Failed to extract version, using full proj_rev");
        return AT_ERROR;
    }

    LOGATCI(LOG_DEBUG, "[QL_AT_QAPSUB_Handle] cmdline %s, version %s", cmdline, version);

    switch(opType) {
        case AT_ACTION_OP:
        case AT_READ_OP:
        case AT_TEST_OP:
            sprintf(response, "APSubEdition: %s", version);
            return AT_OK;

        default:
            break;
    }

    return AT_ERROR;
}


ATRESPONSE_t QL_AT_EGMR_Handle(char* cmdline, ATOP_t opType, char* response) {
    int ret,op,index;
    char *command;
    LOGATCI(LOG_DEBUG, "[QL_AT_EGMR_Handle] cmdline %s", cmdline);

    switch(opType) {
        case AT_SET_OP:
            ret = at_tok_nextint(&cmdline, &op);
            if (ret < 0) {
                LOGATCI(LOG_DEBUG, "[QL_AT_EGMR_Handle] failed to input op" );
                return AT_ERROR;
            }
            if(op == 1){ //write
                ret = at_tok_nextint(&cmdline, &index);
                if (ret < 0) {
                    LOGATCI(LOG_DEBUG, "[QL_AT_EGMR_Handle] failed to input index" );
                    return AT_ERROR;
                }
                if(index == 5){
                    ret = at_tok_nextstr(&cmdline, &command);
                    if (ret < 0) {
                        LOGATCI(LOG_DEBUG, "[QL_AT_EGMR_Handle] failed to input IMSI" );
                        return AT_ERROR;
                    }
                    memset(qwifi_data->sn,0,sizeof(qwifi_data->sn));
                    strncpy(qwifi_data->sn,command,strlen(command));
                    LOGATCI(LOG_DEBUG, "[QL_AT_EGMR_Handle] input is %s to %s",qwifi_data->sn,command);
                    modem_sync();
                    LOGATCI(LOG_DEBUG, "[QL_AT_EGMR_Handle] success to input IMSI" );
                    system("sync");
                    return AT_OK;
        
                }else{
                    LOGATCI(LOG_DEBUG, "[QL_AT_EGMR_Handle] index not support" );
                    return AT_ERROR;
                }

            }
            else if (op == 0){ //read
                ret = at_tok_nextint(&cmdline, &index);
                if (ret < 0) {
                    LOGATCI(LOG_DEBUG, "[QL_AT_EGMR_Handle] failed to input index" );
                    return AT_ERROR;
                }
                if(index == 5){
                    sprintf(response, "+EGMR:\"%s\"",qwifi_data->sn);
                    return AT_OK;
                }else{
                    LOGATCI(LOG_DEBUG, "[QL_AT_EGMR_Handle] index not support" );
                    return AT_ERROR;
                }
            }
            else{
                LOGATCI(LOG_DEBUG, "[QL_AT_EGMR_Handle] invaild input option" );
                return AT_ERROR;
            }

        case AT_TEST_OP:
            snprintf(response, MAX_RESPONSE_LEN, "%s", "+EGMR: (0,1),(5)");
            return AT_OK;

        default:
            break;
    }

    return AT_ERROR;
}

/************************WIFI MAC CONTROL**************************
1 save costom wifi mac file to NVM as a bin file
2 data format:
	 * Intf0MacAddress=00AA00BB00CC
	 * Intf1MacAddress=00AA00BB00CD
	 * END
3 copy file from nvm to /lib/firmware/wlan/wlan_mac.bin by script
4 start wlan.ko to use /lib/firmware/wlan/wlan_mac.bin as wifi mac
********************************************************************/

static bool is_mac_address_valid(char* mac_address) {

    int len = strlen(mac_address);
    for (int i = 0; i < len; i++) {
        if (mac_address[i] == '\"') {
            memmove(&mac_address[i], &mac_address[i+1], len-i);
            len--;
        }
    }

    // Check if length is 17 (including 5 colons)
    if (len != 17) {
        return false;
    }

    // Check if all characters are valid hexadecimal digits or colons
    for (int i = 0; i < len; i++) {
        if (i % 3 == 2) { // Check colons
            if (mac_address[i] != ':') {
                return false;
            }
        } else { // Check hexadecimal digits
            if (!isxdigit(mac_address[i])) {
                return false;
            }
        }
    }
    return true;
}

uint64_t mac_str_to_uint64(const char* mac_address) {
  uint64_t result = 0;
  char* endptr;

  char* token = strtok((char*)mac_address, ":");
  while (token != NULL) {
    uint8_t hex_byte = strtol(token, &endptr, 16);
    result = (result << 8) | hex_byte;
    token = strtok(NULL, ":");
  }

  return result;
}

static void uint64_to_mac(uint64_t num, char *mac) {
  sprintf(mac, "%02x%02x%02x%02x%02x%02x" ,
          (uint8_t)((num >> 40) & 0xFF),
          (uint8_t)((num >> 32) & 0xFF),
          (uint8_t)((num >> 24) & 0xFF),
          (uint8_t)((num >> 16) & 0xFF),
          (uint8_t)((num >> 8) & 0xFF),
          (uint8_t)(num & 0xFF));
}

void format_string(char *input_string, char *output_string) {
    int i, j;
    int length = strlen(input_string);
    char tmp[3];

    for (i = 0, j = 0; i < length; i += 2, j += 3) {
        strncpy(tmp, input_string + i, 2);
        tmp[2] = '\0';
        sprintf(output_string + j, "%s:", tmp);
    }

    output_string[j - 1] = '\0';
}


ATRESPONSE_t QL_AT_QWSETMAC_Handle(char* cmdline, ATOP_t opType, char* response){

    int fd = -1;
    int rec_size = 0;
    int rec_num = 0;
    int err;

    int rw_size;
    uint64_t mac_addr;

    char mac_buf0[14];
    char mac_buf1[14];
    char mac_str[18];
    char *command;

    char macinfo[64];

    LOGATCI(LOG_DEBUG, "[QL_AT_QWSETMAC_Handle] cmdline %s", cmdline);

    switch(opType) {
        case AT_SET_OP:
            err = at_tok_nextstr(&cmdline, &command);
            if (err < 0) {
                return AT_ERROR;
            }
            if (!is_mac_address_valid(command)) {
                printf("MAC address is not valid.\n");
                return AT_ERROR;
            }
            LOGATCI(LOG_DEBUG,"cmdline:%s,command:%s",cmdline,command);
            mac_addr = mac_str_to_uint64(command);
            LOGATCI(LOG_DEBUG,"%ld",mac_addr);
            uint64_to_mac(mac_addr, mac_buf0);
            LOGATCI(LOG_DEBUG,"%s",mac_buf0);
            uint64_to_mac(mac_addr+1, mac_buf1);
            LOGATCI(LOG_DEBUG,"%s,%s",mac_buf0,mac_buf1);
            memset(macinfo,0,64);
            sprintf(macinfo,
                    "Intf0MacAddress=%s\nIntf1MacAddress=%s\nEND\n\0",
                    mac_buf0,mac_buf1);
           // sprintf(response,macinfo);
           LOGATCI(LOG_DEBUG,"%s",macinfo);

            fd = open(MAC_BIN_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                LOGATCI(LOG_DEBUG,"open for write");
                return AT_ERROR;
            }
            LOGATCI(LOG_DEBUG,"open success");

            rw_size = write(fd, macinfo, strlen(macinfo));
            if (rw_size < 0) {
                LOGATCI(LOG_DEBUG,"write");
                close(fd);
                return AT_ERROR;
            }
            LOGATCI(LOG_DEBUG,"write success");
            close(fd);

            memset(qwifi_data->wifi_mac,0,sizeof(qwifi_data->wifi_mac));
			strncpy(qwifi_data->wifi_mac,mac_buf0,strlen(mac_buf0));
            memset(qwifi_data->bt_mac,0,sizeof(qwifi_data->wifi_mac));
			strncpy(qwifi_data->bt_mac,mac_buf1,strlen(mac_buf1));
			modem_sync();
            return AT_OK;
            break;
        case AT_READ_OP:
            fd = open(MAC_BIN_FILE, O_RDONLY);
            if (fd < 0) {
                LOGATCI(LOG_DEBUG,"open for read");
                return AT_OK;
            }

            memset(macinfo, 0, sizeof(macinfo));
            rw_size = read(fd, macinfo, sizeof(macinfo) - 1);
            if (rw_size < 0) {
                LOGATCI(LOG_DEBUG,"read");
                close(fd);
                return AT_OK;
            }

            close(fd);

            if (strncmp(macinfo, "Intf0MacAddress=", 17) != 0) {
                LOGATCI(LOG_DEBUG,"[QL_AT_QWSETMAC_Handle] No valid value, please write first");
                return AT_OK;
            }

            memcpy(mac_buf0, macinfo + 17, 12);
            mac_buf0[12] = '\0';

            format_string(mac_buf0, mac_str);
            snprintf(response, 128, "+QWSETMAC:\"%s\"\n", mac_str);

            return AT_OK;
            break;
        case AT_TEST_OP:
            return AT_OK;
            break;
        default:
            break;
    }

    return AT_ERROR;

}



static int ql_read_adc_channel(int channel)
{
    FILE *file;
    char adc_path[128];
    int value;

    switch(channel){
        case 0:
            strcpy(adc_path,"/sys/bus/iio/devices/iio:device1/in_voltage_pm7325_adc0_input");
            break;
        case 1:
            strcpy(adc_path,"/sys/bus/iio/devices/iio:device1/in_voltage_pm7325_adc1_input");
            break;
        case 2:
            strcpy(adc_path,"/sys/bus/iio/devices/iio:device1/in_voltage_pm7325_adc2_input");
            break;
        default:
            return -1;
            break;
    }
    if ((file = fopen(adc_path, "r")) != NULL) {
        fscanf(file, "%d", &value);
        fclose(file);
    }
    return value;

}



ATRESPONSE_t QL_AT_QADC_Handle(char* cmdline, ATOP_t opType, char* response){

    int adc_channel,value;
    int err = 0;

    LOGATCI(LOG_DEBUG, "[QL_AT_QADC_Handle] cmdline %s", cmdline);

    switch(opType) {
        case AT_SET_OP:
            err = at_tok_nextint(&cmdline, &adc_channel);
            if (err < 0) {
                LOGATCI(LOG_DEBUG, "[QL_AT_QADC_Handle] invaild input info" );
                return AT_ERROR;
            }
            if(adc_channel < 0||adc_channel > 2){
                LOGATCI(LOG_DEBUG, "[QL_AT_QADC_Handle] invaild channel number %d",adc_channel );
                return AT_ERROR;
            }
            value = ql_read_adc_channel(adc_channel);
            sprintf(response, "+QADC: 1,%d",value);
            return AT_OK;
            break;
        case AT_TEST_OP:
            sprintf(response, "+QADC: (0,1,2)");
            return AT_OK;
            break;
        default:
            break;
    }

    return AT_ERROR;

}

void *reboot_system(void *arg) {
    int i;
    usleep(100*1000); //Delay for OK string output
    sync();  // flushes file system buffers
    reboot(LINUX_REBOOT_CMD_RESTART);  // restarts the system
    pthread_exit(NULL);
}

static int remove_dir(char *path) {
  DIR *dir;
  struct dirent *entry;
  char filepath[256];
  int result = 0;

  if ((dir = opendir(path)) == NULL) {
    return -1;
  }

  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    sprintf(filepath, "%s/%s", path, entry->d_name);

    if (entry->d_type == DT_DIR) {
      result = remove_dir(filepath);
      if (result == -1) {
        return -1;
      }
    } else {
      if (remove(filepath) == -1) {
        result = -1;
        perror("remove");
      }
    }
  }

  closedir(dir);
  if (rmdir(path) == -1) {
    result = -1;
    perror("rmdir");
  }

  return result;
}


ATRESPONSE_t QL_AT_QSCLK_Handle(char* cmdline, ATOP_t opType, char* response) {

    LOGATCI(LOG_DEBUG, "[QL_AT_QSCLK_Handle] cmdline %s", cmdline);

    int ret,op;
    FILE *file;
    char line[10];

    switch(opType) {
        case AT_SET_OP:
            ret = at_tok_nextint(&cmdline, &op);
            if (ret < 0) {
                LOGATCI(LOG_DEBUG, "[QL_AT_QSCLK_Handle] invaild input info" );
                return AT_ERROR;
            }
            if(op == 1){
                ql_at_system("echo mem > /sys/power/autosleep");
            }
            else if (op == 0){
                ql_at_system("echo off > /sys/power/autosleep");
            }
            else{
                LOGATCI(LOG_DEBUG, "[QL_AT_QSCLK_Handle] invaild input option" );
                return AT_ERROR;
            }
            return AT_OK;
            break;
        case AT_READ_OP:
            file = fopen("/sys/power/autosleep", "r");
            if (!file) {
                LOGATCI(LOG_DEBUG, "[QL_AT_QSCLK_Handle] Unable to open file\n");
                return AT_ERROR;
            }
            while (fgets(line, 10, file)) {
                if (strstr(line, "mem")) {
                    sprintf(response, "+QSCLK: 1");
                }
                else{
                    sprintf(response, "+QSCLK: 0");
                }
            }
            fclose(file);
            return AT_OK;
            break; 
        case AT_TEST_OP:
            sprintf(response, "+QSCLK: (0,1)");
            return AT_OK;
            break;
        default:
            break;
    }

    return AT_ERROR;
}


static int ql_check_pcie(void)
{
    DIR *dir;
    struct dirent *entry;
    char dev_path[256];
    int found_pcie_dev = 0;

    dir = opendir(SYS_PCI_DEV_PATH);
    if (!dir) {
        fprintf(stderr, "Failed to open %s directory\n", SYS_PCI_DEV_PATH);
        exit(1);
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        snprintf(dev_path, sizeof(dev_path), "%s%s", SYS_PCI_DEV_PATH, entry->d_name);
        if (strstr(dev_path, "pci") != NULL &&
            strstr(dev_path, ":") != NULL &&
            strstr(dev_path, ".") != NULL) {
            LOGATCI(LOG_DEBUG,"PCIe device found: %s\n", entry->d_name);
            found_pcie_dev = 1;
        }
    }
    closedir(dir);
    if (found_pcie_dev)
        return 0;
    else
        return 1;
}


ATRESPONSE_t QL_AT_QTEST_Handle(char* cmdline, ATOP_t opType, char* response){
   
    int value;
    int ret = -1;
    int err = 0;
    char *command;
    const char*pcie_path = "/proc/bus/pci/devices";

    LOGATCI(LOG_DEBUG, "[QL_AT_QTEST_Handle] cmdline %s", cmdline);

    switch(opType) {
        case AT_SET_OP:
            err = at_tok_nextstr(&cmdline, &command);
            if (err < 0) {
                return AT_ERROR;
            }
            //do pcie control
            if (strstr(command, "pci")) {
                value = ql_check_pcie();
                if(value==1){   //no pcie found
                    sprintf(response, "\"pcie\",0");
                    return AT_OK;
                }
                else if(value == 0){ //pcie found
                    sprintf(response, "\"pcie\",1");
                    return AT_OK;   
                }
            }
            return AT_ERROR;
            break;
        case AT_TEST_OP:
            return AT_OK;
            break;
        default:
            break;
    }

    return AT_ERROR;

}

ATRESPONSE_t QL_AT_QFTCMD_Handle(char* cmdline, ATOP_t opType, char* response){
    int err = 0;
    char *command;
    const char*sd_path = "/dev/mmcblk1";

    LOGATCI(LOG_DEBUG, "[QL_AT_QFTCMD_Handle] cmdline %s", cmdline);

    switch(opType) {
        case AT_SET_OP:
            err = at_tok_nextstr(&cmdline, &command);
            if (err < 0) {
                return AT_ERROR;
            }
            if (strstr(command, "SDtest")) {
                if(access(sd_path,F_OK)!= -1){
                    sprintf(response, "+QFTCMD:SDTest success");
                    return AT_OK;
                }
                else{
                    return AT_OK;   
                }
            }
            return AT_ERROR;
            break;
        case AT_TEST_OP:
            return AT_OK;
            break;
        default:
            break;
    }

    return AT_ERROR;

}

ATRESPONSE_t QL_AT_CFUN_Handle(char* cmdline, ATOP_t opType, char* response) {
    char buffer[FCT_MAX_DATA_SIZE] = {0};
    char full_command[FCT_MAX_DATA_SIZE + 8] = {0};
    pthread_t thread;

    LOGATCI(LOG_DEBUG, "[QL_AT_CFUN_Handle] cmdline %s", cmdline);

    switch(opType) {
        case AT_SET_OP:
            if (strstr(cmdline, "1,1")) {
                if (pthread_create(&thread, NULL, reboot_system, NULL) != 0) {
                    LOGATCI(LOG_DEBUG, "[QL_AT_CFUN_Handle] Fail to create pthread" );
                    return AT_ERROR;
                }

                if (pthread_detach(thread) != 0) {
                    LOGATCI(LOG_DEBUG, "[QL_AT_CFUN_Handle] Fail to detach pthread" );
                    return AT_ERROR;
                }
                return AT_OK;
            }
            memset(buffer,0,FCT_MAX_DATA_SIZE);
            memset(full_command,0,FCT_MAX_DATA_SIZE + 8);
            strcpy(full_command,"AT+CFUN=");
            strcat(full_command,cmdline);
            sendDataToRildSync(full_command, buffer);
            snprintf(response, MAX_RESPONSE_LEN, "%s", buffer);
            return AT_QUEC_QUITE;
            break;
        case AT_TEST_OP:
            sprintf(response, "+CFUN: (0,1,4),(0,1)");
            return AT_OK;
        case AT_READ_OP:
        case AT_ACTION_OP:
        memset(buffer,0,FCT_MAX_DATA_SIZE);
        sendDataToRildSync("AT+CFUN?", buffer);
        snprintf(response, MAX_RESPONSE_LEN, "%s", buffer);
        return AT_QUEC_QUITE;
        default:
            break;
    }

    return AT_ERROR;
}

ATRESPONSE_t QL_AT_QPCIE_Handle(char* cmdline, ATOP_t opType, char* response)
{
    int err = 0;
    char *command;
    int value;

    LOGATCI(LOG_DEBUG, "[QL_AT_QPCIE_Handle] cmdline %s", cmdline);

    switch(opType) {
        case AT_SET_OP:
            err = at_tok_nextstr(&cmdline, &command);
            if (err < 0) {
                return AT_ERROR;
            }
            if (strstr(command, "link")) {
                value = ql_check_pcie();
                if(value==1){   //no pcie found
                    return AT_OK;
                }
                else if(value == 0){ //pcie found
                    sprintf(response,"linkup\r\n");
                    return AT_OK;   
                }
            }
            return AT_ERROR;
            break;
        case AT_TEST_OP:
            return AT_OK;
            break;
        default:
            break;
    }
    return AT_ERROR;

}


void *change_baudrate(void *arg) {
    int i;
    usleep(100*1000); //Delay for OK string output

    for(i=0;i<MAX_DEVICE_VCOM_NUM;i++)
    {
        if(strstr(serial.devicename[i],"ttyS")!=NULL){
            if(ql_set_baud_rate(serial.devicename[i],g_baud)!=0){
                printf("Init main uart serial port Failed");
                //return AT_ERROR;
            }
        }
    }
    pthread_exit(NULL);
}


ATRESPONSE_t QL_AT_IPR_Handle(char* cmdline, ATOP_t opType, char* response) {
    char buffer[FCT_MAX_DATA_SIZE] = {0};
    char full_command[FCT_MAX_DATA_SIZE + 8] = {0};
    int ret,baud;
    pthread_t thread;

    LOGATCI(LOG_DEBUG, "[QL_AT_IPR_Handle] cmdline %s", cmdline);

    switch(opType) {
        case AT_SET_OP:
            ret = at_tok_nextint(&cmdline, &baud);
            if (ret < 0) {
                LOGATCI(LOG_DEBUG, "[QL_AT_IPR_Handle] invaild input info" );
                return AT_ERROR;
            }
            LOGATCI(LOG_DEBUG, "New baud rate is %d\n",baud);
            g_baud = baud;

            if (pthread_create(&thread, NULL, change_baudrate, NULL) != 0) {
                LOGATCI(LOG_DEBUG, "[QL_AT_IPR_Handle] Fail to create pthread" );
                return AT_ERROR;
            }

            if (pthread_detach(thread) != 0) {
                LOGATCI(LOG_DEBUG, "[QL_AT_IPR_Handle] Fail to detach pthread" );
                return AT_ERROR;
            }

#if 0
            memset(buffer,0,FCT_MAX_DATA_SIZE);
            memset(full_command,0,FCT_MAX_DATA_SIZE + 8);
            sprintf(full_command,"AT+IPR=%d",baud);
            sendDataToRildSync(full_command, buffer);
            sprintf(response, buffer);
            return AT_QUEC_QUITE;
#else
            return AT_OK;
#endif
            break;
        case AT_TEST_OP:
            sprintf(response, "+IPR: 4800,9600,19200,38400,57600,115200,230400,460800,921600");
            return AT_OK;
            break;
        case AT_READ_OP:
            sprintf(response, "+IPR: %d",g_baud);
            return AT_OK;
            break;
        default:
            break;
    }

    return AT_ERROR;
}

ATRESPONSE_t QL_AT_QUIMSLOT_Handle(char *cmdline, ATOP_t opType, char *response)
{
    int err = 0, slot = 0;

    if (opType == AT_TEST_OP) {
	sprintf(response, "%s", "+QUIMSLOT: (1,2)");
	return AT_OK;

    } else if (opType == AT_READ_OP) {
	sprintf(response, "+QUIMSLOT: %d", quec_get_sim_slot());
	return AT_OK;

    } else if (opType == AT_SET_OP) {
        err = at_tok_nextint(&cmdline, &slot);
	if (err < 0 || (slot != 1 && slot != 2)) {
	    LOGATCI(LOG_DEBUG, "invaild slot id(%d)", slot);
	    return AT_ERROR4;
	}

	quec_set_sim_slot(slot);
	return AT_OK;
    }

    return AT_ERROR4;
}

ATRESPONSE_t QL_AT_GMI_Handle(char* cmdline, ATOP_t opType, char* response)
{
    LOGATCI(LOG_DEBUG, "[QL_AT_GMI_Handle] cmdline %s", cmdline);

    switch (opType)
    {
        case AT_ACTION_OP:
            sprintf(response, "Quectel");
            return AT_OK;
        case AT_TEST_OP:
            return AT_OK;
        default:
            break;
    }

    return AT_ERROR;
}

ATRESPONSE_t QL_AT_GMM_Handle(char* cmdline, ATOP_t opType, char* response)
{
    LOGATCI(LOG_DEBUG, "[QL_AT_GMM_Handle] cmdline %s", cmdline);

    switch (opType)
    {
        case AT_ACTION_OP:
            sprintf(response, "%s", proj_name);
            return AT_OK;
        case AT_TEST_OP:
            return AT_OK;
        default:
            break;
    }

    return AT_ERROR;
}

ATRESPONSE_t QL_AT_CSUB_Handle(char* cmdline, ATOP_t opType, char* response)
{
    printf("proj_rev %s\n", proj_rev);
    switch (opType) {
        case AT_READ_OP:
        case AT_ACTION_OP:
            {
                char *p = proj_rev;
                while ((p = strchr(p, '_')) != NULL) {
                    char *v_pos = p - 1;
                    while (v_pos >= proj_rev && *v_pos != 'V') {
                        v_pos--;
                    }
                    
                    if (v_pos >= proj_rev && *v_pos == 'V') {
                        char *digit = v_pos + 1;
                        int is_valid = 1;
                        while (digit < p) {
                            if (*digit < '0' || *digit > '9') {
                                is_valid = 0;
                                break;
                            }
                            digit++;
                        }
                        
                        if (is_valid && digit == p) {
                            int len = p - v_pos;
                            sprintf(response, "SubEdition: %.*s", len, v_pos);
                            return AT_OK;
                        }
                    }
                    p++;
                }
            }
            return AT_ERROR;
        case AT_TEST_OP:
            return AT_OK;
        default:
            printf("opType 0x%x not support\n", opType);
            break;
    }

    return AT_ERROR;
}

ATRESPONSE_t QL_AT_QHVN_Handle(char* cmdline, ATOP_t opType, char* response)
{
    LOGATCI(LOG_DEBUG, "[QL_AT_QHVN_Handle] cmdline %s", cmdline);

    switch (opType)
    {
        case AT_ACTION_OP:
            sprintf(response, "+QHVN: R1.0");
            return AT_OK;
        case AT_TEST_OP:
            return AT_OK;
        default:
            break;
    }

    return AT_ERROR;
}

ATRESPONSE_t QL_AT_QSVN_Handle(char* cmdline, ATOP_t opType, char* response)
{
    LOGATCI(LOG_DEBUG, "[QL_AT_QSVN_Handle] cmdline %s", cmdline);

    switch (opType)
    {
        case AT_ACTION_OP:
            sprintf(response, "+QSVN: %s", proj_svn);
            return AT_OK;
        case AT_TEST_OP:
            return AT_OK;
        default:
            break;
    }

    return AT_ERROR;
}



ATRESPONSE_t QL_AT_QBASELINE_Handle(char* cmdline, ATOP_t opType, char* response)
{
    LOGATCI(LOG_DEBUG,"[quectel_at_qbaseline_hdlr] handle cmdline:%s", cmdline);
    char ap_base[128] = {0};
    char cp_base[128] = {0};

    FILE *fp = NULL;
    switch (opType)
    {
        case AT_ACTION_OP:
            fp  = fopen("/etc/baseline", "r");
            if(fp == NULL)
            {
                return AT_ERROR;
            }
            else
            {
                fgets(ap_base, 128, fp);
                snprintf(response, MAX_RESPONSE_LEN, "%s", ap_base);
                fgets(cp_base, 128, fp);
                sprintf(response, "%s%s", response, cp_base);
            }
            fclose(fp);
    return AT_OK;
        break;
        default:
            return AT_ERROR;
        break;
    }

    return AT_ERROR;
}

ATRESPONSE_t QL_AT_QFCT_Handle(char* cmdline, ATOP_t opType, char* response)
{
    int err;
    char *command;
    char *flag[20];
    int op;
    char channel[256];
    int index=0;
    char bt_mac[256];
    char buf[256];

    LOGATCI(LOG_DEBUG, "[QL_AT_QFCT_Handle] cmdline %s", cmdline);
    switch(opType) {
        case AT_SET_OP:
            err = at_tok_nextstr(&cmdline, &command);
            if (err < 0) {
                return AT_ERROR;
            }
            if (strstr(command, "ble-start")) {
                start_bluetooth();
                return AT_OK;
            }else if (strstr(command, "ble-end")){
                stop_bluetooth();
                return AT_OK;
            }else if (strstr(command, "eth")){
                restart_eth();
                return AT_OK;
            }else if (strstr(command, "wifi-start")){
                start_wifi();
                return AT_OK;
            }else if (strstr(command, "wifi-end")){
                stop_wifi();
                return AT_OK;
            }else if (strstr(command, "bt")){
                err = at_tok_nextstr(&cmdline, &command);
                if (err < 0) {
                    return AT_ERROR;
                }
                if (strstr(command, "power")) {
                    err = at_tok_nextint(&cmdline, &op);
                    if (err < 0) {
                        return AT_ERROR;
                    }
                    if(op == 1){
                        start_bluetooth_power();
                        return  AT_OK;
                    }
                    else if(op == 0){
                        system("btconfig reset");
                        sleep(1);
                        return AT_OK;
                    }else{
                        LOGATCI(LOG_DEBUG, "[QL_AT_QFCT_Handle] not support input %s", command);
                        return AT_ERROR;
                    }
                    LOGATCI(LOG_DEBUG, "[QL_AT_QFCT_Handle] not support input %s", command);
                    return AT_ERROR;
                }else if (strstr(command, "br_send")){
                    err = at_tok_nextstr(&cmdline, &flag[0]);   //2
                    if (err < 0) {
                        return AT_ERROR;
                    }
                    if (!validateInput(flag[0])){
                        LOGATCI(LOG_DEBUG, "[QL_AT_QFCT_Handle] not support input %s", flag[0]);
                        return AT_ERROR;
                    }
					sprintf(channel,"0x%s 0x%s 0x%s 0x%s 0x%s",flag[0],flag[0],flag[0],flag[0],flag[0]);
					LOGATCI(LOG_DEBUG,"br_send channel:%s\n",flag[0]);
                    err = at_tok_nextstr(&cmdline, &flag[1]);   //3
                    if (err < 0) {
                        return AT_ERROR;
                    }
                    err = at_tok_nextstr(&cmdline, &flag[2]);   //4
                    if (err < 0) {
                        return AT_ERROR;
                    }
                    err = at_tok_nextstr(&cmdline, &flag[3]);   //5
                    if (err < 0) {
                        return AT_ERROR;
                    }
                    err = at_tok_nextstr(&cmdline, &flag[4]);   //6
                    if (err < 0) {
                        return AT_ERROR;
                    }
                    err = at_tok_nextstr(&cmdline, &flag[5]);   //7
                    if (err < 0) {
                        return AT_ERROR;
                    }
                    if(strlen(flag[4])>8)
                        sprintf(bt_mac,"%s",flag[4]);
                    else
					    sprintf(bt_mac,"0x9C 0x35 0xBD 0x9C 0x35 0xBD");

                    if (!validateInput(flag[1]) || !validateInput(flag[2])  || !validateInput(flag[2]) || !validateInput(flag[5])) {
                        LOGATCI(LOG_DEBUG, "[QL_AT_QFCT_Handle] not support input %s", command);
                        return AT_ERROR;
                    }
                    if(atoi(flag[5]) == 0)
					{
						sprintf(buf,"btconfig rawcmd 0x3F 0x0004 0x04 %s 0x%s 0x%s 0x00 0x%s 0x01 %s 0x00 0x1B 0x00 0x00",
						channel,flag[1],flag[2],flag[3],bt_mac);//channel 协议 功率 蓝牙mac 
					}
					else if(atoi(flag[5]) == 1)
					{
						sprintf(buf,"btconfig rawcmd 0x3F 0x0004 0x04 %s 0x%s 0x%s 0x00 0x%s 0x01 %s 0x00 0x53 0x01 0x00",
						channel,flag[1],flag[2],flag[3],bt_mac);
					}
					else if(atoi(flag[5]) == 2)
					{
						sprintf(buf,"btconfig rawcmd 0x3F 0x0004 0x04 %s 0x%s 0x%s 0x00 0x%s 0x01 %s 0x00 0xA7 0x02 0x00",
						channel,flag[1],flag[2],flag[3],bt_mac);
					}
					else if(atoi(flag[5]) == 3)
					{
						sprintf(buf,"btconfig rawcmd 0x3F 0x0004 0x04 %s 0x%s 0x%s 0x00 0x%s 0x01 %s 0x00 0xFD 0x03 0x00",
						channel,flag[1],flag[2],flag[3],bt_mac);
					}
					int len=strlen(buf);
					LOGATCI(LOG_DEBUG,"br_send buf:%s len= %d \n",buf,len);
					system(buf);
					sleep(1);
                    return AT_OK;
                }else if (strstr(command, "br_recv")){
                    err = at_tok_nextstr(&cmdline, &flag[0]);   //2
                    if (err < 0) {
                        return AT_ERROR;
                    }
                    if (!validateInput(flag[0])){
                        LOGATCI(LOG_DEBUG, "[QL_AT_QFCT_Handle] not support input %s", flag[0]);
                        return AT_ERROR;
                    } 
                    sprintf(channel,"0x%s 0x%s 0x%s 0x%s 0x%s",flag[0],flag[0],flag[0],flag[0],flag[0]);
					LOGATCI(LOG_DEBUG,"br_send channel:%s\n",flag[0]);
                    err = at_tok_nextstr(&cmdline, &flag[1]);   //3
                    if (err < 0) {
                        return AT_ERROR;
                    }
                    err = at_tok_nextstr(&cmdline, &flag[2]);   //4
                    if (err < 0) {
                        return AT_ERROR;
                    }
                    err = at_tok_nextstr(&cmdline, &flag[3]);   //5
                    if (err < 0) {
                        return AT_ERROR;
                    }
                    err = at_tok_nextstr(&cmdline, &flag[4]);   //6
                    if (err < 0) {
                        return AT_ERROR;
                    }
                    err = at_tok_nextstr(&cmdline, &flag[5]);   //7
                    if (err < 0) {
                        return AT_ERROR;
                    }
                    if(strlen(flag[4])>8)
                        sprintf(bt_mac,"%s",flag[4]);
                    else
                        sprintf(bt_mac,"0x9C 0x35 0xBD 0x9C 0x35 0xBD");
                    if (!validateInput(flag[1]) || !validateInput(flag[2])  || !validateInput(flag[2]) || !validateInput(flag[5])) {
                        LOGATCI(LOG_DEBUG, "[QL_AT_QFCT_Handle] not support input %s", command);
                        return AT_ERROR;
                    }
                    if(atoi(flag[5]) == 0)
					{
						sprintf(buf,"btconfig rawcmd 0x3F 0x0004 0x06 %s 0x%s 0x%s 0x00 0x%s 0x01 %s 0x00 0x1B 0x00 0x00",
						channel,flag[1],flag[2],flag[3],bt_mac);//channel 协议 功率 蓝牙mac 
					}
					else if(atoi(flag[5]) == 1)
					{
						sprintf(buf,"btconfig rawcmd 0x3F 0x0004 0x06 %s 0x%s 0x%s 0x00 0x%s 0x01 %s 0x00 0x53 0x01 0x00",
						channel,flag[1],flag[2],flag[3],bt_mac);
					}
					else if(atoi(flag[5]) == 2)
					{
						sprintf(buf,"btconfig rawcmd 0x3F 0x0004 0x06 %s 0x%s 0x%s 0x00 0x%s 0x01 %s 0x00 0xA7 0x02 0x00",
						channel,flag[1],flag[2],flag[3],bt_mac);
					}
					else if(atoi(flag[5]) == 3)
					{
						sprintf(buf,"btconfig rawcmd 0x3F 0x0004 0x06 %s 0x%s 0x%s 0x00 0x%s 0x01 %s 0x00 0xFD 0x03 0x00",
						channel,flag[1],flag[2],flag[3],bt_mac);
					}
					int len=strlen(buf);
					LOGATCI(LOG_DEBUG,"br_recv buf:%s len= %d \n",buf,len);
					system(buf);
					sleep(1);
                    return AT_OK;
                }else if (strstr(command, "ble_send")){
                    err = at_tok_nextstr(&cmdline, &flag[0]);   //3
                    if (err < 0) {
                        return AT_ERROR;
                    }
                    err = at_tok_nextstr(&cmdline, &flag[1]);   //4
                    if (err < 0) {
                        return AT_ERROR;
                    }
                    err = at_tok_nextstr(&cmdline, &flag[2]);   //5
                    if (err < 0) {
                        return AT_ERROR;
                    }
                    if (!validateInput(flag[0]) || !validateInput(flag[1]) || !validateInput(flag[2])) {
                        // 长度超过MAX_LEN 或 包含非16进制字符
                        LOGATCI(LOG_DEBUG,"input error");
                        return AT_ERROR;
                    }
                    sprintf(buf,"btconfig rawcmd 0x08 0x001E 0x%s 0x%s 0x%s",flag[0],flag[1],flag[2]);
                    int len=strlen(buf);
                    LOGATCI(LOG_DEBUG,"ble_send buf:%s len= %d \n",buf,len);
                    system(buf);
                    sleep(1);
                    return AT_OK;
                }else if (strstr(command, "ble_recv")){
                    err = at_tok_nextstr(&cmdline, &flag[0]);   //3
                    if (err < 0) {
                        LOGATCI(LOG_DEBUG,"failed to get flag [0] error");
                        return AT_ERROR;
                    }
                    if (!validateInput(flag[0])) {
                    // 长度超过MAX_LEN 或 包含非16进制字符
                        LOGATCI(LOG_DEBUG,"input error");
                        return AT_ERROR;
                    }
                    sprintf(buf,"btconfig rawcmd 0x08 0x001D 0x%s",flag[0]);
                    int len=strlen(buf);
                    LOGATCI(LOG_DEBUG,"ble_recv buf:%s len= %d \n",buf,len);
                    system(buf);
                    sleep(1);
                    return AT_OK;
                }else if (strstr(command, "br_read")){
                    sprintf(buf,"btconfig rawcmd 0x3F 0x0004 0x02 0x00 > /var/log/br_read.txt");
					int len=strlen(buf);
					LOGATCI(LOG_DEBUG,"br_read buf:%s len=%d\n",buf,len);
					system(buf);
					sleep(1);
					char *resp_buf=NULL;
					if(NULL == resp_buf)
					{
						resp_buf = (char *)malloc(MAX_READ_LEN);
						if(resp_buf == NULL)
						{
						 LOGATCI(LOG_DEBUG,"resp_buf No Memory   \n");
						 return AT_ERROR; // error
						}
						memset(resp_buf, 0, MAX_READ_LEN);
					 }
					FILE *fp;
					fp = fopen("/var/log/br_read.txt", "r");
					if (fp == NULL) {
						return AT_ERROR; // error
					}
					char line[MAX_READ_LEN];
					char read_br[MAX_READ_LEN] = {0};  // 存放提取的值
					int found = 0;  // 标识是否找到期望的行

					// 逐行读取文件内容
					while (fgets(line, sizeof(line), fp) != NULL) {
						// 查找字符串 "RECV <- dump :" 
						if (strstr(line, "RECV <- dump : ") != NULL) {
							// 找到后，将其中的数据提取并保存到 read_br
							char *start = strstr(line, "RECV <- dump : ") + strlen("RECV <- dump : ");
							strncpy(read_br, start, MAX_READ_LEN - 1);
							read_br[MAX_READ_LEN - 1] = '\0';  // 确保字符串以 NULL 结尾
							found = 1;  // 标记已找到
							break;  // 找到后跳出循环
						}
					}
					fclose(fp);  // 关闭文件
					char Opcode[32] ={0};
					memset(Opcode, 0, sizeof(Opcode));
					memcpy(Opcode, read_br, 15 * sizeof(read_br[0]));
					LOGATCI(LOG_DEBUG,"Opcode buf:%s \n",Opcode);
					
					int pkt_num=0;	//第一列
					int top_pki=0;	//第五列
					int pkt_ace=0;	//第二列
					int hec_err=0;	//第三列
					int crc_err=0;	//第四列
					int rssi=0;	//第八列
					pkt_num=extract_data(read_br,16);
					LOGATCI(LOG_DEBUG,"pkt_num result %d\n",pkt_num);
					
					top_pki=extract_data(read_br,64);
					LOGATCI(LOG_DEBUG,"top_pki result %d\n",top_pki);
					
					pkt_ace=extract_data(read_br,28);
					LOGATCI(LOG_DEBUG,"pkt_ace result %d\n",pkt_ace);
					
					hec_err=extract_data(read_br,40);
					LOGATCI(LOG_DEBUG,"hec_err result %d\n",hec_err);
					
					crc_err=extract_data(read_br,52);
					LOGATCI(LOG_DEBUG,"crc_err result %d\n",crc_err);
					
					rssi=extract_data(read_br,100)/5;
					LOGATCI(LOG_DEBUG,"rssi result %d\n",rssi);
					double result;
					LOGATCI(LOG_DEBUG,"read_br :%s len = %d\n",read_br,strlen(read_br));
					//Calculate the bit error rate
                    err = at_tok_nextstr(&cmdline, &flag[0]);   //3
                    if (err < 0) {
                        return AT_ERROR;
                    }
					if(atoi(flag[0]) > 0)
					{
						if(pkt_num != 0)
						{
							if(pkt_num == pkt_ace)
							{
								result=100;
							}
							else
							{
								result = (double)top_pki/(double)((atoi(flag[0]))*pkt_num)*100;
							}
						}
						else
						{
							result=100;
						}
						sprintf(resp_buf, "Opcode %s\n Ber = %.6f%%\n total_packets : %d\ntotal_packets_access_error : %d\nhec_error : %d \ncrc_errors : %d\ntotal_packes_bit_errors : %d\n RSSI : %d\n",Opcode,result,pkt_num,pkt_ace,hec_err,crc_err,top_pki,rssi);
					}
					else
					{
						sprintf(resp_buf, "Opcode %s\ntotal_packets : %d\ntotal_packets_access_error : %d\nhec_error : %d \ncrc_errors : %d\ntotal_packes_bit_errors : %d\n RSSI : %d\n",Opcode,pkt_num,pkt_ace,hec_err,crc_err,top_pki,rssi);
					}
                    return AT_OK;
                }else if (strstr(command, "ble_read")){
                    sprintf(buf,"btconfig rawcmd 0x08 0x001F > /var/log/ble_read.txt");
					int len=strlen(buf);
					LOGATCI(LOG_DEBUG,"ble_read buf:%s len= %d\n",buf,len);
					system(buf);
					sleep(1);
					char *resp_buf=NULL;
					if(NULL == resp_buf)
					{
						resp_buf = (char *)malloc(MAX_READ_LEN);
						if(resp_buf == NULL)
						{
						 LOGATCI(LOG_DEBUG,"resp_buf No Memory   \n");
                         return AT_ERROR; // error
						}
						memset(resp_buf, 0, MAX_READ_LEN);
					 }
					FILE *fp;
					fp = fopen("/var/log/ble_read.txt", "r");
					if (fp == NULL) {
                        return AT_ERROR; // error
					}
					char line[MAX_READ_LEN];
					char read_ble[MAX_READ_LEN] = {0};  // 存放提取的值
					int found = 0;  // 标识是否找到期望的行
					char recv_pack[8]= {0};;

					// 逐行读取文件内容
					while (fgets(line, sizeof(line), fp) != NULL) {
						// 查找字符串 "RECV <- dump :" 
						if (strstr(line, "RECV <- dump : ") != NULL) {
							// 找到后，将其中的数据提取并保存到 read_ble
							char *start = strstr(line, "RECV <- dump : ") + strlen("RECV <- dump : ");
							strncpy(read_ble, start, MAX_READ_LEN - 1);
							read_ble[MAX_READ_LEN - 1] = '\0';  // 确保字符串以 NULL 结尾
							found = 1;  // 标记已找到
							break;  // 找到后跳出循环
						}
						//LOGATCI(LOG_DEBUG,"read_ble line %s",line);
					}
					fclose(fp);  // 关闭文件
					// 取出最后两个字节
					LOGATCI(LOG_DEBUG,"read_ble buf %s\n",read_ble);
					double result;
                    err = at_tok_nextstr(&cmdline, &flag[0]);   //3
                    if (err < 0) {
                        return AT_ERROR;
                    }
					if(atoi(flag[0]) > 0)
					{
						sprintf(recv_pack,"%c%c%c%c",read_ble[22],read_ble[23],read_ble[19],read_ble[20]);
						
						long int decimalValue;	//recv_pack num
						char *endptr;
						decimalValue = strtol(recv_pack, &endptr, 16);
						if (*endptr != '\0' || errno == ERANGE) {
							LOGATCI(LOG_DEBUG,"fial\n");
						} 
						LOGATCI(LOG_DEBUG,"recv_pack buf %s %d\n",recv_pack,decimalValue);
						if(decimalValue > atoi(flag[0]))
						{
							sprintf(resp_buf, "read_ble %s\n Error,data read >  data input\n",read_ble);
						}
						else
						{
							//double result;
							result = (double)(atoi(flag[0])-decimalValue)/(atoi(flag[0]))*100;
							sprintf(resp_buf, "read_ble %s\n Ber = %.6f%%",read_ble,result);
						}
					}
					else
					{
						sprintf(resp_buf, "read_ble %s\n",read_ble);
					}
                    return AT_OK;
                }else{
                    LOGATCI(LOG_DEBUG, "[QL_AT_QFCT_Handle] not support input %s", command);
                    return AT_ERROR;
                }
            }else if (strstr(command, "done")){
                system("systemctl stop gdm.service");
                system("rm -rf /etc/gdm3/daemon.conf");
                system("rm -rf /etc/gdm3/custom.conf");
                system("pkill -9 -u pi && userdel -r pi");
                sleep(2);
                system("userdel -r pi");
                system("rm -rf /home/pi");
                system("mkdir -p /opt/persist_backup");
                system("touch /var/persist/fct_done_flag");
                system("cp -a /var/persist/* /opt/persist_backup/");
                system("sync");
                return AT_OK;
            }else{
                LOGATCI(LOG_DEBUG, "[QL_AT_QFCT_Handle] not support input %s", command);
                return AT_ERROR;
            }
            return AT_ERROR;
            break;
        case AT_TEST_OP:
            return AT_OK;
            break;
        default:
            break;
    }

    return AT_ERROR;
}

ATRESPONSE_t QL_AT_QNVW_Handle(char* cmdline, ATOP_t opType, char* response)
{
    int err;
    char *command;
    int op,index;
    char macinfo[64];
    int fd;
    int rw_size;

    LOGATCI(LOG_DEBUG, "[QL_AT_QNVW_Handle] cmdline %s", cmdline);
    switch(opType) {
        case AT_SET_OP:
            err = at_tok_nextint(&cmdline, &op);
            if (err < 0) {
                LOGATCI(LOG_DEBUG, "[QL_AT_QNVW_Handle] not support op input %s", command);
                return AT_ERROR;
            }
            if (op == 4678) {
                err = at_tok_nextint(&cmdline, &index);
                if (err < 0) {
                    LOGATCI(LOG_DEBUG, "[QL_AT_QNVW_Handle] not support index input %s", command);
                    return AT_ERROR;
                }
                if(index == 0){
                    err = at_tok_nextstr(&cmdline, &command);
                    if(strlen(command)==12){
                        memset(qwifi_data->wifi_mac,0,sizeof(qwifi_data->wifi_mac));
                        strncpy(qwifi_data->wifi_mac,command,strlen(command));
                        modem_sync();
                        memset(macinfo,0,64);
                        sprintf(macinfo,"Intf0MacAddress=%s\nEND\n\0",command);
                        fd = open(MAC_BIN_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (fd < 0) {
                            LOGATCI(LOG_DEBUG,"open for write");
                            return AT_ERROR;
                        }
                        LOGATCI(LOG_DEBUG,"open success");
            
                        rw_size = write(fd, macinfo, strlen(macinfo));
                        if (rw_size < 0) {
                            LOGATCI(LOG_DEBUG,"write");
                            close(fd);
                            return AT_ERROR;
                        }
                        LOGATCI(LOG_DEBUG,"write success");
                        close(fd);
                        system("sync");
                        return AT_OK;
                    }
                }
            }
            if (op == 447) {
                err = at_tok_nextint(&cmdline, &index);
                if (err < 0) {
                    LOGATCI(LOG_DEBUG, "[QL_AT_QNVW_Handle] not support index input %s", command);
                    return AT_ERROR;
                }
                if(index == 0){
                    err = at_tok_nextstr(&cmdline, &command);
                    if(strlen(command)==12){
                        memset(qwifi_data->bt_mac,0,sizeof(qwifi_data->bt_mac));
                        strncpy(qwifi_data->bt_mac,command,strlen(command));
                        modem_sync();
                        system("sync");
                        //BT will use same Mac with WiFi 
                        return AT_OK;
                    }
                }
            }
            return AT_ERROR;
            break;
        case AT_TEST_OP:
            return AT_OK;
            break;
        default:
            break;
    }

    return AT_ERROR;
}


ATRESPONSE_t QL_AT_QNVR_Handle(char* cmdline, ATOP_t opType, char* response)
{
    int err;
    char *command;
    int op,index;
    char macinfo[64];
    int fd;
    int rw_size;

    LOGATCI(LOG_DEBUG, "[QL_AT_QNVR_Handle] cmdline %s", cmdline);
    switch(opType) {
        case AT_SET_OP:
            err = at_tok_nextint(&cmdline, &op);
            if (err < 0) {
                LOGATCI(LOG_DEBUG, "[QL_AT_QNVR_Handle] not support op input %s", command);
                return AT_ERROR;
            }
            if (op == 4678) {
                err = at_tok_nextint(&cmdline, &index);
                if (err < 0) {
                    LOGATCI(LOG_DEBUG, "[QL_AT_QNVR_Handle] not support index input %s", command);
                    return AT_ERROR;
                }
                if(index == 0){
                    sprintf(response, "+QNVR:\"%s\"",qwifi_data->wifi_mac);
                    return AT_OK;
                }
            }
            if (op == 447) {
                err = at_tok_nextint(&cmdline, &index);
                if (err < 0) {
                    LOGATCI(LOG_DEBUG, "[QL_AT_QNVR_Handle] not support index input %s", command);
                    return AT_ERROR;
                }
                if(index == 0){
                    sprintf(response, "+QNVR:\"%s\"",qwifi_data->bt_mac);
                    return AT_OK;
                }
            }
            return AT_ERROR;
            break;
        case AT_TEST_OP:
            return AT_OK;
            break;
        default:
            break;
    }

    return AT_ERROR;
}

void *run_cmd_thread_func(void *arg);

ATRESPONSE_t QL_AT_HEADSET_START_Handle(char* cmdline, ATOP_t opType, char* response)
{
    pthread_t pid;
    const char* cmd = (const char*)scripts_audio_loop_sh;

    switch (opType) {
        case AT_ACTION_OP:
            pthread_create(&pid, NULL, run_cmd_thread_func, (void*)cmd);
            return AT_OK;
        case AT_TEST_OP:
            return AT_OK;
        default:
            break;
    }
    return AT_ERROR;
}

void *run_cmd_thread_func(void *arg)
{
    pthread_detach(pthread_self());
    const char *cmd = (const char *)arg;
    system(cmd);
    return NULL;
}

static int is_gnome_session_running(void)
{
    FILE *fp;
    char line[256];
    int found = 0;

    fp = popen("ps aux | grep -Ei '[g]nome-shell|[g]nome-session|[g]dm|[m]utter'", "r");
    if (fp == NULL) {
        return 0;
    }

    if (fgets(line, sizeof(line), fp) != NULL) {
        found = 1;
    }

    pclose(fp);
    return found;
}

ATRESPONSE_t QL_AT_CAMERA0_START_Handle(char* cmdline, ATOP_t opType, char* response) {
    pthread_t pid;
    const char* cmd = NULL;

    if(is_gnome_session_running()) {
        // 构建完整的 shell 命令（用分号分隔多个命令，确保在同一进程中执行）
        cmd = 
            "FILE=/tmp/capture.yuv;"
            "export GST_PLUGIN_PATH=/usr/lib/gstreamer-1.0:$GST_PLUGIN_PATH;"
            "export DISPLAY=:0;"
            "FILE_PATH=$(find /run/user/1001 -type f -name '.mutter-*' | head -n 1);"
            "if [ -z \"$FILE_PATH\" ]; then "
                "echo \"未找到 .mutter 文件\"; "
            "else "
                "echo \"找到文件：$FILE_PATH\"; "
                "export XAUTHORITY=\"$FILE_PATH\"; "
                "echo \"XAUTHORITY 设置为：$XAUTHORITY\"; "
            "fi;"
            // 执行 gst-launch-1.0 命令，使用前面设置的环境变量
            "sudo -E gst-launch-1.0 -e qtiqmmfsrc name=camsrc camera=0 ! 'video/x-raw,format=NV12,width=1280,height=720,framerate=30/1' ! autovideosink;";
    }
    else {
        // 构建完整的 shell 命令（用分号分隔多个命令，确保在同一进程中执行）
        cmd = 
            "mount -o rw,remount /;"
            "export XDG_RUNTIME_DIR=/dev/socket/weston;"
            "export WAYLAND_DISPLAY=wayland-1;"
            // 执行 gst-launch-1.0 命令，使用前面设置的环境变量
            "gst-launch-1.0 -e qtiqmmfsrc name=camsrc camera=0 !  waylandsink;";
    }

    // 创建线程执行完整命令（注意：cmd 是常量字符串，生命周期需长于线程）
    pthread_create(&pid, NULL, run_cmd_thread_func, (void*)cmd);

    return AT_OK;
}

ATRESPONSE_t QL_AT_CAMERA1_START_Handle(char* cmdline, ATOP_t opType, char* response) {
    pthread_t pid;
    const char* cmd = NULL;
    
    if(is_gnome_session_running()) {
        // 构建完整的 shell 命令（用分号分隔多个命令，确保在同一进程中执行）
        cmd = 
            "FILE=/tmp/capture.yuv;"
            "export GST_PLUGIN_PATH=/usr/lib/gstreamer-1.0:$GST_PLUGIN_PATH;"
            "export DISPLAY=:0;"
            "FILE_PATH=$(find /run/user/1001 -type f -name '.mutter-*' | head -n 1);"
            "if [ -z \"$FILE_PATH\" ]; then "
                "echo \"未找到 .mutter 文件\"; "
            "else "
                "echo \"找到文件：$FILE_PATH\"; "
                "export XAUTHORITY=\"$FILE_PATH\"; "
                "echo \"XAUTHORITY 设置为：$XAUTHORITY\"; "
            "fi;"
            // 执行 gst-launch-1.0 命令，使用前面设置的环境变量
            "sudo -E gst-launch-1.0 -e qtiqmmfsrc name=camsrc camera=1 ! 'video/x-raw,format=NV12,width=1280,height=720,framerate=30/1' ! autovideosink;";
    }
    else {
        // 构建完整的 shell 命令（用分号分隔多个命令，确保在同一进程中执行）
        cmd = 
            "mount -o rw,remount /;"
            "export XDG_RUNTIME_DIR=/dev/socket/weston;"
            "export WAYLAND_DISPLAY=wayland-1;"
            // 执行 gst-launch-1.0 命令，使用前面设置的环境变量
            "gst-launch-1.0 -e qtiqmmfsrc name=camsrc camera=1 !  waylandsink;";
    }

    // 创建线程执行完整命令（注意：cmd 是常量字符串，生命周期需长于线程）
    pthread_create(&pid, NULL, run_cmd_thread_func, (void*)cmd);

    return AT_OK;
}

ATRESPONSE_t QL_AT_CAMERA2_START_Handle(char* cmdline, ATOP_t opType, char* response) {
    pthread_t pid;
    const char* cmd = NULL;
    
    if(is_gnome_session_running()) {
        // 构建完整的 shell 命令（用分号分隔多个命令，确保在同一进程中执行）
        cmd = 
            "FILE=/tmp/capture.yuv;"
            "export GST_PLUGIN_PATH=/usr/lib/gstreamer-1.0:$GST_PLUGIN_PATH;"
            "export DISPLAY=:0;"
            "FILE_PATH=$(find /run/user/1001 -type f -name '.mutter-*' | head -n 1);"
            "if [ -z \"$FILE_PATH\" ]; then "
                "echo \"未找到 .mutter 文件\"; "
            "else "
                "echo \"找到文件：$FILE_PATH\"; "
                "export XAUTHORITY=\"$FILE_PATH\"; "
                "echo \"XAUTHORITY 设置为：$XAUTHORITY\"; "
            "fi;"

            "CAM_DEV=$(ls /dev/v4l/by-id/usb-*-video-index0 2>/dev/null | head -n 1);"
            "if [ -z \"$CAM_DEV\" ]; then "
                "echo \"No USB camera found\"; "
                "exit 1; "
            "fi;"
            "echo \"Using camera: $CAM_DEV\"; "

            // 执行 gst-launch-1.0 命令，使用前面设置的环境变量
            "sudo -E gst-launch-1.0 -e v4l2src device=\"$CAM_DEV\" ! 'image/jpeg,width=1280,height=720,framerate=10/1' ! jpegdec ! videoconvert ! autovideosink;";
    }
    else {
        // 构建完整的 shell 命令（用分号分隔多个命令，确保在同一进程中执行）
        cmd = 
            "mount -o rw,remount /;"
            "export XDG_RUNTIME_DIR=/dev/socket/weston;"
            "export WAYLAND_DISPLAY=wayland-1;"

            "CAM_DEV=$(ls /dev/v4l/by-id/usb-*-video-index0 2>/dev/null | head -n 1);"
            "if [ -z \"$CAM_DEV\" ]; then "
                "echo \"No USB camera found\"; "
                "exit 1; "
            "fi;"
            "echo \"Using camera: $CAM_DEV\"; "

            // 执行 gst-launch-1.0 命令，使用前面设置的环境变量
            "gst-launch-1.0 -e v4l2src device=\"$CAM_DEV\" ! 'image/jpeg,width=1280,height=720,framerate=10/1' ! jpegdec ! waylandsink;";
    }

    // 创建线程执行完整命令（注意：cmd 是常量字符串，生命周期需长于线程）
    pthread_create(&pid, NULL, run_cmd_thread_func, (void*)cmd);

    return AT_OK;
}

ATRESPONSE_t QL_AT_CAMERA_STOP_Handle(char* cmdline, ATOP_t opType, char* response)
{
    system("killall -INT gst-launch-1.0");
    return AT_OK;
}

ATRESPONSE_t QL_AT_KEYGET_Handle(char* cmdline, ATOP_t opType, char* response)
{
    extern int key_power_down, key1_down, key2_down;
    snprintf(response, MAX_RESPONSE_LEN, "POWER_LEY: %d\nKEY1: %d\nKEY2: %d", key_power_down, key1_down, key2_down);
    return AT_OK;
}

ATRESPONSE_t QL_AT_CARDGET_Handle(char* cmdline, ATOP_t opType, char* response)
{
    int len = 0;
    char line[512];
    FILE *fp = popen("df -h | grep -E '/media|/mnt'", "r");
    if (fp) {
        while (1) {
            if (fgets(line, sizeof(line), fp) == NULL) break;
            len += sprintf(response + len, "%s", line);
        }
        pclose(fp);
    }
    return AT_OK;
}

ATRESPONSE_t QL_AT_LEDREDON_Handle (char* cmdline, ATOP_t opType, char* response) { system("qpi-config led brightness red 100"); return AT_OK; }
ATRESPONSE_t QL_AT_LEDREDOFF_Handle(char* cmdline, ATOP_t opType, char* response) { system("qpi-config led brightness red 0"); return AT_OK; }
ATRESPONSE_t QL_AT_LEDGREENON_Handle (char* cmdline, ATOP_t opType, char* response) { system("qpi-config led brightness green 100"); return AT_OK; }
ATRESPONSE_t QL_AT_LEDGREENOFF_Handle(char* cmdline, ATOP_t opType, char* response) { system("qpi-config led brightness green 0"); return AT_OK; }
ATRESPONSE_t QL_AT_LEDBLUEON_Handle (char* cmdline, ATOP_t opType, char* response) { system("qpi-config led brightness blue 100"); return AT_OK; }
ATRESPONSE_t QL_AT_LEDBLUEOFF_Handle(char* cmdline, ATOP_t opType, char* response) { system("qpi-config led brightness blue 0"); return AT_OK; }
ATRESPONSE_t QL_AT_SSDTEST_Handle(char* cmdline, ATOP_t opType, char* response)
{
    int len = 0;
    char line[512];
    FILE *fp = popen(
        "set -e;"
        "if [ ! -b /dev/nvme0n1p1 ]; then "
        // 关键修改：明确指定分区大小（使用整个磁盘）和类型为 Linux（83）
        "fdisk /dev/nvme0n1 >/dev/null 2>&1 <<EOF\n"
        "n\n"     // 新建分区
        "p\n"     // 主分区
        "1\n"     // 分区号 1
        "2048\n"  // 起始扇区（跳过前2048扇区，避免覆盖分区表）
        "\n"      // 结束扇区：默认（使用剩余全部空间）
        "t\n"     // 更改分区类型
        "83\n"    // 类型 83 = Linux（ext4 兼容）
        "w\n"     // 保存分区表
        "EOF\n"
        // 强制刷新分区表（关键！确保内核识别新分区）
        "partprobe /dev/nvme0n1 || blockdev --rereadpt /dev/nvme0n1;\n"
        "sleep 2;\n"  // 等待内核识别分区
        "mkfs.ext4 -F -q /dev/nvme0n1p1;\n"
        "fi;"
        // 后续挂载和测试逻辑不变
        "mkdir -p /mnt/ssd;\n"
        "umount /dev/nvme0n1p1 >/dev/null 2>&1 || true;\n"
        "mount /dev/nvme0n1p1 /mnt/ssd/ 2>&1;\n"
        "echo 'write test:';\n"
        "dd if=/dev/zero of=/mnt/ssd/testfile bs=1G count=1 oflag=direct 2>&1;\n"
        "echo 'read test:';\n"
        "dd if=/mnt/ssd/testfile of=/dev/null bs=1G iflag=direct 2>&1;\n"
        "umount /dev/nvme0n1p1;",
        "r"
    );
    if (fp) {
        while (1) {
            //if (feof(fp)) break;
            if (fgets(line, sizeof(line), fp) == NULL) {
                //sleep(1); continue;
                break;
            }
            len += sprintf(response + len, "%s", line);
        }
        fclose(fp);
    }
    return AT_OK;
}
ATRESPONSE_t QL_AT_40PINLEDON_Handle (char* cmdline, ATOP_t opType, char* response) { system(scripts_gpio_led_on_sh); return AT_OK; }
ATRESPONSE_t QL_AT_40PINLEDOFF_Handle(char* cmdline, ATOP_t opType, char* response) { system(scripts_gpio_led_off_sh); return AT_OK; }
ATRESPONSE_t QL_AT_FANON_Handle      (char* cmdline, ATOP_t opType, char* response) {
    system("systemctl stop fan_ctrl.service");
    system("echo 1 > /sys/devices/platform/soc@0/soc@0:pwm-fan/hwmon/hwmon32/pwm1_enable");
    system("echo 255 > /sys/devices/platform/soc@0/soc@0:pwm-fan/hwmon/hwmon32/pwm1");
    return AT_OK;
}
ATRESPONSE_t QL_AT_FANOFF_Handle     (char* cmdline, ATOP_t opType, char* response) {
    system("systemctl stop fan_ctrl.service");
    system("echo 0 > /sys/devices/platform/soc@0/soc@0:pwm-fan/hwmon/hwmon32/pwm1");
    system("echo 0 > /sys/devices/platform/soc@0/soc@0:pwm-fan/hwmon/hwmon32/pwm1_enable");
    return AT_OK;
}
#define QCOM_SCM_DLOAD_MODE_PATH "/sys/module/qcom_scm/parameters/download_mode"
/* Dump 持久化配置：qpi-config.ini 的 dumpenable 字段 (1=开, 0=关)。
 * 不用 /var/persist/dump_flag 标记文件——persist 分区有整目录双向备份
 * (pi-cleanup.sh sync_persist_backup), 删掉的 flag 会被备份复活, 导致
 * performance 固件上 dump 无法真正关闭。qpi-config.ini 在 rootfs (/etc),
 * 不受 persist 备份影响。 */
#define QPI_CONFIG_INI_PATH "/etc/qpi-config/qpi-config.ini"

/* 写 sysfs download_mode (full/off)，即时生效 */
static int ql_dump_write_sysfs(const char *val)
{
	FILE *fp = fopen(QCOM_SCM_DLOAD_MODE_PATH, "w");
	if (!fp) {
		LOGATCI(LOG_DEBUG, "[QL_AT_QCFG_Handle] open %s failed", QCOM_SCM_DLOAD_MODE_PATH);
		return -1;
	}
	if (fputs(val, fp) == EOF) {
		LOGATCI(LOG_DEBUG, "[QL_AT_QCFG_Handle] write %s failed", val);
		fclose(fp);
		return -1;
	}
	fclose(fp);
	return 0;
}

/* 持久化 dump 开关到 qpi-config.ini 的 dumpenable 字段 (1/0)。
 * 用 sed 精确替换该行, 不影响 ini 里其他配置项。
 * 失败只记日志, 不影响 AT 返回 (本次开机周期功能正常, 仅重启后可能还原)。 */
static void ql_dump_set_flag(int enable)
{
	char cmd[256];
	int ret;

	snprintf(cmd, sizeof(cmd),
		 "sed -i 's/^dumpenable=.*/dumpenable=%d/' %s",
		 enable ? 1 : 0, QPI_CONFIG_INI_PATH);
	ret = system(cmd);
	if (ret != 0) {
		LOGATCI(LOG_DEBUG, "[QL_AT_QCFG_Handle] %s failed (ret=%d), dump may not persist after reboot",
			cmd, ret);
		return;
	}
	LOGATCI(LOG_DEBUG, "[QL_AT_QCFG_Handle] dumpenable=%d persisted to %s",
		enable ? 1 : 0, QPI_CONFIG_INI_PATH);
}

ATRESPONSE_t QL_AT_QCFG_Handle (char* cmdline, ATOP_t opType, char* response)
{
	/*
	 * Qualcomm dump 模式 (内核 crash 后进 900e Sahara) 由 qcom_scm 驱动的
	 * module_param download_mode 控制，底层经 set_download_mode() 写 TCSR
	 * dload-mode 寄存器 bit[5:4]。
	 *
	 * 双层机制：
	 *  - 即时层：直接写 sysfs /sys/module/qcom_scm/parameters/download_mode，
	 *    值 full=>开、off=>关，立即生效（当前开机周期内 crash 才抓 dump）。
	 *  - 持久层：把状态写入 /etc/qpi-config/qpi-config.ini 的 dumpenable 字段
	 *    (1=开/0=关)，开机时 pi-cleanup.sh 读取该字段恢复 sysfs 状态。
	 *    不用 /var/persist/dump_flag 标记文件——persist 分区有整目录双向
	 *    备份，删除的标记会被备份复活，导致 dump 关不掉。
	 *
	 * sysfs 写优先：只要即时生效成功就返回 AT_OK，ini 持久化失败只记日志、
	 * 不影响 AT 返回（本次开机周期功能正常，仅重启后可能还原）。
	 */
	/* AT+QCFG="dumpenable",1  -> 打开 dump */
	if (strcasecmp(cmdline, "dumpenable,1") == 0 || strcasecmp(cmdline, "\"dumpenable\",1") == 0) {
		if (ql_dump_write_sysfs("full") < 0) {
			return AT_ERROR;
		}
		ql_dump_set_flag(1);
		LOGATCI(LOG_DEBUG, "[QL_AT_QCFG_Handle] dump enabled");
		return AT_OK;
	}
	/* AT+QCFG="dumpenable",0  -> 关闭 dump */
	if (strcasecmp(cmdline, "dumpenable,0") == 0 || strcasecmp(cmdline, "\"dumpenable\",0") == 0) {
		if (ql_dump_write_sysfs("off") < 0) {
			return AT_ERROR;
		}
		ql_dump_set_flag(0);
		LOGATCI(LOG_DEBUG, "[QL_AT_QCFG_Handle] dump disabled");
		return AT_OK;
	}

	/* AT+QCFG="dumpenable"  -> 查询当前 dump 状态 (直读 sysfs 真实值) */
	if (strcasecmp(cmdline, "dumpenable") == 0 || strcasecmp(cmdline, "\"dumpenable\"") == 0) {
		FILE *fp = fopen(QCOM_SCM_DLOAD_MODE_PATH, "r");
		char mode[32] = {0};
		if (!fp) {
			LOGATCI(LOG_DEBUG, "[QL_AT_QCFG_Handle] read %s failed", QCOM_SCM_DLOAD_MODE_PATH);
			return AT_ERROR;
		}
		if (fgets(mode, sizeof(mode), fp) == NULL) {
			LOGATCI(LOG_DEBUG, "[QL_AT_QCFG_Handle] fgets failed");
			fclose(fp);
			return AT_ERROR;
		}
		fclose(fp);
		mode[strcspn(mode, "\r\n")] = '\0';
		/* off/N/0 = 0(关闭), full/on/1/Y/mini/full,mini = 1(开启) */
		int enabled = 0;
		if (strcasecmp(mode, "off") != 0 && strcasecmp(mode, "0") != 0 &&
			strcasecmp(mode, "n") != 0 && mode[0] != '\0') {
			enabled = 1;
		}
		snprintf(response, MAX_RESPONSE_LEN, "DumpEnable: %d", enabled);
		return AT_OK;
	}
	return AT_ERROR;
}

ATRESPONSE_t QL_AT_QUSBMODE_Handle(char* cmdline, ATOP_t opType, char* response){

    int adc_channel,value;
    int err = 0;
    FILE *fp;
    char speed[64];

    LOGATCI(LOG_DEBUG, "[QL_AT_QUSBMODE_Handle] cmdline %s", cmdline);

    switch(opType) {
        case AT_ACTION_OP:

            fp = fopen("/sys/class/udc/a600000.usb/current_speed", "r");
            if (!fp) {
                LOGATCI(LOG_DEBUG, "[QL_AT_QUSBMODE_Handle] failed to open QUSBMODE file");
                return AT_ERROR;
            }
        
            if (fgets(speed, sizeof(speed), fp) != NULL) {
                if (strstr(speed, "super-speed"))
                    sprintf(response, "+QUSBMODE: 3");
                else if (strstr(speed, "high-speed"))
                    sprintf(response, "+QUSBMODE: 2");
                else
                    sprintf(response, "+QUSBMODE: %s",speed);
            }
            return AT_OK;
            break;
        default:
            break;
    }

    return AT_ERROR;

}

ATRESPONSE_t QL_AT_QREDDAVN_Handle(char* cmdline, ATOP_t opType, char* response) {

    switch(opType) {
        case AT_ACTION_OP:
        case AT_READ_OP:
            sprintf(response, "+QREDDAVN: PIH1LINUXREDDAVN001");
            return AT_OK;

        default:
            break;
    }

    return AT_ERROR;
}

#define MAC_FILE_PATH "/var/persist/mac_addr"
#define MAX_MAC_LEN 18  
#define MAX_CLEAN_MAC_LEN 13 

static void clean_mac_separator(char *src_mac, char *dst_mac) {
    if (src_mac == NULL || dst_mac == NULL) {
        return;
    }

    int dst_idx = 0;
    for (int i = 0; src_mac[i] != '\0' && dst_idx < MAX_CLEAN_MAC_LEN - 1; i++) {
        if (isxdigit((unsigned char)src_mac[i])) {
            dst_mac[dst_idx++] = src_mac[i];
        }
    }
    dst_mac[dst_idx] = '\0';
}

static void add_mac_separator(char *src_mac, char *dst_mac) {
    if (src_mac == NULL || dst_mac == NULL || strlen(src_mac) != 12) {
        // 源MAC非12位时置空，标记无效
        dst_mac[0] = '\0';
        return;
    }

    // 按2位一组拼接冒号，构建标准MAC格式
    snprintf(dst_mac, MAX_MAC_LEN, 
             "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
             src_mac[0], src_mac[1],
             src_mac[2], src_mac[3],
             src_mac[4], src_mac[5],
             src_mac[6], src_mac[7],
             src_mac[8], src_mac[9],
             src_mac[10], src_mac[11]);
}

ATRESPONSE_t QL_AT_QMAC_Handle(char* cmdline, ATOP_t opType, char* response) {
    int err;
    FILE *fp = NULL;
    char raw_mac[MAX_MAC_LEN] = {0};  
    char clean_mac[MAX_CLEAN_MAC_LEN] = {0}; 
    char input_mac[MAX_CLEAN_MAC_LEN] = {0};
    char formatted_mac[MAX_MAC_LEN] = {0};
    char *command;
    int port;
    switch(opType) {
        case AT_READ_OP:
            fp = fopen(MAC_FILE_PATH, "r");
            if (fp == NULL) {
                LOGATCI(LOG_DEBUG, "[QL_AT_QMAC_HANDLE] open file failed");
                return AT_ERROR;
            }

            if (fgets(raw_mac, sizeof(raw_mac), fp) == NULL) {
                LOGATCI(LOG_DEBUG, "[QL_AT_QMAC_HANDLE] read mac failed");
                fclose(fp);
                return AT_ERROR;
            }
            fclose(fp);
            raw_mac[strcspn(raw_mac, "\n\r")] = '\0';

            clean_mac_separator(raw_mac, clean_mac);

            sprintf(response, "+QMAC: %s", clean_mac);
            return AT_OK;
        case AT_SET_OP:
            err = at_tok_nextstr(&cmdline, &command);
            if (err < 0) {
                return AT_ERROR;
            }
            if (strstr(command, "MACW")) {
                err = at_tok_nextint(&cmdline,&port);
                if (err < 0) {
                    LOGATCI(LOG_DEBUG, "[QL_AT_QMAC_HANDLE] invaild input info" );
                    return AT_ERROR;
                }
                if (port == 0){
                    err = at_tok_nextstr(&cmdline, &command);
                    if (err < 0) {
                        return AT_ERROR;
                    }
                    if (strlen(command) != 12) {
                        LOGATCI(LOG_DEBUG, "[QL_AT_QMAC_HANDLE] invalid input mac input:%s",command);
                        return AT_ERROR;
                    }
                    add_mac_separator(command, formatted_mac);
                    if (formatted_mac[0] == '\0') {
                        LOGATCI(LOG_DEBUG, "[QL_AT_QMAC_HANDLE] format mac failed");
                        return AT_ERROR;
                    }
                    fp = fopen(MAC_FILE_PATH, "w");
                    if (fp == NULL) {
                        LOGATCI(LOG_DEBUG, "[QL_AT_QMAC_HANDLE] open file for write failed");
                        return AT_ERROR;
                    }
                    if (fputs(formatted_mac, fp) == EOF || fputc('\n', fp) == EOF) {
                        LOGATCI(LOG_DEBUG, "[QL_AT_QMAC_HANDLE] write mac failed");
                        fclose(fp);
                        return AT_ERROR;
                    }
                    fclose(fp);
                    system("sync");
                    return AT_OK;
                }
            }
            if (strstr(command, "MACR")) {
                err = at_tok_nextint(&cmdline,&port);
                if (err < 0) {
                    LOGATCI(LOG_DEBUG, "[QL_AT_QMAC_HANDLE] invaild input info" );
                    return AT_ERROR;
                }
                if (port == 0){
                    fp = fopen(MAC_FILE_PATH, "r");
                    if (fp == NULL) {
                        LOGATCI(LOG_DEBUG, "[QL_AT_QMAC_HANDLE] open file failed");
                        return AT_ERROR;
                    }
        
                    if (fgets(raw_mac, sizeof(raw_mac), fp) == NULL) {
                        LOGATCI(LOG_DEBUG, "[QL_AT_QMAC_HANDLE] read mac failed");
                        fclose(fp);
                        return AT_ERROR;
                    }
                    fclose(fp);
                    raw_mac[strcspn(raw_mac, "\n\r")] = '\0';
        
                    clean_mac_separator(raw_mac, clean_mac);
        
                    sprintf(response, "MACR: \"%s\"", clean_mac);
                    return AT_OK;
                }
            }
            return AT_ERROR;
            break;
        case AT_TEST_OP:
        default:
            return AT_OK;
            break;
    }

    return AT_ERROR;
}

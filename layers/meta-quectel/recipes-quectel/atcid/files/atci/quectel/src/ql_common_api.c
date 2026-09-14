#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <poll.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <time.h>
#include <stdio.h>

#include <sys/types.h> 
#include <sys/stat.h>

#include "../atcid.h"
#include "../atcid_util.h"
#include "../atcid_serial.h"
#include "../at_tok.h"

#include "quectel-buildconfig-gen.h"
#include "ql_at_factory.h"
#include "ql_atcmd_info.h"
#include "ql_common_api.h"

Quec_wifi_fsg_data  *qwifi_data;

static int quec_write(char *dev,Quec_wifi_fsg_data *data)
{	
	int count, fd;

	fd = open(dev, O_RDWR | O_CREAT, 0644);
	if(fd < 0){
		LOGATCI(LOG_DEBUG,"quec_write open error \n");
		return -1;
	}

	count = write(fd, (char *)data, sizeof(Quec_wifi_fsg_data));
	fsync(fd);
	close(fd);
	if(count==sizeof(Quec_wifi_fsg_data)){
		LOGATCI(LOG_DEBUG,"quec_write OK \n");
	    return 1;
	}
	system("sync");

    return 0;
}

static int quec_read(char *dev,Quec_wifi_fsg_data *data)
{
	int count,fd;
	fd = open(dev, O_RDWR);
	if(fd < 0)
		return -1;	
	count = read(fd, (char *)data, sizeof(Quec_wifi_fsg_data));
	close(fd);
	if(count==sizeof(Quec_wifi_fsg_data))
	return 1;
	else 
	return 0;
}
int modem_sync(void)
{
	memcpy(qwifi_data->magic_head,fsg_head_string,sizeof(qwifi_data->magic_head));
	memcpy(qwifi_data->magic_tail,fsg_tail_string,sizeof(qwifi_data->magic_tail));
	LOGATCI(LOG_DEBUG,"modem_sync!!!!! magic_head:%s",qwifi_data->magic_head);
	return quec_write(DEV_DATA, qwifi_data);
}
 int modem_backup(void)
{

	memcpy(qwifi_data->magic_head,fsc_head_string,sizeof(qwifi_data->magic_head));
	memcpy(qwifi_data->magic_tail,fsc_tail_string,sizeof(qwifi_data->magic_tail));
	LOGATCI(LOG_DEBUG,"modem_backup!!!!!");
	return quec_write(DEV_BACKUP,qwifi_data);
}

/*===========================================================================
  start and stop wifi
===========================================================================*/
void start_wifi()
{
    //echo 5 > /sys/module/wlan/parameters/con_mode
    system("ifconfig wlan0 up &");
    system("rmmod wlan");
	sleep(1);
	system("insmod /lib/modules/quectel-wifi/updates/wlan.ko con_mode=5");
	sleep(1);
    system("ifconfig wlan0 up");
}

void stop_wifi()
{
	system("killall -9 Qcmbr");
}

void restart_eth()
{
	system("ifconfig usb0 down");
	sleep(1);
	system("ifconfig usb0 up");
	system("ip addr add 192.168.5.12/24 dev usb0");
	system("Qcmbr -v &");
}

/*===========================================================================
  start and stop bluetooth
===========================================================================*/
void start_bluetooth()
{
	system("killall -9 Btdiag");
	system("killall -9 hciattach");
	system("killall -9 qcom-hciattach");
    system("echo 0 > /sys/devices/platform/rfkill/bt_en");
	sleep(1);
	system("echo 1 > /sys/devices/platform/rfkill/bt_en");
	sleep(1);
	system("qcom-hciattach /dev/ttyHS1 qca -t120 3000000 flow");
	system("hciconfig hci0 up");
	system("Btdiag UDT=yes PORT=2391 IOType=USB QDARTIOType=ethernet BT-DEVICE=hci0 &");
}

void start_bluetooth_power()
{
	system("killall -9 Btdiag");
	system("killall -9 hciattach");
	system("killall -9 qcom-hciattach");
	LOGATCI(LOG_DEBUG,"start_bluetooth_power");
    system("echo 0 > /sys/devices/platform/rfkill/bt_en");
	sleep(1);
	system("echo 1 > /sys/devices/platform/rfkill/bt_en");
	sleep(1);
	system("btconfig download > /var/log/log_download.txt");
	sleep(2);
	system("btconfig reset > /var/log/log_reset.txt");
	LOGATCI(LOG_DEBUG,"start_bluetooth_power  end");
}


void stop_bluetooth()
{
	system("killall -9 Btdiag");
	system("killall -9 hciattach");
	system("killall -9 qcom-hciattach");
	system("echo 0 > /sys/devices/platform/rfkill/bt_en");
}


int fsg_rmts_fresh_data(void)
{
	Quec_wifi_fsg_data *buf=malloc(sizeof(Quec_wifi_fsg_data));
	memset(buf,0,sizeof(Quec_wifi_fsg_data));
	quec_read(DEV_DATA,buf);
	LOGATCI(LOG_DEBUG,"temp buff %s %s\n",buf->magic_head,buf->magic_tail);
	if((strncmp(buf->magic_head,fsg_head_string,9)!=0)||(strncmp(buf->magic_tail,fsg_tail_string,9)!=0))
		return 0;
	memcpy(qwifi_data,buf,sizeof(Quec_wifi_fsg_data));
	LOGATCI(LOG_DEBUG,"qwifi_data %s %s\n",qwifi_data->magic_head,qwifi_data->magic_tail);
	free(buf);
	LOGATCI(LOG_DEBUG,"Quec_fsg_rmts_fresh_data!!");
	return 1;
}
int fsc_rmts_fresh_data(void)
{
	Quec_wifi_fsg_data *buf=malloc(sizeof(Quec_wifi_fsg_data));
	memset(buf,0,sizeof(Quec_wifi_fsg_data));
	quec_read(DEV_BACKUP,buf);
	LOGATCI(LOG_DEBUG,"temp buff %s %s\n",buf->magic_head,buf->magic_tail);
	if((strncmp(buf->magic_head,fsc_head_string,9)!=0)||(strncmp(buf->magic_tail,fsc_tail_string,9)!=0))
		return 0;
	memcpy(qwifi_data,buf,sizeof(Quec_wifi_fsg_data));
	LOGATCI(LOG_DEBUG,"qwifi_data %s %s\n",qwifi_data->magic_head,qwifi_data->magic_tail);
	free(buf);
	LOGATCI(LOG_DEBUG,"Quec_fsc_rmts_fresh_data!!");
	return 1;
}

int fct_data_init(void)
{
	int fd = -1;
	int read_len = -1;

	if (qwifi_data == NULL) {
		qwifi_data = malloc(sizeof(Quec_wifi_fsg_data));
		if (qwifi_data == NULL) {
			LOGATCI(LOG_DEBUG, "Failed to allocate memory for qwifi_data\n");
			return -1;
		}
	}
	memset((char *)qwifi_data, 0, sizeof(Quec_wifi_fsg_data));

	///////////////////////////////////////////////////////////////////////////
	// 2. 获取保存在 主，备份存储中存储的数据，保存到 全局变量 qwifi_data 中
	//      如果为空，则初始化默认值
	///////////////////////////////////////////////////////////////////////////
	if (fsg_rmts_fresh_data() == 0) {
		if (fsc_rmts_fresh_data() == 0) {
		LOGATCI(LOG_DEBUG,"faker_modem_creat new empty file!!!!!\n");
		memset( (char *)qwifi_data,0, sizeof(Quec_wifi_fsg_data));
		strncpy(qwifi_data->sn,"11111",5);//delet ""
		strncpy(qwifi_data->wifi_mac,"000000000000",12);//delet ""
		strncpy(qwifi_data->bt_mac,"000000000000",12);//delet ""
	}

	modem_sync();
	}

	
    return 0;
}

const int MAX_LEN = 45;

int isHexChar(char c) {
    return (c >= '0' && c <= '9') || \
    (c >= 'a' && c <= 'f') || \
    (c >= 'A' && c <= 'F') || \
	(c == 'x') || \
	(c == 'X') || \
    (c == ',');
}

int validateInput(const char* input) {
    LOGATCI(LOG_DEBUG,"%s\n",input);
    int i;
    for (i = 0; (input[i] != '\0') && (input[i] != '\n'); i++) {
        if (!isHexChar(input[i])) {
			LOGATCI(LOG_DEBUG,"%c\n",input[i]);
            return 0;
        }
    }
    LOGATCI(LOG_DEBUG,"LEN=%d \n",i);
    if(MAX_LEN < i){
        return 0;
    }
    return 1;
}

int extract_data(char* read_br,int start_num)
{
	char pkt_rcvd[32]={0};
	memset(pkt_rcvd, 0, sizeof(pkt_rcvd));
	int pkt_num=0;
	long int temp;	//pkt_bum num
	char *endptr;
	char pkt_result[32]={0};
	memset(pkt_result, 0, sizeof(pkt_result));
	char pkt_result_r[32]={0};
	memset(pkt_result_r, 0, sizeof(pkt_result_r));
	for(int i=0;i<5;i++)
	{
		
		int index=0;
		int read_index = 0;           // 读取字符串的索引
		memcpy(pkt_rcvd, &read_br[start_num+i*96], 11 * sizeof(read_br[0]));
		LOGATCI(LOG_DEBUG,"pkt_rcvd:%s \n",pkt_rcvd);
		while (pkt_rcvd[index] != '\0') {
			if (pkt_rcvd[index] != ' ') {
				// 如果不是空格，则复制到 pkt_result 中
				pkt_result[read_index++] = pkt_rcvd[index];
			}
			// 移动到下一个字符
			index++;
		}
		pkt_result[read_index] = '\0'; // 添加字符串结束符
		
		pkt_result_r[0] = pkt_result[6];
		pkt_result_r[1] = pkt_result[7];
		pkt_result_r[2] = pkt_result[4];
		pkt_result_r[3] = pkt_result[5];
		pkt_result_r[4] = pkt_result[2];
		pkt_result_r[5] = pkt_result[3];
		pkt_result_r[6] = pkt_result[0];
		pkt_result_r[7] = pkt_result[1];
		pkt_result_r[8] = '\0';  // 添加字符串结束符
		
		temp = strtol(pkt_result_r, &endptr, 16);
		if (*endptr != '\0' || errno == ERANGE) {
			LOGATCI(LOG_DEBUG,"fial\n");
		} 
		LOGATCI(LOG_DEBUG,"pkt_result :%d %s\n",temp,pkt_result_r);
		pkt_num+=temp;
		memset(pkt_result, 0, sizeof(pkt_result));
		memset(pkt_rcvd, 0, sizeof(pkt_rcvd));
		memset(pkt_result_r, 0, sizeof(pkt_result_r));
		
	}
	LOGATCI(LOG_DEBUG,"pkt_num = %d\n",pkt_num);
	return pkt_num;
}

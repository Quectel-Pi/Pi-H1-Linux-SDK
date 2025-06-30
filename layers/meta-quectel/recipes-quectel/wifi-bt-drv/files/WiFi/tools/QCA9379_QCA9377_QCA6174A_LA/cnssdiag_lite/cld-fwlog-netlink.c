/*
 * Copyright (c) 2015-2017 Qualcomm Atheros, Inc.
 *
 * All Rights Reserved.
 * Qualcomm Atheros Confidential and Proprietary.
 */

/*
 * hostapd / VLAN initialization
 * Copyright 2003, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

/*
 * Copyright (c) 2013-2014 The Linux Foundation. All rights reserved.
 *
 * Previously licensed under the ISC license by Qualcomm Atheros, Inc.
 *
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * This file was originally distributed by Qualcomm Atheros, Inc.
 * under proprietary terms before Copyright ownership was assigned
 * to the Linux Foundation.
 */



#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <getopt.h>
#include <limits.h>
#include <asm/types.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/capability.h>
#include <sys/prctl.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <linux/prctl.h>
#include <pwd.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/wireless.h>

#include "cld-diag-parser.h"

#define CNSS_INTF "wlan0"
#define DELAY_IN_S 3
#define FREE_MEMORY_THRESHOLD 100

const char options[] =
"Options:\n\
-c, --enable host log(no parameter value)\n\
-x, --enable FW diag message(value: 0-disable, 1~31-enable)\n\
-e, --enable FW diag event/log(value: 0-disable, 1-enable)\n\
-l, --save log to file by QXDM format(same path with tool, folder wlan_logs)\n\
currently only host log and FW diag message could be shown in console window,\n\
FW diag event/log only could be saved into file and parsed by QXDM tool.\n\
The options can be given in any order.";

char *log_file_name_prefix[LOG_FILE_MAX] = {
	[HOST_LOG_FILE] = "host_driver_logs_",
	[FW_LOG_FILE] = "cnss_fw_logs_",
	[HOST_QXDM_LOG_FILE] = "host_qxdm_driver_logs_",
	[FW_QXDM_LOG_FILE] = "cnss_fw_qxdm_logs_"};
char *log_file_name_extn[LOG_FILE_MAX] = {
	[HOST_LOG_FILE] = "txt",
	[FW_LOG_FILE] = "txt",
	[HOST_QXDM_LOG_FILE] = "qmdl2",
	[FW_QXDM_LOG_FILE] = "qmdl"};
/*
struct sockaddr_nl src_addr, dest_addr;
struct nlmsghdr *nlh = NULL;
struct iovec iov;
static int sock_fd = -1;
struct msghdr msg; */

tSock fw_hdl = { 0 };
tSock host_hdl = { 0 };

int32_t optionflag = 0;

int log_path_flag = WRITE_TO_INTERNAL_SDCARD;
int delayed_count = 0;

int avail_space = 8192;  /* assume there is enough free disk space on x86 */
int max_file_size = MAX_FILE_SIZE;
int max_archives = MAX_FILE_INDEX;
unsigned int configured_buffer_size = 0;
int free_mem_threshold = FREE_MEMORY_THRESHOLD;
char wlan_log_dir_path[MAX_FILENAME_SIZE];

int max_no_buffer = 0;
int max_buff_size = DEFAULT_BUFFER_SIZE_MAX;

struct cnss_log_file_data log_file[LOG_FILE_MAX];
uint8_t gwlan_dev = CNSS_DIAG_WLAN_DEV_UNDEF;
char *cnss_diag_wlan_dev_name[CNSS_DIAG_WLAN_DEV_MAX] = {
	[CNSS_DIAG_WLAN_DEV_UNDEF] = "X_X",
	[CNSS_DIAG_WLAN_ROM_DEV] = "QCA6174",
	[CNSS_DIAG_WLAN_TUF_DEV] = "QCA93",
	[CNSS_DIAG_WLAN_HEL_DEV] = "WCN3990",
	[CNSS_DIAG_WLAN_NAP_DEV] = "XXX_XXX"};

char *cnssdiag_config_file = "/data/misc/wifi/cnss_diag.conf";
char log_capture_loc[MAX_SIZE] = { 0 };
char db_parse_path[MAX_SIZE] = { 0 };
char hbuffer_log_file[MAX_FILENAME_SIZE] =
		 "./wlan_logs/buffered_cnsshost_log.txt";
char fbuffer_log_file[MAX_FILENAME_SIZE] =
		 "./wlan_logs/buffered_cnssfw_log.txt";

struct s_hdlc_buf hdlc_buf;
#ifdef CONFIG_CLD80211_LIB
struct cld80211_ctx *cldctx = NULL;
#endif

boolean isDriverLoaded = FALSE;

char *line_trim(char *);

static void
usage(void)
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "%s\n", options);
	exit(-1);
}

/* function to find whether file exists or not */
static  int doesFileExist(const char *filename) {
	struct stat st;
	int result = stat(filename, &st);
	return result == 0;
}

#if DIAG_KEEP_ORIG
static uint32_t get_le32(const uint8_t *pos)
{
	return pos[0] | (pos[1] << 8) | (pos[2] << 16) | (pos[3] << 24);
}
#endif

/* Opens a directory wlan_logs and searches the same for the presence of
 * host and firmware log files. Sets the index of the file which is used
 * to store the logs before the reboot.
 */


void readDir(const char *dirName, enum fileType type) {
	DIR *fdir;
	struct dirent *dirent;
	int *files = NULL;
	char file_name[32];
	int i = 0, found = 0;
	int archives = 0;

	archives = max_archives;

	files = (int *)malloc(sizeof(int) * archives);
	if (NULL == files) {
	    debug_printf("%s: failed to allocate memory to host_files\n", __func__);
	    return;
	}

	memset(files, 0, (sizeof(int) * archives));
	fdir = opendir(dirName);
	if (NULL == fdir) {
	    debug_printf("%s: fdir is NULL\n", __func__);
	    free(files);
	    return;
	}
	chdir(dirName);
	while ((dirent = readdir(fdir)) != NULL) {
		found = 0;
		for (i = 0; i < archives; i++) {
			snprintf(file_name, sizeof(file_name), "%s%03d.%s",
					log_file_name_prefix[type], i, log_file_name_extn[type]);

			if ((0 == (strcmp(dirent->d_name, file_name)))) {
				files[i] = 1;
				found = 1;
			}
			if (found)
				break;
		}
	}
/*
 * value 0 at index 'i' indicates, host log file current.txt will be renamed
 * with the filename at 'i'th index.
  */
	i = 0;
	while (i < archives) {
		if (!files[i]) {
			log_file[type].index = i;
			break;
		}
		i++;
	}
	debug_printf("%s: File Index: HOST_LOG_FILE: %d, HOST_QXDM_LOG_FILE: %d, FW_LOG_FILE: %d\n",
		 __func__, log_file[HOST_LOG_FILE].index, log_file[HOST_QXDM_LOG_FILE].index, log_file[FW_LOG_FILE].index);
	free(files);
	closedir(fdir);
}

/*
 * rename host/firmware current.txt logfile with the corresponding
 * host/firmware log file with proper index and delete its next
 * index file to identify the last file name used to store the logs
 * after a reboot.
 */


void backup_file(enum fileType type)
{
	char newFileName[100];
	char delFileName[100];

	if (type >= LOG_FILE_MAX)
		return;

	snprintf(newFileName, sizeof(newFileName), "%s%s%03d.%s",
			wlan_log_dir_path, log_file_name_prefix[type],
			log_file[type].index, log_file_name_extn[type]);
	errno = 0;
	rename(log_file[type].name, newFileName);
	log_file[type].fp = NULL;
	log_file[type].index++;

	if (max_archives == log_file[type].index)
		log_file[type].index = 0;

	snprintf(delFileName, sizeof(delFileName), "%s%s%03d.%s",
			wlan_log_dir_path, log_file_name_prefix[type],
			log_file[type].index, log_file_name_extn[type]);
	unlink(delFileName);
}

static void cleanup(void)
{
	int i;
	if (host_hdl.sock_fd)
	close(host_hdl.sock_fd);
        if (fw_hdl.sock_fd)
        close(fw_hdl.sock_fd);
	for (i = HOST_LOG_FILE; i < LOG_FILE_MAX; i++) {
		/*if (i == FW_QXDM_LOG_FILE)
			buffer_fw_logs_log_pkt("", TRUE);*/
		if(log_file[i].fp) {
			fwrite(log_file[i].buf, sizeof(char), (log_file[i].buf_ptr - log_file[i].buf), log_file[i].fp);
			fflush(log_file[i].fp);
			fclose(log_file[i].fp);
		}
		if (log_file[i].buf) {
			free(log_file[i].buf);
			log_file[i].buf = NULL;
		}
	}

	if (hdlc_buf.buf) {
		free(hdlc_buf.buf);
		hdlc_buf.used_len = 0;
	}
}

static void clean_all_buff(struct cnss_log_file_data *lfd)
{
	t_buffer *next_buff = NULL, *start_buff;

	pthread_mutex_lock(&lfd->buff_lock);
	start_buff = (t_buffer *)lfd->buf;

	while (start_buff)
	{
		next_buff = start_buff->next;
		free(start_buff->start);
		free(start_buff);
		if (start_buff == next_buff)
			break;
		start_buff = next_buff;
	}
	pthread_mutex_unlock(&lfd->buff_lock);
}

#if DIAG_KEEP_ORIG
static void stop(int32_t signum)
{
	UNUSED(signum);
	if (optionflag & LOG_BUFF_FLAG) {
		printf("free all buffers\n ");
		clean_all_buff(&log_file[BUFFER_HOST_FILE]);
		log_file[BUFFER_HOST_FILE].buf = NULL;
		clean_all_buff(&log_file[BUFFER_FW_FILE]);
		log_file[BUFFER_FW_FILE].buf = NULL;
	}
	if(optionflag & LOGFILE_FLAG){
		printf("Recording stopped\n");
		cleanup();
	}

	if (pthread_mutex_destroy(&log_file[BUFFER_HOST_FILE].buff_lock))
		printf("Failed to destroy host buff_lock");

	if (pthread_mutex_destroy(&log_file[BUFFER_FW_FILE].buff_lock))
		printf("Failed to destroy firmware buff_lock");

	exit(0);
}

static void logbuffer_to_file(struct cnss_log_file_data *lfd)
{
	t_buffer *start_pos = (t_buffer *)lfd->buf_ptr;
	t_buffer *buffer_log;
	FILE *fp;
	size_t len;

	if (start_pos == NULL)
		return;
	fp = fopen(lfd->name, "w+");
	if (fp == NULL) {
		android_printf("Failed to open file %s\n", lfd->name);
		return;
	}

	pthread_mutex_lock(&lfd->buff_lock);
	buffer_log = start_pos->next;

	while (1) {
		len = fwrite(buffer_log->start, sizeof(char),
			     buffer_log->end - buffer_log->start, fp);
		if (len != (size_t)(buffer_log->end - buffer_log->start)) {
			android_printf("fwrite failed with len = %zu\n", len);
			break;
		}

		if (buffer_log == start_pos)
			break;
		buffer_log = buffer_log->next;
	}
	pthread_mutex_unlock(&lfd->buff_lock);
	fclose(fp);
}

static void update_buff_to_file(int32_t signum)
{
	UNUSED(signum);
#if DIAG_KEEP_ORIG
	logbuffer_to_file(&log_file[BUFFER_HOST_FILE]);
	logbuffer_to_file(&log_file[BUFFER_FW_FILE]);
#endif
	printf("Written buffers successfully into files\n");
}
#endif

#if DIAG_KEEP_ORIG
static void default_handler_sigusr1(int32_t signum)
{
	printf("SIGUSR1: %d is reserved for buffer logging\n", signum);
}

void process_cnss_log_file(uint8_t *dbgbuf)
{
	uint16_t length = 0;
	uint32_t dropped = 0;
	uint32_t timestamp = 0;
	uint32_t res =0;
	struct dbglog_slot *slot = (struct dbglog_slot *)dbgbuf;
	if (NULL != log_file[FW_LOG_FILE].fp)
		fseek(log_file[FW_LOG_FILE].fp, ftell(log_file[FW_LOG_FILE].fp), SEEK_SET);
	timestamp = get_le32((uint8_t *)&slot->timestamp);
	length = get_le32((uint8_t *)&slot->length);
	dropped = get_le32((uint8_t *)&slot->dropped);
	if (!((optionflag & SILENT_FLAG) == SILENT_FLAG)) {
		/* don't like this have to fix it */
		printf("Read bytes %ld timestamp=%u length=%u fw dropped=%u\n",
		    (log_file[FW_LOG_FILE].fp != NULL )? ftell(log_file[FW_LOG_FILE].fp) : 0, timestamp, length, dropped);
	}
	if (NULL != log_file[FW_LOG_FILE].fp) {
		if ((res = fwrite(dbgbuf, RECLEN, 1, log_file[FW_LOG_FILE].fp)) != 1) {
			perror("fwrite");
			return;
		}
		fflush(log_file[FW_LOG_FILE].fp);
	}
}
#endif

/*
 * This function trims any leading and trailing white spaces
 */
char *line_trim(char *str)
{
	char *ptr;

	if(*str == '\0') return str;

	/* Find the first non white-space */
	for (ptr = str; i_isspace(*ptr); ptr++);
	if (*ptr == '\0')
	    return str;

	/* This is the new start of the string*/
	str = ptr;

	/* Find the last non white-space and null terminate the string */
	ptr += strlen(ptr) - 1;
	for (; ptr != str && i_isspace(*ptr); ptr--);
	ptr[1] = '\0';

	return str;
}
#if DIAG_KEEP_ORIG
void read_config_file(void) {

	FILE *fp = NULL;
	char line_string[256];
	char *line;
	char string[100];
	static int path_flag = 0;
	static int size_flag = 0;
	int archive_flag = 0;
	int log_buff_flag = 0;
	int host_log_flag = 0;
	int fw_log_flag = 0;
	int memory_threshold_flag = 0;

	int log_storage = 0;

	fp = fopen(cnssdiag_config_file, "a+");
	if (NULL != fp) {
		fseek(fp, 0, SEEK_SET);
		while (!feof(fp)) {
			fgets(line_string, sizeof(line_string), fp);
			line = line_string;
			line = line_trim(line);
			if (*line == '#')
				continue;
			else {
				sscanf(line, "%s", string);
				if (strcmp(string, "LOG_PATH_FLAG") == 0) {
					sscanf((line + strlen("LOG_PATH_FLAG")
						+ FLAG_VALUE_OFFSET),
							"%s", string);
					log_path_flag = atoi(string);
					path_flag = 1;
					debug_printf("file_path=%d\n", log_path_flag);
				}
				else if (strcmp(string, "MAX_LOG_FILE_SIZE") == 0) {
					sscanf((line +	strlen("MAX_LOG_FILE_SIZE") +
						FLAG_VALUE_OFFSET),
							 "%s", string);
					max_file_size = (atoi(string) * (1024) * (1024));
					size_flag = 1;
					debug_printf("max_file_size=%d\n", max_file_size);
				}
				else if (strcmp(string, "MAX_ARCHIVES") == 0) {
					sscanf((line +	strlen("MAX_ARCHIVES") +
						FLAG_VALUE_OFFSET),
							 "%s", string);
					max_archives = atoi(string);
					if (max_archives >= 50)
						max_archives = 50;
					archive_flag = 1;
					debug_printf("max_archives=%d\n", max_archives);
				}
				else if (strcmp(string, "AVAILABLE_MEMORY_THRESHOLD") == 0) {
					sscanf((line +	strlen("AVAILABLE_MEMORY_THRESHOLD") +
						FLAG_VALUE_OFFSET), "%s", string);
					free_mem_threshold = atoi(string);
					memory_threshold_flag = 1;
					debug_printf("free_mem_threshold=%d\n", free_mem_threshold);
				} else if (strcmp(string, "LOG_STORAGE_PATH") == 0) {
					sscanf((line +	strlen("LOG_STORAGE_PATH") +
						FLAG_VALUE_OFFSET), "%s", string);
					if (strlen(string) != 0)
						strlcpy(log_capture_loc, string, sizeof(log_capture_loc));
					android_printf("log_capture_location  = %s\n", log_capture_loc);
					log_storage = 1;

				} else if (strcmp(string, "MAX_LOG_BUFFER") == 0) {
					sscanf((line +
						strlen("MAX_LOG_BUFFER") +
						FLAG_VALUE_OFFSET),
						"%s", string);
					max_buff_size = (atoi(string) *
							     (1024) * (1024));
					log_buff_flag = 1;
					debug_printf("max_buff_size=%d\n",
						     max_buff_size);
				} else if (strcmp(string, "HOST_LOG_FILE") == 0) {
					sscanf((line +
						strlen("HOST_LOG_FILE") +
						FLAG_VALUE_OFFSET), "%s",
						string);
					if (strlen(string) != 0)
						strlcpy(hbuffer_log_file,
							string,
							sizeof(hbuffer_log_file));

					android_printf("Host_logs_location  = %s\n",
							 hbuffer_log_file);
					host_log_flag = 1;
				} else if (strcmp(string, "FIRMWARE_LOG_FILE") == 0) {
					sscanf((line +
						strlen("FIRMWARE_LOG_FILE") +
						FLAG_VALUE_OFFSET),
					       "%s", string);
					if (strlen(string) != 0)
						strlcpy(fbuffer_log_file,
							string,
							sizeof(fbuffer_log_file));

					android_printf("firmware_logs_location  = %s\n",
						       fbuffer_log_file);
					fw_log_flag = 1;
				} else
					continue;
				}
				if ((1 == path_flag) && (1 == size_flag) &&
				    (archive_flag == 1) &&
				    (memory_threshold_flag) && log_storage &&
				    log_buff_flag && host_log_flag &&
				    fw_log_flag) {
					break;
				}
			}
			if (!path_flag)
				fprintf(fp, "LOG_PATH_FLAG = %d\n", log_path_flag);
			if (!size_flag)
				fprintf(fp, "MAX_LOG_FILE_SIZE = %d\n", MAX_FILE_SIZE /((1024) * (1024)));
			if (!archive_flag)
				fprintf(fp, "MAX_ARCHIVES = %d\n", MAX_FILE_INDEX);
			if (! log_storage)
				fprintf(fp, "LOG_STORAGE_PATH = %s\n", log_capture_loc);
			if (!memory_threshold_flag)
				fprintf(fp, "AVAILABLE_MEMORY_THRESHOLD = %d\n", FREE_MEMORY_THRESHOLD);
			if (!log_buff_flag)
				fprintf(fp, "MAX_LOG_BUFFER = %d\n",
					DEFAULT_BUFFER_SIZE_MAX/((1024) * (1024)));
			if (!host_log_flag)
				fprintf(fp, "HOST_LOG_FILE = %s\n",
					hbuffer_log_file);
			if (!fw_log_flag)
				fprintf(fp, "FIRMWARE_LOG_FILE = %s\n",
					fbuffer_log_file);
	}
	else {
		debug_printf("%s(%s): Configuration file not present "
				"set defualt log file path to internal "
				"sdcard\n", __func__, strerror(errno));
	}
	if (fp)
		fclose(fp);
}

#endif

void cnss_open_log_file(int max_size_reached, enum fileType type)
{
	struct stat st;
	int ret;

	if (log_path_flag == WRITE_TO_FILE_DISABLED) {
		optionflag &= ~(LOGFILE_FLAG);
		debug_printf("%s: write to file flag is disabled\n", __func__);
	}

	do {
		if (!max_size_reached)
			log_file[type].index = 0;


		if(stat(wlan_log_dir_path, &st) == 0 &&
			S_ISDIR(st.st_mode)) {
			android_printf("%s: directory %s created",
					__func__, wlan_log_dir_path);
		}
		else {
			ret = mkdir(wlan_log_dir_path, 755);
			android_printf("%s: create directory %s "
					"ret = %d errno= %d", __func__,
					wlan_log_dir_path, ret, errno);
		}
		readDir(wlan_log_dir_path, type);

		if (NULL == log_file[type].fp) {
			if (max_size_reached) {
				log_file[type].fp = fopen(log_file[type].name, "w");
			} else {
				log_file[type].fp = fopen(log_file[type].name, "a+");
				if ((log_file[type].fp != NULL) &&
						(ftell(log_file[type].fp) >=
							max_file_size)) {
					if ((avail_space  < free_mem_threshold) &&
							(log_path_flag ==
							WRITE_TO_INTERNAL_SDCARD)) {
						android_printf("Device free memory is insufficient");
						break;
					}
					fflush(log_file[type].fp);
					backup_file(type);
					fclose(log_file[type].fp);
					log_file[type].fp = fopen(log_file[type].name, "w");
				} else {
					if(NULL == log_file[type].fp)
						debug_printf("failed to open file %s with a+ mode\n", log_file[type].name);
					else
						debug_printf("file size %ld is less than max_file_size %d, file name: %s, type = %d\n",
							ftell(log_file[type].fp),
							max_file_size, log_file[type].name, type);
				}
			}
			if (NULL == log_file[type].fp) {
				debug_printf("Failed to open file %s: %d\n",
						log_file[type].name, errno);
			}
		}

		if (NULL == log_file[type].fp) {
			if (MAX_RETRY_COUNT != delayed_count) {
				debug_printf("%s: Sleep and poll again for %s "
						" sdcard\n", __func__,
						(log_path_flag == 1) ? "internal" : "external");
				sleep(DELAY_IN_S);
				delayed_count++;
			}
			else {
				delayed_count = 0;
				if (log_path_flag == WRITE_TO_EXTERNAL_SDCARD) {
					log_path_flag = WRITE_TO_INTERNAL_SDCARD;
					debug_printf("%s: External sdcard not mounted try for"
							" internal sdcard ", __func__);
					continue;
				}
				else {
					debug_printf("%s: Internal sdcard not yet mounted"
						" Disable writing logs to a file\n", __func__);
					log_path_flag = WRITE_TO_FILE_DISABLED;
					break;
				}
			}
		} else
			break;
	} while(1);
	return;
}

/*
 * Process FW debug, FW event and FW log messages
 * Read the payload and process accordingly.
 *
 */
void process_cnss_diag_msg(int cmd, tAniCLDHdr *wnl)
{

	uint8_t *eventbuf = ((uint8_t *)wnl + sizeof(wnl->radio));
	uint16_t diag_type = 0;
#if DIAG_KEEP_ORIG
	uint8_t *dbgbuf;
	uint32_t event_id = 0;
	uint16_t length = 0;
	struct dbglog_slot *slot;
	uint32_t dropped = 0;

	dbgbuf = eventbuf;
#endif
	diag_type = *(uint16_t *)eventbuf;
	eventbuf += sizeof(uint16_t);
#if DIAG_KEEP_ORIG
	length = *(uint16_t *)eventbuf;
#endif
	eventbuf += sizeof(uint16_t);

	if (cmd == WLAN_NL_CNSS_FW_MSG) {
		/* Raw diag buffer from kernel */
		if (diag_type == 0)
			process_cnss_fw_message(wnl, optionflag);
		else
			printf("!!Unsupport diag type %d.\n", diag_type);
	}

	if (cmd == WLAN_NL_MSG_CNSS_HOST_MSG) {
		if ((wnl->wmsg.type == ANI_NL_MSG_LOG_HOST_MSG_TYPE) ||
			(wnl->wmsg.type == ANI_NL_MSG_LOG_MGMT_MSG_TYPE)) {
			if ((optionflag & LOGFILE_FLAG) && (!doesFileExist(log_file[HOST_LOG_FILE].name))
				&& (log_path_flag == WRITE_TO_INTERNAL_SDCARD)&& log_file[HOST_LOG_FILE].fp) {
				if (fclose(log_file[HOST_LOG_FILE].fp) == EOF)
					perror("Failed to close host file ");
				log_file[HOST_LOG_FILE].index = 0;
				log_file[HOST_LOG_FILE].fp = fopen(log_file[HOST_LOG_FILE].name, "w");
				if (log_file[HOST_LOG_FILE].fp == NULL) {
					debug_printf("Failed to create a new file");
				}
			}
			process_cnss_host_message(wnl, optionflag);
		}
#if DIAG_KEEP_ORIG
		else if (wnl->wmsg.type == ANI_NL_MSG_LOG_FW_MSG_TYPE) {
			if ((optionflag & LOGFILE_FLAG) && (!doesFileExist(log_file[FW_LOG_FILE].name))
				&&(log_path_flag == WRITE_TO_INTERNAL_SDCARD)&& log_file[FW_LOG_FILE].fp) {
				if (fclose(log_file[FW_LOG_FILE].fp) == EOF)
					perror("Failed to close fw file ");
				log_file[FW_LOG_FILE].index = 0;
				log_file[FW_LOG_FILE].fp = fopen(log_file[FW_LOG_FILE].name, "w");
				if(log_file[FW_LOG_FILE].fp == NULL) {
					debug_printf("Failed to create a new file");
				}
			}
			process_pronto_firmware_logs(wnl, optionflag);
		}
#endif
	}
#if DIAG_KEEP_ORIG
	else if (cmd == WLAN_NL_MSG_CNSS_HOST_EVENT_LOG &&
		   (wnl->wmsg.type == ANI_NL_MSG_LOG_HOST_EVENT_LOG_TYPE)) {
		process_cnss_host_diag_events_log(
		    (char *)((char *)&wnl->wmsg.length +
			      sizeof(wnl->wmsg.length)),
		    optionflag);
	} else {
		if (diag_type == DIAG_TYPE_FW_EVENT) {
			eventbuf += sizeof(uint32_t);
			event_id = *(uint32_t *)eventbuf;
			eventbuf += sizeof(uint32_t);
			if (optionflag & QXDM_FLAG) {
				if (length)
					event_report_payload(event_id, length,
							     eventbuf);
				else
					event_report(event_id);
			}
		} else if (diag_type == DIAG_TYPE_FW_LOG) {
			/* Do nothing for now */
		} else if (diag_type == DIAG_TYPE_FW_DEBUG_MSG) {
			slot =(struct dbglog_slot *)dbgbuf;
			length = get_le32((uint8_t *)&slot->length);
			dropped = get_le32((uint8_t *)&slot->dropped);
			dbglog_parse_debug_logs(&slot->payload[0],
				    length, dropped, wnl->radio);
		} else if (diag_type == DIAG_TYPE_FW_MSG) {
			uint32_t version = 0;
			slot = (struct dbglog_slot *)dbgbuf;
			length = get_32((uint8_t *)&slot->length);
			version = get_le32((uint8_t *)&slot->dropped);
			if ((optionflag & LOGFILE_FLAG) && (!doesFileExist(log_file[FW_LOG_FILE].name))
				&&(log_path_flag == WRITE_TO_INTERNAL_SDCARD)&& log_file[FW_LOG_FILE].fp) {
				if (fclose(log_file[FW_LOG_FILE].fp) == EOF)
					perror("Failed to close fw file ");
				log_file[FW_LOG_FILE].index = 0;
				log_file[FW_LOG_FILE].fp = fopen(log_file[FW_LOG_FILE].name, "w");
				if(log_file[FW_LOG_FILE].fp == NULL) {
					debug_printf("Failed to create a new file");
				}
			}
			process_diagfw_msg(&slot->payload[0], length,
			    optionflag, version, sock_fd, wnl->radio);
		} else if (diag_type == DIAG_TYPE_HOST_MSG) {
			slot = (struct dbglog_slot *)dbgbuf;
			length = get_32((uint8_t *)&slot->length);
			process_diaghost_msg(slot->payload, length);
		} else {
			/* Do nothing for now */
		}
	}
#endif
}

/*
 * Open the socket and bind the socket with src
 * address. Return the socket fd if sucess.
 *
 */
static int32_t create_nl_socket()
{
	int32_t ret;
	
	
    if(host_hdl.enable) {
		host_hdl.sock_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_USERSOCK);
		if (host_hdl.sock_fd < 0) {
			fprintf(stderr, "1,Socket creation failed sock_fd 0x%x \n",
					host_hdl.sock_fd);
			return -1;
		}

		memset(&host_hdl.src_addr, 0, sizeof(host_hdl.src_addr));
		host_hdl.src_addr.nl_family = AF_NETLINK;
		host_hdl.src_addr.nl_groups = 0x01;
		host_hdl.src_addr.nl_pid = getpid(); /* self pid */

		ret = bind(host_hdl.sock_fd, (struct sockaddr *)&host_hdl.src_addr, sizeof(host_hdl.src_addr));
		if (ret < 0)
			{
			fprintf(stderr, "1.1,Socket creation failed sock_fd \n");
			close(host_hdl.sock_fd);
			return ret;
		}
    }

	if (fw_hdl.enable) {
		fw_hdl.sock_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_CUSTOM_FW);
		if (fw_hdl.sock_fd < 0) {
			fprintf(stderr, "2,Socket creation failed sock_fd 0x%x \n",
			        fw_hdl.sock_fd);
			return -1;
		}

		memset(&fw_hdl.src_addr, 0, sizeof(fw_hdl.src_addr));
		fw_hdl.src_addr.nl_family = AF_NETLINK;
		fw_hdl.src_addr.nl_groups = 0x01;
		fw_hdl.src_addr.nl_pid = getpid(); /* self pid */

		ret = bind(fw_hdl.sock_fd, (struct sockaddr *)&fw_hdl.src_addr,
			sizeof(fw_hdl.src_addr));
		if (ret < 0)
		{
                       fprintf(stderr, "1.2,Socket creation failed sock_fd \n");
			close(fw_hdl.sock_fd);
			return ret;
		}
	}
	return ret;
}

static int initial_file_path()
{
	char filepath[2*MAX_SIZE] = {0};

	int count;

	count = readlink("/proc/self/exe", filepath, 2*MAX_SIZE);

	if(count < 0 || count > 2*MAX_SIZE) {
	   fprintf(stderr, "%s: failed.\n", __func__);
	   return -1;
	}

	/* remove the app name from path */
	while(count > 0)
	{
	    if(filepath[count] == '/')
		break;
	    count--;
	}

	if(count > 0)
	   filepath[count] = '\0';
	else {
	   fprintf(stderr, "%s: failed.\n", __func__);
	   return -1;
	}

	snprintf(log_capture_loc, sizeof(log_capture_loc),
				"%s/wlan_logs", filepath);
	snprintf(db_parse_path, sizeof(db_parse_path),
				"%s/%s", filepath, DB_PATH);

	return 0;
}

static int initialize()
{
	if(initial_file_path())
		return -1;

	if(host_hdl.enable) {
		memset(&host_hdl.dest_addr, 0, sizeof(host_hdl.dest_addr));
		host_hdl.dest_addr.nl_family = AF_NETLINK;
		host_hdl.dest_addr.nl_pid = 0; /* For Linux Kernel */
		host_hdl.dest_addr.nl_groups = 0; /* unicast */

		if (host_hdl.nlh) {
			free(host_hdl.nlh);
			host_hdl.nlh = NULL;
		}
		host_hdl.nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(MAX_MSG_SIZE));
		if (host_hdl.nlh == NULL) {
			android_printf("%s Cannot allocate memory for nlh",
				__func__);
			return -1;
		}
		memset(host_hdl.nlh, 0, NLMSG_SPACE(MAX_MSG_SIZE));
		host_hdl.nlh->nlmsg_len = NLMSG_SPACE(MAX_MSG_SIZE);
		host_hdl.nlh->nlmsg_pid = getpid();
		host_hdl.nlh->nlmsg_type = WLAN_NL_MSG_CNSS_DIAG;
		host_hdl.nlh->nlmsg_flags = NLM_F_REQUEST;

	/*	memcpy(NLMSG_DATA(host_hdl.nlh), mesg, strlen(mesg)); */

		host_hdl.iov.iov_base = (void *)host_hdl.nlh;
		host_hdl.iov.iov_len = host_hdl.nlh->nlmsg_len;
		host_hdl.msg.msg_name = (void *)&host_hdl.dest_addr;
		host_hdl.msg.msg_namelen = sizeof(host_hdl.dest_addr);
		host_hdl.msg.msg_iov = &host_hdl.iov;
		host_hdl.msg.msg_iovlen = 1;
		}

	if (fw_hdl.enable) {
		memset(&fw_hdl.dest_addr, 0, sizeof(fw_hdl.dest_addr));
		fw_hdl.dest_addr.nl_family = AF_NETLINK;
		fw_hdl.dest_addr.nl_pid = 0; /* For Linux Kernel */
		fw_hdl.dest_addr.nl_groups = 0; /* unicast */
		
		if (fw_hdl.nlh) {
			free(fw_hdl.nlh);
			fw_hdl.nlh = NULL;
		}
		fw_hdl.nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(MAX_MSG_SIZE));
		if (fw_hdl.nlh == NULL) {
			android_printf("%s Cannot allocate memory for nlh",
				__func__);
			return -1;
		}
		memset(fw_hdl.nlh, 0, NLMSG_SPACE(MAX_MSG_SIZE));
		fw_hdl.nlh->nlmsg_len = NLMSG_SPACE(MAX_MSG_SIZE);
		fw_hdl.nlh->nlmsg_pid = getpid();
		fw_hdl.nlh->nlmsg_type = WLAN_NL_MSG_CNSS_DIAG;
		fw_hdl.nlh->nlmsg_flags = NLM_F_REQUEST;
		
	/*	memcpy(NLMSG_DATA(nlh), mesg, strlen(mesg)); */
		
		fw_hdl.iov.iov_base = (void *)fw_hdl.nlh;
		fw_hdl.iov.iov_len = fw_hdl.nlh->nlmsg_len;
		fw_hdl.msg.msg_name = (void *)&fw_hdl.dest_addr;
		fw_hdl.msg.msg_namelen = sizeof(fw_hdl.dest_addr);
		fw_hdl.msg.msg_iov = &fw_hdl.iov;
		fw_hdl.msg.msg_iovlen = 1;
	}

    hdlc_buf.buf = malloc(MAX_HDLC_BUF_SIZE);
	hdlc_buf.used_len = 0;
	return 1;
}

int init_log_file()
{
	boolean enable_log_file[LOG_FILE_MAX] = {FALSE, FALSE, FALSE};
	int i;

	if (optionflag & LOGFILE_FLAG) {
		enable_log_file[HOST_LOG_FILE] = TRUE;
		enable_log_file[FW_LOG_FILE] = TRUE;
	}
	if (optionflag & LOGFILE_QXDM_FLAG) {
		enable_log_file[HOST_QXDM_LOG_FILE] = TRUE;
		enable_log_file[FW_QXDM_LOG_FILE] = TRUE;
	}
	if (log_path_flag == WRITE_TO_EXTERNAL_SDCARD) {
		snprintf(wlan_log_dir_path, sizeof(wlan_log_dir_path),
				"%s", "/mnt/media_rw/sdcard1/wlan_logs/");
	} else if (log_path_flag == WRITE_TO_INTERNAL_SDCARD) {
		snprintf(wlan_log_dir_path, sizeof(wlan_log_dir_path),
				"%s/", log_capture_loc);
	}

	for (i = HOST_LOG_FILE; i < LOG_FILE_MAX; i++) {
		if ((i == BUFFER_HOST_FILE) || (i == BUFFER_FW_FILE)) {
			if (optionflag & LOG_BUFF_FLAG) {
				t_buffer *buff_ctx = NULL;
				if (i == BUFFER_HOST_FILE)
					snprintf(log_file[i].name,
						 sizeof(log_file[i].name),
						 "%s", hbuffer_log_file);
				else
					snprintf(log_file[i].name,
						 sizeof(log_file[i].name),
						 "%s", fbuffer_log_file);
				log_file[i].buf = (char*) malloc(sizeof(t_buffer));
				if (!log_file[i].buf)
					goto free_bufs;

				memset(log_file[i].buf, 0x00, (sizeof(t_buffer)));
				buff_ctx = (t_buffer *)log_file[i].buf;
				buff_ctx->start = (unsigned char *) malloc(EACH_BUF_SIZE);
				if (buff_ctx->start == NULL) {
					free(buff_ctx);
					goto free_bufs;
				}
				buff_ctx->end = buff_ctx->start;
				buff_ctx->next = buff_ctx;

				log_file[i].buf_ptr = log_file[i].buf;
				log_file[i].fp = NULL;
				if (pthread_mutex_init(&log_file[i].buff_lock,
						       NULL)) {
					android_printf("Failed to initialize buff_lock");
					goto free_bufs;
				}
			}
		} else {
			snprintf(log_file[i].name, sizeof(log_file[i].name),
				"%s%scurrent.%s", wlan_log_dir_path, log_file_name_prefix[i], log_file_name_extn[i]);
			if (enable_log_file[i] == FALSE)
				continue;
			if (!(optionflag & BUFFER_SIZE_FLAG))
				configured_buffer_size = DEFAULT_LOG_BUFFER_LIMIT;
			log_file[i].free_buf_mem = configured_buffer_size;

			log_file[i].buf = (char*) malloc(configured_buffer_size * sizeof(char));
			if (!log_file[i].buf) {
				goto free_bufs;
			}
			memset(log_file[i].buf, 0x00, (configured_buffer_size * sizeof(char)));
			log_file[i].buf_ptr = log_file[i].buf;
			cnss_open_log_file(FALSE, i);
		}
	}

	if (optionflag & LOGFILE_QXDM_FLAG) {
		struct qmdl_file_hdr file_hdr;
		file_hdr.hdr_len = sizeof(struct qmdl_file_hdr);
		file_hdr.version = 1;
		file_hdr.data_type = 0;
		file_hdr.guid_list_count = 0;
		cnss_write_buf_logs(sizeof(struct qmdl_file_hdr), (char *)&file_hdr, HOST_QXDM_LOG_FILE);
		cnss_write_buf_logs(sizeof(struct qmdl_file_hdr), (char *)&file_hdr, FW_QXDM_LOG_FILE);
	}
	return 0;

free_bufs:
	android_printf("malloc failed, free bufs allocated so far");
	for (; i >= 0; i--) {
		if (log_file[i].buf) {
			if ((i == BUFFER_HOST_FILE) || (i == BUFFER_FW_FILE)) {
				t_buffer *buff_ctx = (t_buffer *)log_file[i].buf;
				if (buff_ctx->start)
					free(buff_ctx->start);
			}
			free(log_file[i].buf);
		}
	}
	return -1;
}

#if DIAG_KEEP_ORIG
static int getAvailableSpace(const char* path) {
	struct statvfs stat;
	if (statvfs(path, &stat) != 0) {
		return -1;
	}
	/* the available size is f_bsize * f_bavail , return in MBs */
	return ((stat.f_bsize * stat.f_bavail) / (1024 * 1024));
}
#endif

static void cnss_diag_find_wlan_dev()
{
#if DIAG_KEEP_ORIG
	int i, sk;
	char buf[512], *hw_name;
	struct iwreq rq;
#define WLAN_VERSION_IOCTL 0x8be5

	sk = socket(AF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return;
	strlcpy(rq.ifr_name, CNSS_INTF, IFNAMSIZ);
	rq.u.data.pointer = (caddr_t) buf;
	rq.u.data.flags = 1;
	rq.u.data.length = 0;
	system("ifconfig "CNSS_INTF" up");
	if(ioctl(sk, WLAN_VERSION_IOCTL, &rq) < 0) {
		printf("Not CLD WLAN Driver\n");
		return;
	}
	buf[rq.u.data.length] = '\0';
	hw_name = strstr(buf, "HW:");
	if (!hw_name)
		return;
	hw_name += strlen("HW:");
	gwlan_dev = CNSS_DIAG_WLAN_ROM_DEV;
	for (i = CNSS_DIAG_WLAN_ROM_DEV; i < CNSS_DIAG_WLAN_DEV_MAX; i++) {
		if (strncmp(hw_name, cnss_diag_wlan_dev_name[i], strlen(cnss_diag_wlan_dev_name[i])) == 0) {
			gwlan_dev = i;
			break;
		}
	}
	close(sk);
#else
	gwlan_dev = CNSS_DIAG_WLAN_NAP_DEV;
#endif
}

#ifdef CONFIG_CLD80211_LIB
/* Event handler */
static int event_handler(struct nl_msg *msg, void *arg)
{
	struct nlattr *attrs[CLD80211_ATTR_MAX + 1];
	struct genlmsghdr *header;
	struct nlattr *tb_vendor[CLD80211_ATTR_MAX + 1];
	struct  nlmsghdr *nlh = nlmsg_hdr(msg);

	UNUSED(arg);

	header = (struct genlmsghdr *)nlmsg_data(nlh);
	if (header->cmd == WLAN_NL_MSG_CNSS_HOST_MSG ||
	    (unsigned long)nlh->nlmsg_len > sizeof(struct dbglog_slot)) {
		int result = nla_parse(attrs, CLD80211_ATTR_MAX, genlmsg_attrdata(header, 0),
				       genlmsg_attrlen(header, 0), NULL);

		if (result || !attrs[CLD80211_ATTR_VENDOR_DATA]) {
			printf("No valid data received\n");
			return 0;
		}

		nla_parse(tb_vendor, CLD80211_ATTR_MAX,
			  (struct nlattr *)nla_data(attrs[CLD80211_ATTR_VENDOR_DATA]),
			  nla_len(attrs[CLD80211_ATTR_VENDOR_DATA]), NULL);

		if (tb_vendor[CLD80211_ATTR_DATA]) {
			tAniCLDHdr *clh =
				(tAniCLDHdr *)nla_data(tb_vendor[CLD80211_ATTR_DATA]);
#ifndef CNSS_DIAG_PLATFORM_WIN
			if (gwlan_dev == CNSS_DIAG_WLAN_DEV_UNDEF)
				cnss_diag_find_wlan_dev();
#endif
			process_cnss_diag_msg(header->cmd, clh);
		} else
			printf("%s: CLD80211_ATTR_DATA not found\n", __func__);

	}

	return 0;
}
#endif /* CONFIG_CLD80211_LIB */


void *fw_thread_rotine(void *args)
{
	int res = 0;

	UNUSED(args);

	while (1) {
		if ((res = recvmsg(fw_hdl.sock_fd, &fw_hdl.msg, 0)) < 0)
			continue;

		if ((res >= (int)sizeof(struct dbglog_slot)) ||
		    (fw_hdl.nlh->nlmsg_type == WLAN_NL_MSG_CNSS_HOST_EVENT_LOG)) {
			process_cnss_diag_msg(fw_hdl.nlh->nlmsg_type,
					      (tAniCLDHdr *)(fw_hdl.nlh + 1));
			memset(fw_hdl.nlh,0,NLMSG_SPACE(MAX_MSG_SIZE));
		} else {
			/* Ignore other messages that might be broadcast */
			printf("!!FW Recv %d bytes, with nlmsg %d. Not process.\n",
				res, fw_hdl.nlh->nlmsg_type);
			continue;
		}
	}


	pthread_exit(NULL);
}

void *host_thread_rotine(void *args)
{

	int res = 0;

	UNUSED(args);

	while ( 1 )  {
			if ((res = recvmsg(host_hdl.sock_fd, &host_hdl.msg, 0)) < 0)
				continue;
	
			if ((res >= (int)sizeof(struct dbglog_slot)) ||
				(host_hdl.nlh->nlmsg_type == WLAN_NL_MSG_CNSS_HOST_EVENT_LOG)) {
			/*	if (fetch_free_mem && (optionflag & LOGFILE_FLAG)) {
					avail_space = getAvailableSpace("/sdcard");
					if (avail_space != -1)
						fetch_free_mem = FALSE;
				} */
				//Identify driver once on receiving NL MSG from driver
				if (gwlan_dev == CNSS_DIAG_WLAN_DEV_UNDEF) {
					// Default to Rome (compaitble with legacy device logging)
					cnss_diag_find_wlan_dev();
				}
				process_cnss_diag_msg(host_hdl.nlh->nlmsg_type,
						      (tAniCLDHdr *)(host_hdl.nlh + 1));
				memset(host_hdl.nlh,0,NLMSG_SPACE(MAX_MSG_SIZE));
			} else {
				/* Ignore other messages that might be broadcast */
				printf("!!Recv %d bytes, with nlmsg %d. Not process.\n",
					res, host_hdl.nlh->nlmsg_type);
				continue;
			}
		}

	pthread_exit(NULL);

}


#ifdef CONFIG_CLD80211_LIB
void *genl_thread_routine(void *args)
{
	UNUSED(args);
	cld80211_recv(cldctx, -1, true, &event_handler, NULL);
	pthread_exit(NULL);
}
#endif

void create_fw_sock_thread()
{
	pthread_t fw_thread;
	pthread_create(&fw_thread, NULL, fw_thread_rotine, NULL);
}

void create_host_sock_thread()
{
	 pthread_t host_thread;
	 pthread_create(&host_thread, NULL, host_thread_rotine, NULL);
}

#ifdef CONFIG_CLD80211_LIB
void create_genl_sock_thread()
{
	pthread_t genl_thread;
	pthread_create(&genl_thread, NULL, genl_thread_routine, NULL);
}
#endif

int32_t fw_log_mask = MASK_LOW_LEVEL;

int32_t main(int32_t argc, char *argv[])
{
	int32_t ret = 0;
	int32_t c;
	uint8_t diag_event_log_enable = 0;
	uint8_t diag_event_log_set = 0;
	uint8_t diag_msg_mask_set = 0;

	int32_t option_index = 0;
	static struct option long_options[] = {
		{"host_log", 0, NULL, 'c'},
		{"fw_diag_message",required_argument, NULL, 'x'},
		{"qxdm_log_file", 0, NULL, 'l'},
		{"fw_diag_log_event",required_argument, NULL, 'e'},
		{ 0, 0, 0, 0}
	};

	while (1) {
		c = getopt_long (argc, argv, "cx:le:", long_options,
				 &option_index);
		if (c == -1) break;

		switch (c) {
		case 'b':
			optionflag |= BUFFER_SIZE_FLAG;
			if (optarg != NULL) {
				configured_buffer_size = atoi(optarg) * 1024;
			}
			break;

		case 'c':
			optionflag |= CONSOLE_FLAG;
			host_hdl.enable = TRUE;
			break;
			
		case 'x':
			optionflag |= CONSOLE_FLAG;
			fw_hdl.enable = TRUE;
                        if (optarg != NULL) {
				fw_log_mask = atoi(optarg);
				diag_msg_mask_set = 1;
			}
			break;

		case 'l':
			optionflag |= LOGFILE_QXDM_FLAG;
			break;

		case 'e':
			optionflag |= CONSOLE_FLAG;
			fw_hdl.enable = TRUE;
                        if (optarg != NULL) {
				diag_event_log_enable = atoi(optarg);
				diag_event_log_set = 1;
			}
			break;

		default:
			usage();
		}
	}

	if (!(optionflag & (LOGFILE_FLAG | CONSOLE_FLAG | SILENT_FLAG |
			    DEBUG_FLAG | BUFFER_SIZE_FLAG | LOG_BUFF_FLAG |
			    DECODED_LOG_FLAG | LOGFILE_QXDM_FLAG))) {
		usage();
		return -1;
	}

	printf("note: enter 'k' to exit process .\n");

#ifdef CONFIG_CLD80211_LIB
	cldctx = cld80211_init();
	if (cldctx != NULL) {
		ret = cld80211_add_mcast_group(cldctx, "fw_logs");
		if (ret) {
			printf("Failed to add mcast group: fw_logs");
			cld80211_deinit(cldctx);
			return -1;
		}
		ret = cld80211_add_mcast_group(cldctx, "host_logs");
		if (ret) {
			printf("Failed to add mcast group: host_logs");
			cld80211_remove_mcast_group(cldctx, "fw_logs");
			cld80211_deinit(cldctx);
			return -1;
		}
		ret = cld80211_add_mcast_group(cldctx, "diag_events");
		if (ret) {
			printf("Failed to add mcast group:diag_events");
			cld80211_remove_mcast_group(cldctx, "host_logs");
			cld80211_remove_mcast_group(cldctx, "fw_logs");
			cld80211_deinit(cldctx);
			return -1;
		}
	} else
#endif /* CONFIG_CLD80211_LIB */
	{
		printf("Failed to initialize cld80211 proceed with legacy procedure\n");
		ret = create_nl_socket();

		if (ret < 0) {
			fprintf(stderr, "3, Socket creation failed, result 0x%x \n",
				ret);
			return -1;
		}

		if (initialize() < 0)
			return -1;
	}
	max_no_buffer = max_buff_size/EACH_BUF_SIZE;
	if (optionflag & LOGFILE_FLAG || optionflag & LOGFILE_QXDM_FLAG ||
	    optionflag & LOG_BUFF_FLAG) {
		/* initial log file */
		if (init_log_file())
			goto end;
	}

#ifdef CONFIG_CLD80211_LIB
	if (cldctx) {
		create_genl_sock_thread();
	} else
#endif

	{
		/* Create thread for Host socket */
		if(host_hdl.enable)
			create_host_sock_thread();

		/* Create thread for FW socket */
		if (fw_hdl.enable) {
			create_fw_sock_thread();

			if(diag_msg_mask_set) {
				diag_local_enable_debug_msg();
			}

			if(diag_event_log_set) {
				switch(diag_event_log_enable) {
				case 0:
					diag_local_event_toggle(0);
					diag_local_disable_log();
					break;

				case 1:
					diag_local_event_toggle(1);
					diag_local_enable_event();
					diag_local_enable_log();
					break;

				default:
					fprintf(stderr, "invalid -e cmd paramter %d \n",
						diag_event_log_enable);
					break;
				}
			}
		}
	}

	while(getchar()!= 'k')
	{
		;
	}

end:
	if (optionflag & LOG_BUFF_FLAG) {
		printf("free all buffers\n ");
		clean_all_buff(&log_file[BUFFER_HOST_FILE]);
		log_file[BUFFER_HOST_FILE].buf = NULL;
		clean_all_buff(&log_file[BUFFER_FW_FILE]);
		log_file[BUFFER_FW_FILE].buf = NULL;
	}
	if (optionflag & LOGFILE_FLAG || optionflag & LOGFILE_QXDM_FLAG)
		cleanup();

#ifdef CONFIG_CLD80211_LIB
	if (cldctx) {
		cld80211_remove_mcast_group(cldctx, "host_logs");
		cld80211_remove_mcast_group(cldctx, "fw_logs");
		cld80211_remove_mcast_group(cldctx, "diag_events");
		cld80211_deinit(cldctx);
	} else
#endif
	{

		if(host_hdl.enable) {
			close(host_hdl.sock_fd);
			free(host_hdl.nlh);
		}

		if(fw_hdl.enable) {
			close(fw_hdl.sock_fd);
			free(fw_hdl.nlh);
		}
	}

	if (pthread_mutex_destroy(&log_file[BUFFER_HOST_FILE].buff_lock))
		android_printf("Failed to destroy host buff_lock");

	if (pthread_mutex_destroy(&log_file[BUFFER_FW_FILE].buff_lock))
		android_printf("Failed to destroy firmware buff_lock");

	return 0;
}


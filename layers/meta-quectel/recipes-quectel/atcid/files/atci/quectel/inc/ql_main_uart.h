#ifndef ATCID_MAIN_UART_H
#define ATCID_MAIN_UART_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>  
#include <sys/stat.h>   
#include <fcntl.h>
#include <termios.h>
#include <errno.h>

int ql_init_main_uart(const char*uart_dev,long baud,int databits, int stopbits, int parity);
int ql_open_uart(void);
int ql_set_baud_rate(const char*uart_dev,int baud);
#endif
#include <pthread.h>
#include <termios.h>
#include <unistd.h>

#include "ql_main_uart.h"

#include "atcid.h"
#include "atcid_adaptation.h"
#include "atcid_serial.h"


extern Serial serial;
pthread_mutex_t at_mutex;

long g_baud=115200;
int g_databits=8;
int g_stopbits=1;
int g_parity='n';

int ql_init_main_uart(const char*uart_dev,long baud,int databits, int stopbits, int parity){
    struct termios options;
    int baud_speed;

    int fd = open(uart_dev, O_RDWR | O_NOCTTY | O_NDELAY);
    if(fd<=0){
        LOGATCI(LOG_ERR,"fail to open uart");
        return -1;
    }


    if (tcgetattr(fd, &options) != 0)
	{
		LOGATCI(LOG_ERR,"serial_set_speed 1");
		close(fd);
		return -1;
	}
	switch (baud)
    {
    case 4800:
        baud_speed = B4800;
        break;
    case 9600:
        baud_speed = B9600;
        break;
    case 19200:
        baud_speed = B19200;
        break;
    case 38400:
        baud_speed = B38400;
        break;
    case 57600:
        baud_speed = B57600;
        break;
    case 115200:
        baud_speed = B115200;
        break;
    case 230400:
        baud_speed = B230400;
        break;
    case 460800:
        baud_speed = B460800;
        break;
    case 921600:
        baud_speed = B921600;
        break;
    default:
		g_baud = 115200;		//if input baudrate is invaild restore 115200 
        baud_speed = B115200;
        break;
    }

	options.c_cflag &= ~CSIZE;
	switch (databits) 
	{
		case 5:
			options.c_cflag |= CS5;
			break;
		case 6:
			options.c_cflag |= CS6;
			break;
		case 7:
			options.c_cflag |= CS7;
			break;
		case 8:
			options.c_cflag |= CS8;
			break;
		default:
			LOGATCI(LOG_ERR,"Unsupported data sizen set databit 8");
            options.c_cflag |= CS8;
            break;
	}
	
	switch (parity)
	{
		case 'n':
		case 'N':
			//Clear parity enable
			options.c_cflag &= ~PARENB;
			//Enable parity checking
			//options.c_iflag &= ~INPCK;
			break;
		case 'o':
		case 'O':
			//设置为奇效验
			options.c_cflag |= (PARODD | PARENB);
			//Disnable parity checking
			options.c_iflag |= INPCK;
			break;
		case 'e':
		case 'E':
			//Enable parity
			options.c_cflag |= PARENB;
			//转换为偶效验
			options.c_cflag &= ~PARODD;
			//Disnable parity checking
			options.c_iflag |= INPCK;       
			break;
		case 'S':
		case 's':
			//as no parity
			options.c_cflag &= ~PARENB;
			options.c_cflag &= ~CSTOPB;
			break;
		default:
			LOGATCI(LOG_ERR,"unsupported parityn set parityn N");
			options.c_cflag &= ~PARENB;
            //options.c_iflag &= ~INPCK;
            break;
	}
	
	switch (stopbits)
	{
		case 1:
			options.c_cflag &= ~CSTOPB;
			break;
		case 2:
			options.c_cflag |= CSTOPB;
			break;
		default:
			LOGATCI(LOG_ERR,"unsupported stop bitsn set stopbit 1");
            options.c_cflag &= ~CSTOPB;
			break;
	}
	if (parity != 'n')
	{
		options.c_iflag |= INPCK;
	}
	// options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    // options.c_iflag &= ~(IXON | IXOFF | IXANY);
    // options.c_oflag &= ~OPOST;

	options.c_iflag &= ~(INLCR | IGNCR | ICRNL);
	options.c_oflag &= ~(ONLCR | OCRNL);
	options.c_lflag = ICANON | ECHO;
	options.c_iflag &= ~(IXON | IXOFF | IXANY);
	options.c_iflag |= (INLCR | IGNCR);

	//设置超时15 seconds
	cfmakeraw(&options);
	options.c_cc[VTIME] = 0;
	//Update the options and do it NOW
	options.c_cc[VMIN] = 1; 

    tcflush(fd, TCIOFLUSH);
	cfsetispeed(&options, baud_speed);
	cfsetospeed(&options, baud_speed);
	
	if (tcsetattr(fd, TCSANOW, &options) != 0)
	{
		LOGATCI(LOG_ERR,"serial_set_parity 2");
		close(fd);
		return -1;
	}

	//for debug
	if (tcgetattr(fd, &options) != 0)
	{
		LOGATCI(LOG_ERR,"serial_set_parity 1");
		close(fd);
		return -1;
	}
	tcflush(fd, TCIOFLUSH);
	close(fd);
    return 0;

}

int ql_set_baud_rate(const char*uart_dev,int baud){
    struct termios options;
	int baud_speed;

    int fd = open(uart_dev, O_RDWR | O_NOCTTY | O_NDELAY);
    if(fd<=0){
        LOGATCI(LOG_ERR,"fail to open uart");
        return -1;
    }

	tcgetattr(fd, &options); 

	switch (baud)
    {
    case 4800:
        baud_speed = B4800;
        break;
    case 9600:
        baud_speed = B9600;
        break;
    case 19200:
        baud_speed = B19200;
        break;
    case 38400:
        baud_speed = B38400;
        break;
    case 57600:
        baud_speed = B57600;
        break;
    case 115200:
        baud_speed = B115200;
        break;
    case 230400:
        baud_speed = B230400;
        break;
    case 460800:
        baud_speed = B460800;
        break;
    case 921600:
        baud_speed = B921600;
        break;
    default:
		g_baud = 115200;		//if input baudrate is invaild restore 115200 
        baud_speed = B115200;
        break;
    }
	cfsetispeed(&options, baud_speed);
	cfsetospeed(&options, baud_speed);

	if (tcsetattr(fd, TCSANOW, &options) != 0)
	{
		LOGATCI(LOG_ERR,"serial_set_parity 1");
		close(fd);
		return -1;
	}
	close(fd);
    return 0;
}

int ql_open_uart(void){

	if(ql_init_main_uart("/dev/ttyS2",g_baud,g_databits,g_stopbits,g_parity)!=0){		//modify uart config
		LOGATCI(LOG_INFO, "Init main uart serial port Failed");
	}

    return 1;
}

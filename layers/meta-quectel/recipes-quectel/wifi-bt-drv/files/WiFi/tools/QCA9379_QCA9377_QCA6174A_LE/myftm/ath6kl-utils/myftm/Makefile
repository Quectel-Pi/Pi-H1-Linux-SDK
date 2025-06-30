ifndef CC
CC = gcc
endif

TARGET = myftm

#CFLAGS = -I../libtcmd -I../libathtestcmd -I../libathtestcmd/include -I../libathtestcmd/libtlvutil/common -I$(INCLUDE_LIBNL32_HEADER) -I/home/john/Downloads/ambalink_sdk_3_10/output/amba/host/usr/include 
CFLAGS = -I../libtcmd -I../libathtestcmd -I../libathtestcmd/include -I../libathtestcmd/libtlvutil/common 

CFLAGS += -I../libtcmd

CFLAGS += -DTCMD_LIB_DEBUG \
		  -DCONFIG_FTM_WLAN \
		  -DBOARD_HAS_ATH_WLAN_AR6320 \
		  -DCONFIG_FTM_WLAN_AUTOLOAD \
		  -DLINUX_FSL \
		  -DMDM 
SLIBS = ../libathtestcmd/libathtestcmd.a ../libathtestcmd/libtlvutil/libtlvutil.a

SLIBS += ../libtcmd/libtcmd.a

LIBS = -lnl-genl-3 -lnl-3 -lrt -lz -lpthread

SRCS = myftm_main.c myftm_wlan.c myftm_qtip.c 

OBJS = $(SRCS:.c=.o)

.c.o:
	$(CC) $(CFLAGS)  -c $< -lbsd -o $@ -lbsd -g

all : $(TARGET)

$(TARGET) : $(OBJS)
	$(CC) -o $@ $(OBJS) $(SLIBS) $(LIBS)

clean:
	rm -f $(OBJS) $(TARGET)

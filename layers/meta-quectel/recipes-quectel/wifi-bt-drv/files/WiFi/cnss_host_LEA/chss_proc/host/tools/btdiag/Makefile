# Makefile for Btdiag

#CFLAGS += -g -DDEBUG -DLINUX_X86 -m32
CFLAGS += -g -DDEBUG -DLINUX_X86 
#LDFLAGS += -m32
LDLIBS = -lpthread -lrt

#Here config BT interface, change to n if UART
#Default make for UART interface,
#if USB, make CONFIG_BT_USB=y
CONFIG_BT_USB=y
ifeq ($(CONFIG_BT_USB), y) 
#Here config BT stack, change to bluez if Bluez
CONFIG_BT_STACK=bluez
ifeq ($(CONFIG_BT_STACK),bluetopia)
CFLAGS += -I../../btp_host/btp_host/Bluetopia/include -DBLUETOPIA_STACK
LDFLAGS += -L../../btp_host/btp_host/Bluetopia/lib
LDLIBS += -lSS1BTPS -lBTPSVEND -lBTPSFILE
endif
ifeq ($(CONFIG_BT_STACK),bluez)
CFLAGS += -DBLUEZ_STACK
LDLIBS += -lbluetooth
endif
else
CFLAGS += -DBT_HCI_UART
endif


.PHONY: all clean

Btdiag: Btdiag.o connection.o hciattach_rome.o privateMembers.o uart.o

Btdiag.o: Btdiag.c
	${CC} -c $(CFLAGS) Btdiag.c
connection.o: connection.c connection.h
	${CC} -c $(CFLAGS) connection.c
hciattach_rome.o: hciattach_rome.c hciattach_rome.h
	${CC} -c $(CFLAGS) hciattach_rome.c
privateMembers.o : privateMembers.c privateMembers.h
	${CC} -c $(CFLAGS) privateMembers.c
uart.o : uart.c uart.h
	${CC} -c $(CFLAGS) uart.c
	
clean:
	-$(RM) *.o 
	-$(RM)  Btdiag

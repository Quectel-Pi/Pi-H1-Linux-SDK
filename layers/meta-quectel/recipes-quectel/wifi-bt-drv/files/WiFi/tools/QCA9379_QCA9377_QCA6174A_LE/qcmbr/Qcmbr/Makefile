# Makefile for Qcmbr

CFLAGS += -g -DDEBUG -DLINUX_X86
LDLIBS = -lpthread -lrt

ifeq ($(CONFIG_QCMBR_ENABLE_SOFTMAC), y) 
CFLAGS += -DQCMBR_ENABLE_SOFTMAC
endif
ifeq ($(CONFIG_QCMBR_UART), y) 
CFLAGS += -DQCMBR_UART_QDART
OBJ_UART = uart.o
endif

.PHONY: all clean

Qcmbr: Qcmbr.o diag_pkt_handler.o tlvCmd_if_Qdart.o Socket.o q_os_if.o $(OBJ_UART)

Qcmbr.o : Qcmbr.c Qcmbr.h
	${CC} -c $(CFLAGS) Qcmbr.c
diag_pkt_handler.o : diag_pkt_handler.c
	${CC} -c $(CFLAGS) diag_pkt_handler.c
tlvCmd_if_Qdart.o : tlvCmd_if_Qdart.c
	${CC} -c $(CFLAGS) tlvCmd_if_Qdart.c
Socket.o  : Socket.c Socket.h
	${CC} -c $(CFLAGS) Socket.c
q_os_if.o : q_os_if.c
	${CC} -c $(CFLAGS) q_os_if.c
ifeq ($(CONFIG_QCMBR_UART), y) 
$(OBJ_UART) : uart.c uart.h
	${CC} -c $(CFLAGS) uart.c
endif
clean:
	-$(RM) *.o
	-$(RM) Qcmbr




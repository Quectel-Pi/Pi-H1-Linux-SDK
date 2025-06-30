ifndef CC
	CC = gcc
endif

TARGET = libathtestcmd.a

CFLAGS = -I../include \
	-I include \
	-I libtlvutil \
	-I libtlvutil/common \
	-I ../libtcmd

CFLAGS += -DTCMD_LIB_DEBUG -DLINUX_FSL 

SRCS = athtestcmdlib.c

OBJS = $(SRCS:.c=.o)

.c.o:
	$(CC) $(CFLAGS) -c $< -lbsd -o $@

all : $(TARGET)

$(TARGET) : $(OBJS)
	$(AR) rcs $@ $^

clean:
	rm -f $(OBJS) $(TARGET)

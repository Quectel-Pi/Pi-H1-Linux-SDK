TARGET := atcid

CFLAGS += -DMTK_GPS_FEATURE
CFLAGS += -I/usr/include/libevdev-1.0
PKG_CONFIG  ?= pkg-config

CFLAGS  += $(shell $(PKG_CONFIG) --cflags libevdev)
LDFLAGS += $(shell $(PKG_CONFIG) --libs   libevdev)

INCLUDES := -I. \
         -I./platform \
         -I./quectel/inc


SRCS := \
           ./at_tok.c \
           ./atcid.c \
           ./atcid_serial.c \
           ./atcid_cmd_dispatch.c \
           ./atcid_cust_cmd.c \
           ./atcid_util.c \
           ./platform/atcid_cust_cmd_platform.c \
           ./platform/atcid_common.c \
           ./atcid_universal_ant_cmd.c\
           ./quectel/src/ql_main_uart.c \
           ./quectel/src/ql_at_factory.c \
           ./quectel/src/ql_temp_cmd.c \
           ./quectel/src/ql_qcfg_cmd.c \
           ./quectel/src/ql_common_api.c \

CXXSRCS := \
           ./platform/atcid_adaptation.cpp \

EXTRA_CXXFLAGS := -std=c++11

OBJS := ${SRCS:%.c=%.o}

CXXOBJS := ${CXXSRCS:%.cpp=%.o}

#LDFLAGS = -L. -lz -ldl -lpthread -lstdc++
LDFLAGS2 = -L.
LDFLAGS2 += -levdev

.PHONY: all
all : $(TARGET)

${TARGET}: $(OBJS) $(CXXOBJS)
	$(CC) $(CFLAGS) $(OBJS) $(CXXOBJS) $(LDFLAGS) $(LDFLAGS2) $(LIBS) -o $@

%.o: %.c
	${CC} $(CFLAGS) -c $< -o $@ $(INCLUDES) $(CFLAGS)
	
%.o: %.cpp
	${CXX} $(CXXFLAGS) -c $< -o $@ $(INCLUDES) $(CFLAGS) $(EXTRA_CXXFLAGS)

scripts/audio_loop_sh.h: scripts/audio_loop.sh
	xxd -i $^ $@

scripts/gpio_led_on_sh.h: scripts/gpio_led_on.sh
	xxd -i $^ $@

scripts/gpio_led_off_sh.h: scripts/gpio_led_off.sh
	xxd -i $^ $@

quectel/src/ql_at_factory.c: scripts/audio_loop_sh.h scripts/gpio_led_on_sh.h scripts/gpio_led_off_sh.h

.PHONY: clean
clean:
	$(warning "makefile atcid clean")
	rm -rf $(OBJS) $(CXXOBJS) $(TARGET)

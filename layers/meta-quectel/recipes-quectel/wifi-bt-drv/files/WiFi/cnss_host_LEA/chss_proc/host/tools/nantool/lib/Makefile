INCDIR=-I../inc/ -I. -I/usr/include/libnl3/ -I/usr/include/

OBJ=nan.o
OBJ+=common.o
OBJ+=cpp_bindings.o
OBJ+=ifaceeventhandler.o
OBJ+=nan_ind.o
OBJ+=nan_req.o
OBJ+=nan_rsp.o
OBJ+=wifi_hal.o
OBJ+=list.o
OBJ+=wificonfig.o

CFLAGS += -fPIC -fpermissive

all: libwifi-hal-qcom.so

libwifi-hal-qcom.so: $(OBJ)
	gcc -shared $^ -o $@

%.o: %.cpp
	g++  -c $< $(CFLAGS) $(INCDIR) -static -Wl,--no-as-needed -ldl $(shell pkg-config --cflags --libs libbsd libnl-3.0 libnl-genl-3.0 libssl) -o $@

%.o: %.c
	g++  -c $< $(CFLAGS) $(INCDIR) -static -Wl,--no-as-needed -ldl $(shell pkg-config --cflags --libs libbsd libnl-3.0 libnl-genl-3.0 libssl) -o $@

clean:
	rm *.a;rm *.so;rm *.o

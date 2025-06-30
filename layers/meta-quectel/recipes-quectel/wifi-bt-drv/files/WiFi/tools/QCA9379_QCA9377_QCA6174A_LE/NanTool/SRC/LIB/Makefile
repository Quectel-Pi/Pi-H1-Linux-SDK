INCDIR=-I../inc -I. -I/usr/include/libnl3/ -I/usr/include/
OBJ=nan.o common.o cpp_bindings.o ifaceeventhandler.o nan_ind.o nan_req.o nan_rsp.o wifi_hal.o 

all: libwifi-hal-qcom.so

libwifi-hal-qcom.so: $(OBJ)
	gcc -shared $^ -o $@	

%.o: %.cpp
	g++  -c $< $(CFLAGS) $(INCDIR) -Wl,--no-as-needed -ldl $(pkg-config --cflags --libs libnl-3.0 libnl-genl-3.0) -o $@
clean:
	rm *.a;rm *.so;rm *.o

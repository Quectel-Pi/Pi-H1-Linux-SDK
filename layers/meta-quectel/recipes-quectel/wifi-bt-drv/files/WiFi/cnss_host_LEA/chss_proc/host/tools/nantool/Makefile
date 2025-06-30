INCDIR=-I. -I./inc -I./lib

OBJ=halProxyDaemon.o
OBJ+=nan_test.o

LD=-L./lib

CFLAGS += -DQTI_BSP

all: Nandemotool

Nandemotool: $(OBJ)
	g++ $(CFLAGS) $(INCDIR) $(LD) $^ -lwifi-hal-qcom -lpthread -lstdc++ -Wl,--no-as-needed -ldl -lnl-3 -lnl-genl-3 -lbsd -lssl -o $@

%.o: %.cpp
	g++ -c  $(LD) $(CFLAGS) -Wformat=0 $(INCDIR) $< -o $@

clean:
	rm Nandemotool;rm *.o

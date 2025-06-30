MKDIR   = mkdir -p

OBJ :=  build/btconfig.o
OBJ +=  build/hci_uart.o
OBJ +=  build/hw_rome.o

all: btconfig

build/%.o :src/%.c
	$(MKDIR) build
	$(CC) -Wall -g -c $< -o $@

btconfig: $(OBJ)
	 $(CC) -o $@ $^
	
clean:
	rm -rf build/*.o
	rm -rf btconfig
	



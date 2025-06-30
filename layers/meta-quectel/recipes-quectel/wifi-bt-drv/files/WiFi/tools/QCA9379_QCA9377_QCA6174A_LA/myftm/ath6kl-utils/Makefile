all: myftm-linux

myftm-linux: libtlvutil-m libathtestcmd-m libtcmd-m
	cd myftm && make

libtlvutil-m:
	cd libathtestcmd/libtlvutil && make

libathtestcmd-m:
	cd libathtestcmd && make

libtcmd-m:
	cd libtcmd && make

clean: myftm-linux-clean

myftm-linux-clean: myftm-clean libathtestcmd-clean libtlvutil-clean libtcmd-clean

myftm-clean:
	cd myftm && make clean

libathtestcmd-clean:
	cd libathtestcmd && make clean

libtlvutil-clean:
	cd libathtestcmd/libtlvutil && make clean

libtcmd-clean:
	cd libtcmd && make clean

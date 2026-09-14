scap-y += main.o dynamic_params_table.o fillers_table.o flags_table.o ppm_events.o ppm_fillers.o event_table.o syscall_table64.o ppm_cputime.o ppm_tp.o syscall_ia32_64_map.o
obj-m += scap.o
ccflags-y := -DHAS_DEVNODE_ARG1_CONST -DHAS_FS_MNT_IDMAP -DHAS_0__SANITY -DHAS_CLASS_CREATE_1 -DHAS_ACCESS_OK_2  -DMODULE  -DKBUILD_BASENAME='"dynamic_params_table"' -DKBUILD_MODNAME='"scap"' -D__KBUILD_MODNAME=kmod_scap

KERNELDIR 	?= /lib/modules/$(shell uname -r)/build
TOP 		:= $(shell pwd)

all:
	$(MAKE) -C $(KERNELDIR) M=$(TOP) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(TOP) clean

install: all
	$(MAKE) -C $(KERNELDIR) M=$(TOP) modules_install

ifneq ($(KERNELRELEASE),)
obj-m = quec_ufs_size.o
else
DIR := $(shell pwd)
modules clean modules_install:
	$(MAKE) ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -C $(KDIR) M=$(DIR) $@
endif

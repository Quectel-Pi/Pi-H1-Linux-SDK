ifneq ($(KERNELRELEASE),)
obj-m = yt6801.o
yt6801-objs := fuxi-gmac-common.o fuxi-gmac-desc.o fuxi-gmac-ethtool.o fuxi-gmac-hw.o fuxi-gmac-net.o fuxi-gmac-pci.o fuxi-gmac-phy.o fuxi-efuse.o fuxi-gmac-debugfs.o
#EXTRA_CFLAGS += -DFXGMAC_DEBUG
else
DIR := $(shell pwd)
modules clean modules_install:
	$(MAKE) ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -C $(KDIR) M=$(DIR) $@
endif

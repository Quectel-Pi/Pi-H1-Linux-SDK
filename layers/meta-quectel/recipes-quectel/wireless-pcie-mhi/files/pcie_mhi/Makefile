ifneq ($(KERNELRELEASE),)

obj-m += pcie_mhi.o
pcie_mhi-objs := core/mhi_init.o core/mhi_main.o core/mhi_pm.o core/mhi_boot.o core/mhi_dtr.o controllers/mhi_qti.o
pcie_mhi-objs += devices/mhi_uci.o
pcie_mhi-objs += devices/mhi_netdev_quectel.o

else

DIR := $(shell pwd)
modules clean modules_install:
	$(MAKE) ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -C $(KDIR) M=$(DIR) $@

endif

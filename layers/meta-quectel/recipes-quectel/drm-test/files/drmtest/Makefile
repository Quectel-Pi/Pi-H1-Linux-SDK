ifneq ($(KERNELRELEASE),)
obj-m = vkms_drv.o
vkms_drv-objs = sample-code/drm/driver/vkms/vkms_drv.o
else
DIR = `pwd`
modules:
	$(MAKE) ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -C $(KDIR) M=$(DIR) $@
clean:
	$(MAKE) ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -C $(KDIR) M=$(DIR) $@
modules_install:
	$(MAKE) ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -C $(KDIR) M=$(DIR) $@
endif

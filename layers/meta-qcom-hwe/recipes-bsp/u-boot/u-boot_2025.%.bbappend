# Image files for Uboot
UBOOT_IMAGETYPE ?= "bin"
UBOOT_ELF_SUFFIX ?= "elf"
UBOOT_ELF = "u-boot.elf"
UBOOT_ELF_IMAGE ?= "u-boot-${MACHINE}-${PV}-${PR}.${UBOOT_ELF_SUFFIX}"
UBOOT_ELF_BINARY ?= "u-boot.${UBOOT_ELF_SUFFIX}"

do_configure:append() {
        fsbootargs=""
        cmdline=""
        if [ -n "${QCOM_BOOTIMG_ROOTFS}" ]; then
                fsbootargs="root=${QCOM_BOOTIMG_ROOTFS} rw rootwait"
        fi
        if grep -q "^fsbootargs=" "${S}/board/qualcomm/default.env";then
                sed -i "s#^fsbootargs=.*#fsbootargs=${fsbootargs}#g" ${S}/board/qualcomm/default.env
        else
                echo "fsbootargs=${fsbootargs}" >> ${S}/board/qualcomm/default.env
        fi

        if [ ! -z "${SERIAL_CONSOLES}" ]; then
                tmp="${SERIAL_CONSOLES}"
                console=""
                for entry in $tmp ; do
                        baudrate=`echo $entry | sed 's/\;.*//'`
                        tty=`echo $entry | sed -e 's/^[0-9]*\;//' -e 's/\;.*//'`
                        console="$tty","$baudrate"n8
                done
                cmdline="console=$console"
        fi

        if [ -n "${KERNEL_CMDLINE_EXTRA}" ]; then
                cmdline="$cmdline ${KERNEL_CMDLINE_EXTRA}"
        fi

        if grep -q "^bootargs=" "${S}/board/qualcomm/default.env";then
                sed -i "s#^bootargs=.*#bootargs=${cmdline}#g" ${S}/board/qualcomm/default.env
        else
                echo "bootargs=${cmdline}" >> ${S}/board/qualcomm/default.env
        fi
}

do_deploy:append() {
        cp u-boot-${MACHINE}-${PV}-${PR}.${UBOOT_ELF_SUFFIX} u-boot-${MACHINE}-${PV}-${PR}-stripped.${UBOOT_ELF_SUFFIX}
}

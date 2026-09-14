DESCRIPTION = "TOSHIBA tc956x firmware"

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/${LICENSE};md5=0835ade698e0bcf8506ecda2f7b4f302"

SRCREV = "f9b0e1bc0f7c3dfc74ad1a46a87efa56885b9288"

SRC_URI  =  "git://github.com/TC956X/TC9564_Firmware;branch=industrial_limited_tested;protocol=https"

S = "${WORKDIR}/git"
PACKAGE_ARCH = "${MACHINE_ARCH}"

# Place firmware binary in the package.
FILES:${PN} += "${base_libdir}/firmware/TC956X_Firmware_PCIeBridge.bin"
FILES:${PN} += "${base_libdir}/firmware/LICENSE"

QPS615_BIN_FIRMWARE_PATH = "${D}${base_libdir}/firmware/"
# Install the /lib/firmware directory on target device.
# Then install binary into that directory.
do_install() {
    install -d ${QPS615_BIN_FIRMWARE_PATH}
    install -m 0644 ${S}/Bin/TC956X_Firmware_PCIeBridge.bin -D ${QPS615_BIN_FIRMWARE_PATH}
    install -m 0644 ${S}/Bin/LICENSE ${QPS615_BIN_FIRMWARE_PATH}
}

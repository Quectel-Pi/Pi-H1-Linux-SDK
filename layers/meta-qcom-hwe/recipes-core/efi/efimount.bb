LICENSE = "BSD-3-Clause-Clear"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/BSD-3-Clause-Clear;md5=7a434440b651f4a472ca93716d01033a"

DESCRIPTION = "Mount EFI partition on /boot since the firmware is not yet providing \
the needed EFI variables for systemd-gpt-auto-generator to work."
LICENSE = "BSD-3-Clause-Clear"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/BSD-3-Clause-Clear;md5=7a434440b651f4a472ca93716d01033a"

SRC_URI = " \
    file://boot.mount \
    file://mount_efi.sh \
    file://efi-mount-fail.service \
"

do_install () {
    install -d ${D}${systemd_unitdir}/system/local-fs.target.wants
    install -m 0644  ${WORKDIR}/boot.mount ${D}${systemd_unitdir}/system/boot.mount
    ln -sf ${systemd_unitdir}/system/boot.mount ${D}${systemd_unitdir}/system/local-fs.target.wants/boot.mount

    install -m 0644  ${WORKDIR}/efi-mount-fail.service ${D}${systemd_unitdir}/system/efi-mount-fail.service

    install -d ${D}${bindir}
    install -m 0755 ${WORKDIR}/mount_efi.sh ${D}${bindir}/mount_efi.sh
}

FILES:${PN} += "${systemd_unitdir}/* ${bindir}/mount_efi.sh"

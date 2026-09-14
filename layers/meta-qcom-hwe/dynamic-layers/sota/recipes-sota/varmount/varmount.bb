LICENSE = "BSD-3-Clause-Clear"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/BSD-3-Clause-Clear;md5=7a434440b651f4a472ca93716d01033a"

DESCRIPTION = "Bind Mount stateroot var on /var"
LICENSE = "BSD-3-Clause-Clear"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/BSD-3-Clause-Clear;md5=7a434440b651f4a472ca93716d01033a"

SRC_URI = " \
    file://var.mount \
"

do_install () {
    install -d ${D}${systemd_unitdir}/system/local-fs.target.wants
    install -m 0644  ${WORKDIR}/var.mount ${D}${systemd_unitdir}/system/var.mount
    ln -sf ${systemd_unitdir}/system/var.mount ${D}${systemd_unitdir}/system/local-fs.target.wants/var.mount
}

FILES:${PN} += "${systemd_unitdir}/*"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += "file://ota \
          "

PACKAGES += " \
    initramfs-module-ota \
    "

do_install:append() {
    install -d ${D}/init.d
    # ota
    install -m 0755 ${WORKDIR}/ota ${D}/init.d/92-ota
}

SUMMARY:initramfs-module-ota = "initramfs support for locating and mounting the root partition"
RDEPENDS:initramfs-module-ota = "${PN}-base"
FILES:initramfs-module-ota = "/init.d/92-ota"
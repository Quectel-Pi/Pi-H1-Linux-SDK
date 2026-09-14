FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += "file://0001-hciattach-add-support-for-qualcomm-chip.patch"


do_install:append() {
    install -d ${D}${bindir}
    install -m 755 ${B}/tools/hciattach ${D}/${bindir}/qcom-hciattach
}
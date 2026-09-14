SUMMARY = "Udev rules for dmabuf heaps"
DESCRIPTION = "This recipe installs udev rules to set file permissions for dmabuf heaps."

LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"

SRC_URI = "file://dmaheap.rules"

S = "${WORKDIR}"

do_install:append() {
	install -m 0644 ${WORKDIR}/dmaheap.rules -D ${D}${sysconfdir}/udev/rules.d/dmaheap.rules
}

FILES_${PN} = "${sysconfdir}/*"

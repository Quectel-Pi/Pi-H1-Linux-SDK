SUMMARY = "qpi 配置工具"
LICENSE = "CLOSED"
LIC_FILES_CHKSUM = ""

SRC_URI = "file://qpi-config \
          file://qpi-config.ini \
          file://qpi.dtso"

#inherit deploy

S = "${WORKDIR}"

do_install() {
    install -d ${D}${bindir}
    install -m 755 ${S}/qpi-config ${D}${bindir}/

    install -d ${D}/etc/qpi-config
    install -m 0644 ${S}/qpi-config.ini ${D}/etc/qpi-config/
    install -m 0644 ${S}/qpi.dtso       ${D}/etc/qpi-config/
}

FILES_${PN} = "/usr/bin/qpi-config /etc/qpi-config/qpi-config.ini /etc/qpi-config/qpi.dtso"
INSANE_SKIP:${PN} += "already-stripped"

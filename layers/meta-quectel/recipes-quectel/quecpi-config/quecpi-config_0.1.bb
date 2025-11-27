SUMMARY = "quecpi 配置工具"
LICENSE = "CLOSED"
LIC_FILES_CHKSUM = ""

SRC_URI = "file://quecpi_config \
          file://quecpi_config.ini \
          file://quecpi.dtso"

#inherit deploy

S = "${WORKDIR}"

do_install() {
    install -d ${D}${bindir}
    install -m 755 ${S}/quecpi_config ${D}${bindir}/

    install -d ${D}/etc/quecpi_config
    install -m 0644 ${S}/quecpi_config.ini ${D}/etc/quecpi_config/
    install -m 0644 ${S}/quecpi.dtso       ${D}/etc/quecpi_config/
}

FILES_${PN} = "/usr/bin/quecpi_config /etc/quecpi_config/quecpi_config.ini /etc/quecpi_config/quecpi.dtso"
INSANE_SKIP:${PN} += "already-stripped"

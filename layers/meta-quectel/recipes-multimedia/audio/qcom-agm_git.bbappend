FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"
FILESEXTRAPATHS:prepend := "${TOPDIR}/../prebuild/audio-profile:"

SRC_URI += " \
    file://0001-AGM-override-sd_line_idx-for-Primary-MI2S-to-SD0.patch \
    file://etc/backend_conf.xml \
"

# 用 prebuild/audio-profile 的 backend_conf.xml 覆盖原生 AGM 配置
do_install:append () {
    install -m 0644 ${WORKDIR}/etc/backend_conf.xml ${D}/etc/backend_conf.xml
}

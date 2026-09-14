FILESEXTRAPATHS:prepend := "${TOPDIR}/../prebuild/audio-profile:"

SRC_URI += " \
    file://etc/acdbdata/qcm6490_idp/acdb_cal.acdb \
    file://etc/acdbdata/qcm6490_idp/workspaceFileXml.qwsp \
"

# 用 prebuild/audio-profile 的 ACDB 校准数据覆盖原生 qcom-acdbdata 版本
do_install:append:qcm6490 () {
    install -m 0644 ${WORKDIR}/etc/acdbdata/qcm6490_idp/acdb_cal.acdb \
        ${D}${sysconfdir}/acdbdata/qcm6490_idp/acdb_cal.acdb
    install -m 0644 ${WORKDIR}/etc/acdbdata/qcm6490_idp/workspaceFileXml.qwsp \
        ${D}${sysconfdir}/acdbdata/qcm6490_idp/workspaceFileXml.qwsp
}

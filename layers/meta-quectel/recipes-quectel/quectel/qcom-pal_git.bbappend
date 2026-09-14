FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
FILESEXTRAPATHS:prepend := "${TOPDIR}/../prebuild/audio-profile:"

SRC_URI += " \
    file://0001-pal-fix-eld-index.patch \
    file://0002-pal-dp-endpoint-nonfatal.patch \
    file://etc/usecaseKvManager.xml \
    file://etc/mixer_paths_qcm6490_idp.xml \
    file://etc/resourcemanager_qcm6490_idp.xml \
"

# 用 prebuild/audio-profile 的 PAL 配置覆盖原生 /etc 配置。
# PAL 实际加载小写 qcm6490_idp 版本；同时把原生大写 QCM6490_IDP
# 版本也替换为相同内容，保证无论按哪个文件名查找都是 prebuild 版本。
do_install:append () {
    install -m 0644 ${WORKDIR}/etc/usecaseKvManager.xml ${D}/etc/usecaseKvManager.xml
    install -m 0644 ${WORKDIR}/etc/mixer_paths_qcm6490_idp.xml ${D}/etc/mixer_paths_qcm6490_idp.xml
    install -m 0644 ${WORKDIR}/etc/resourcemanager_qcm6490_idp.xml ${D}/etc/resourcemanager_qcm6490_idp.xml
    install -m 0644 ${WORKDIR}/etc/mixer_paths_qcm6490_idp.xml ${D}/etc/mixer_paths_QCM6490_IDP.xml
    install -m 0644 ${WORKDIR}/etc/resourcemanager_qcm6490_idp.xml ${D}/etc/resourcemanager_QCM6490_IDP.xml
}

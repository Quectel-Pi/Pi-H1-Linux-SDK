# 指定压缩包路径
PREBUILT_ARCHIVE ?= "${TOPDIR}/../prebuilt/prebuilt-${PN}-${PV}.tar.gz"
PREBUILT_EXTRACT_DIR = "${WORKDIR}/prebuilt"

# 解压预编译包
do_unpack() {
    bbnote "Unpacking prebuilt archive: ${PREBUILT_ARCHIVE}"
    mkdir -p ${PREBUILT_EXTRACT_DIR}
    tar -xzf ${PREBUILT_ARCHIVE} -C ${PREBUILT_EXTRACT_DIR}
}

# 安装解压出的 image 和 license 到打包目录
do_install:prepend() {
    install -d ${D}
    bbnote "PREBUILT_EXTRACT_DIR = ${PREBUILT_EXTRACT_DIR}"
    bbnote "Copying from: ${PREBUILT_EXTRACT_DIR}/image to ${D}"
    cp -a ${PREBUILT_EXTRACT_DIR}/prebuilt-${PN}-${PV}/image/* ${D}/

    install -d ${D}${datadir}/licenses/${PN}
    cp -a ${PREBUILT_EXTRACT_DIR}/prebuilt-${PN}-${PV}/license-destdir/* ${D}${datadir}/
}

# 禁用 configure 和 compile 和 patch 阶段
do_configure[noexec] = "1"
do_patch[noexec] = "1"
do_compile[noexec] = "1"


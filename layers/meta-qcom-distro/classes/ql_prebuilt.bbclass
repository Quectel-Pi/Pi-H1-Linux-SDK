

PREBUILT_TAR_PATH = "${DL_DIR}/../prebuilt/prebuilt-${BP}.tar.gz"
PREBUILT_EXTRACT_DIR = "${WORKDIR}/prebuilt-extracted"

# 跳过 do_fetch、do_unpack、do_patch
do_fetch[noexec] = "1"
do_unpack[noexec] = "1"
do_patch[noexec] = "1"
do_configure[noexec] = "1"
do_compile[noexec] = "1"

# 解压 prebuilt tar 到工作区
do_unpack_prebuilt() {
    if [ ! -f "${PREBUILT_TAR_PATH}" ]; then
        bbfatal "Prebuilt file ${PREBUILT_TAR_PATH} not found!"
    fi

    bbnote "Extracting prebuilt package: ${PREBUILT_TAR_PATH}"
    mkdir -p ${PREBUILT_EXTRACT_DIR}
    tar -xzf ${PREBUILT_TAR_PATH} -C ${PREBUILT_EXTRACT_DIR}
}
addtask unpack_prebuilt before do_install after do_compile

# 安装解压出来的 image 和 license-destdir
do_install() {
    install -d ${D}/opt/atci
    cp -a ${PREBUILT_EXTRACT_DIR}/image/* ${D}/opt/atci/ || true

    install -d ${D}/usr/share/licenses/${PN}
    cp -a ${PREBUILT_EXTRACT_DIR}/license-destdir/* ${D}/usr/share/licenses/${PN}/ || true
}

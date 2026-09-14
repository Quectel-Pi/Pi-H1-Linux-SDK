DESCRIPTION = "A suite of web applications for inspecting and understanding \
your TensorFlow runs and graphs."
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://tensorboard-2.16.2.dist-info/LICENSE;md5=6767c3dee46f5a6b27902ea046d2389d"

SRC_URI = " \
    https://files.pythonhosted.org/packages/3a/d0/b97889ffa769e2d1fdebb632084d5e8b53fc299d43a537acee7ec0c021a3/tensorboard-2.16.2-py3-none-any.whl \
"


do_unpack[depends] += "python3-pip-native:do_populate_sysroot"

SRC_URI[md5sum] = "beb9a3fd5a6490048da95ea0a84d4240"
SRC_URI[sha256sum] = "9f2b4e7dad86667615c0e5cd072f1ea8403fc032a299f0072d6f74855775cc45"

RDEPENDS:${PN} += "python3 \
           python3-core \
           python3-numpy \
           python3-protobuf \
           python3-grpcio \
           python3-werkzeug \
           python3-six \
           python3-markdown \
           python3-absl \
           python3-requests \
"

inherit python3native

do_unpack () {
    echo "Installing pip package"
    ${STAGING_BINDIR_NATIVE}/pip3 install --disable-pip-version-check -v \
        -t ${S} --no-cache-dir --no-deps \
         ${DL_DIR}/tensorboard-2.16.2-py3-none-any.whl

    rm -rf ${S}/bin ${S}/tensorboard-2.16.2.dist-info/direct_url.json
}

do_install () {
    install -d ${D}${PYTHON_SITEPACKAGES_DIR}
    cp -rf ${S}/* ${D}${PYTHON_SITEPACKAGES_DIR}/
}

do_configure[noexec] = "1"
do_compile[noexec] = "1"

FILES:${PN} += "${libdir}/*"

DESCRIPTION = "TensorFlow Keras is an implementation of the Keras API that\
 uses TensorFlow as a backend."
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=175792518e4ac015ab6696d16c4f607e"

SRC_URI = "git://github.com/keras-team/tf-keras.git;branch=r2.16;protocol=https \
           file://0001-customize-for-yocto.patch \
           file://0001-skip-pip-file-check.patch \
          "

SRCREV = "7fb2e8b64dd12a61109abdad341bbf63888a0ce6"
S = "${WORKDIR}/git"

inherit python3native bazel

DEPENDS += " \
    python3-pip-native \
    python3-wheel-native \
    python3-absl-native \
    python3-wrapt-native \
    python3-opt-einsum-native \
    python3-gast-native \
    python3-astunparse-native \
    python3-termcolor-native \
    python3-typing-extensions-native \
    python3-flatbuffers-native \
    python3-mldtypes-native \
    python3-pybind11-native \
    python3-protobuf-native \
    tensorflow-native \
"

do_compile () {
    unset CC
    export TMPDIR="${WORKDIR}"
    export PYTHON_BIN_PATH="${PYTHON}"

    ${BAZEL} build \
        --subcommands --explain=${T}/explain.log \
        --verbose_explanations --verbose_failures \
        --verbose_failures \
        --python_path="${PYTHON}" \
        //tf_keras/tools/pip_package:build_pip_package

    ${S}/bazel-bin/tf_keras/tools/pip_package/build_pip_package \
        ${WORKDIR}/tf_keras_pip
}

do_install () {
    echo "Installing pip package"
    install -d ${D}${PYTHON_SITEPACKAGES_DIR}
    ${STAGING_BINDIR_NATIVE}/pip3 install --disable-pip-version-check -v --no-deps \
        -t ${D}/${PYTHON_SITEPACKAGES_DIR} --no-cache-dir ${WORKDIR}/tf_keras_pip/*.whl

    # Provides module keras as usual
    ln -snrf ${D}/${PYTHON_SITEPACKAGES_DIR}/tf_keras ${D}/${PYTHON_SITEPACKAGES_DIR}/keras

}

FILES:${PN} += "${libdir}/*"

BBCLASSEXTEND = "native"

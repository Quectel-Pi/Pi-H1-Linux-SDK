include tensorflow.inc

SRC_URI += " \
    file://0011-add-yocto-toolchain-to-support-cross-compiling.patch \
    file://0012-fix-build-tensorflow-lite-examples-label_image-label.patch \
    file://0013-label_image-tweak-default-model-location.patch \
    file://0014-label_image.lite-tweak-default-model-location.patch \
    file://0015-CheckFeatureOrDie-use-warning-to-avoid-die.patch \
    file://0016-support-32-bit-x64-and-arm-for-yocto.patch \
    file://BUILD.in \
    file://BUILD.yocto_compiler \
    file://cc_config.bzl.tpl \
    file://yocto_compiler_configure.bzl \
"

SRC_URI += "https://storage.googleapis.com/download.tensorflow.org/models/inception_v3_2016_08_28_frozen.pb.tar.gz;name=model-inv3"
SRC_URI[model-inv3.md5sum] = "a904ddf15593d03c7dd786d552e22d73"
SRC_URI[model-inv3.sha256sum] = "7045b72a954af4dce36346f478610acdccbf149168fa25c78e54e32f0c723d6d"

SRC_URI += "https://storage.googleapis.com/download.tensorflow.org/models/tflite/mobilenet_v1_1.0_224_quant_and_labels.zip;name=model-mobv1"
SRC_URI[model-mobv1.md5sum] = "38ac0c626947875bd311ef96c8baab62"
SRC_URI[model-mobv1.sha256sum] = "2f8054076cf655e1a73778a49bd8fd0306d32b290b7e576dda9574f00f186c0f"

RDEPENDS:${PN} += " \
    python3 \
    python3-core \
    python3-numpy \
"

do_configure:append () {
    mkdir -p ${S}/third_party/toolchains/yocto/
    sed "s#%%CPU%%#${BAZEL_TARGET_CPU}#g" ${WORKDIR}/BUILD.in  > ${S}/third_party/toolchains/yocto/BUILD
    chmod 644 ${S}/third_party/toolchains/yocto/BUILD
    install -m 644 ${WORKDIR}/cc_config.bzl.tpl ${S}/third_party/toolchains/yocto/
    install -m 644 ${WORKDIR}/yocto_compiler_configure.bzl ${S}/third_party/toolchains/yocto/
    install -m 644 ${WORKDIR}/BUILD.yocto_compiler ${S}

    CT_NAME=$(echo ${HOST_PREFIX} | rev | cut -c 2- | rev)
    SED_COMMAND="s#%%CT_NAME%%#${CT_NAME}#g"
    SED_COMMAND="${SED_COMMAND}; s#%%WORKDIR%%#${WORKDIR}#g"
    SED_COMMAND="${SED_COMMAND}; s#%%YOCTO_COMPILER_PATH%%#${BAZEL_OUTPUTBASE_DIR}/external/yocto_compiler#g"

    sed -i "${SED_COMMAND}" ${S}/BUILD.yocto_compiler \
                            ${S}/WORKSPACE \
                            ${S}/configure.py

    ${TF_CONFIG} \
    ./configure
}

TF_TARGET_EXTRA ??= ""

export CUSTOM_BAZEL_FLAGS = " \
    ${TF_ARGS_EXTRA} \
    --jobs=auto \
    -c opt \
    --cpu=${BAZEL_TARGET_CPU} \
    --crosstool_top=@local_config_yocto_compiler//:toolchain \
    --host_crosstool_top=@bazel_tools//tools/cpp:toolchain \
"

do_compile () {
    export CT_NAME=$(echo ${HOST_PREFIX} | rev | cut -c 2- | rev)
    unset CC

    ${BAZEL} build \
        ${CUSTOM_BAZEL_FLAGS} \
        --copt -DTF_LITE_DISABLE_X86_NEON --copt -DMESA_EGL_NO_X11_HEADERS \
        --define tflite_with_xnnpack=false \
        --repo_env=TF_PYTHON_VERSION=3.12 \
        tensorflow/lite:libtensorflowlite.so \
        tensorflow/lite/tools/benchmark:benchmark_model \
        //tensorflow/lite/examples/label_image:label_image \
        ${TF_TARGET_EXTRA}

    # build pip package
    ${S}/tensorflow/lite/tools/pip_package/build_pip_package_with_bazel.sh

}

do_install() {
    install -d ${D}${libdir}
    install -m 644 ${S}/bazel-bin/tensorflow/lite/libtensorflowlite.so \
        ${D}${libdir}

    install -d ${D}${sbindir}
    install -m 755 ${S}/bazel-bin/tensorflow/lite/tools/benchmark/benchmark_model \
        ${D}${sbindir}

    install -m 755 ${S}/bazel-bin/tensorflow/lite/examples/label_image/label_image \
        ${D}${sbindir}/label_image

    install -d ${D}${datadir}/label_image
    install -m 644 ${WORKDIR}/imagenet_slim_labels.txt ${D}${datadir}/label_image
    install -m 644 ${WORKDIR}/inception_v3_2016_08_28_frozen.pb \
        ${D}${datadir}/label_image
    install -m 644 ${S}/tensorflow/examples/label_image/data/grace_hopper.jpg \
        ${D}${datadir}/label_image

    install -m 644 ${WORKDIR}/labels_mobilenet_quant_v1_224.txt ${D}${datadir}/label_image
    install -m 644 ${WORKDIR}/mobilenet_v1_1.0_224_quant.tflite \
        ${D}${datadir}/label_image
    install -m 644 ${S}/tensorflow/lite/examples/label_image/testdata/grace_hopper.bmp \
        ${D}${datadir}/label_image


    #echo "Installing pip package"
    install -d ${D}/${PYTHON_SITEPACKAGES_DIR}
    ${STAGING_BINDIR_NATIVE}/pip3 install --disable-pip-version-check -v \
        -t ${D}/${PYTHON_SITEPACKAGES_DIR} --no-cache-dir --no-deps \
        ${S}/tensorflow/lite/tools/pip_package/gen/tflite_pip/python3/dist/tflite_runtime-${PV}*.whl

}

FILES:${PN} += "${libdir} ${sbindir} ${datadir}/*"
INSANE_SKIP:${PN} += "dev-so \
                      already-stripped \
                     "

SOLIBS = ".so"
FILES_SOLIBSDEV = ""
ALLOW_EMPTY:${PN} = "1"

inherit siteinfo unsupportarch
python __anonymous() {
    if d.getVar("SITEINFO_ENDIANNESS") == 'be':
        msg =  "\nIt failed to use pre-build model to do predict/inference on big-endian platform"
        msg += "\n(such as qemumips), since upstream does not support big-endian very well."
        msg += "\nDetails: https://github.com/tensorflow/tensorflow/issues/16364"
        bb.warn(msg)
}

COMPATIBLE_HOST:arm = "null"

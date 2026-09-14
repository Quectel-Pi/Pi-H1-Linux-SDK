DESCRIPTION = "Bazel build and test tool"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=3b83ef96387f14655fc854ddc3c6bd57"

SRC_URI[md5sum] = "f79f57d82795b591e95f9f7fa0f9a8da"
SRC_URI[sha256sum] = "7bc0c5145c19a56d82a08fce6908c5e1a0e75e4fbfb3b6f12b4deae7f4b38cbc"

SRC_URI = "https://github.com/bazelbuild/bazel/releases/download/${PV}/bazel-${PV}-dist.zip \
           file://0001-HttpDownloader-save-download-tarball-to-distdir.patch \
           file://0001-fix-unzip-command-not-found.patch \
           file://0001-add-Yocto-native-sysroot-dir-to-the-default-Bazel-to.patch \
           file://0001-cutsomize-native-toolchains-for-Yocto.patch \
           file://0001-fix-compile-failure-on-gcc-13.patch \
           file://1f2b3ed4ac717b814d02a0d125f160ddabe78003.patch \
           file://include_stdint.patch \
"

inherit python3native

INHIBIT_SYSROOT_STRIP = "1"

CCACHE_DISABLE = "1"

DEPENDS = "coreutils-native \
           zip-native \
           openjdk-11-native \
          "

S="${WORKDIR}"

inherit bazel-base

EXTRA_BAZEL_ARGS = " \
    --host_javabase=@local_jdk//:jdk \
    --python_path=python3 \
    --distdir=${TS_DL_DIR} \
    ${@oe.utils.conditional("BAZEL_JOBS", "", "", "--jobs=${BAZEL_JOBS}", d )} \
    ${@oe.utils.conditional("BAZEL_JOBS", "", "", "--local_cpu_resources=${BAZEL_JOBS}", d )} \
    ${@oe.utils.conditional("BAZEL_MEM", "", "", "--local_ram_resources=${BAZEL_MEM}", d )} \
"

do_compile[network] = "1"
do_compile () {
    TMPDIR="${TMPDIR}/bazel" \
    VERBOSE=yes \
    EXTRA_BAZEL_ARGS="${EXTRA_BAZEL_ARGS}" \
    ./compile.sh
}

do_install () {
    install -d ${D}${bindir}
    install -m 0755 ${S}/output/bazel ${D}${bindir}
}

# Explicitly disable uninative
UNINATIVE_LOADER = ""

inherit native

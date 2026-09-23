# VA-API driver for the Qualcomm msm_vidc (iris_vpu) video codec.
#
# Upstream: https://github.com/snowf14k3/venus-vaapi-driver (MIT).
# This recipe builds the super617 fork, which carries the QCM6490/QCS6490
# adaptation on branch qcm6490-msm-vidc-adapt: the downstream driver calls
# itself "msm_vidc_driver" instead of "qcom-venus", surfaces may be as small as
# 16x16, surfaces are allocated from a DMA-BUF heap so vaExportSurfaceHandle()
# works (mpv's --hwdec=vaapi requires it), vaSyncSurface drains the decoder when
# the client's own surface pool stalls, and decoder stop/start are paired.
# SRCREV is pinned on purpose: the branch is the tested revision, and a floating
# rev could silently ship a driver without those fixes.
#
# Installed into both dri directories: Yocto's libva searches ${libdir}/dri,
# while the Debian rootfs this SDK also ships uses the multiarch dir
# /usr/lib/aarch64-linux-gnu/dri. Same binary, so the two copies cannot drift.

SUMMARY = "VA-API driver backed by the Qualcomm stateful V4L2 M2M video codec"
HOMEPAGE = "https://github.com/super617/venus-vaapi-driver"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae5ae5eddeebed7d3d3b2f4b475bbfce"

SRC_URI = "git://github.com/super617/venus-vaapi-driver.git;protocol=https;branch=qcm6490-msm-vidc-adapt"
SRCREV = "91aafb019c2a12fb998146ce2bbf2df30b5e940d"
S = "${WORKDIR}/git"

DEPENDS = "libva"
RDEPENDS:${PN} = "libva"

# The decoder is /dev/video32 on this board, so the probe has to scan past it.
PROBE_LIMIT = "64"

# Multiarch libdir of the Debian rootfs this SDK ships next to the Yocto one.
DEBIAN_DRI_DIR = "/usr/lib/aarch64-linux-gnu/dri"

do_compile() {
    ${CC} ${CFLAGS} -fPIC -shared -D_GNU_SOURCE \
        -DVENUS_VAAPI_PROBE_LIMIT=${PROBE_LIMIT} \
        -I${S}/include -I${S}/src \
        -o msm_drv_video.so ${S}/src/*.c ${LDFLAGS} -lva -lpthread
}

do_install() {
    install -d ${D}${libdir}/dri
    install -m 0755 ${B}/msm_drv_video.so ${D}${libdir}/dri/msm_drv_video.so

    # Same binary in the Debian multiarch location. Skip it when the paths
    # already coincide (a multiarch-native Yocto build) so the two installs
    # cannot fight over one file.
    if [ "${DEBIAN_DRI_DIR}" != "${libdir}/dri" ]; then
        install -d ${D}${DEBIAN_DRI_DIR}
        install -m 0755 ${B}/msm_drv_video.so ${D}${DEBIAN_DRI_DIR}/msm_drv_video.so
    fi
}

FILES:${PN} = "${libdir}/dri/msm_drv_video.so ${DEBIAN_DRI_DIR}/msm_drv_video.so"

# A shared library with no SONAME that is only ever dlopen()ed by name.
INSANE_SKIP:${PN} += "dev-so ldflags"

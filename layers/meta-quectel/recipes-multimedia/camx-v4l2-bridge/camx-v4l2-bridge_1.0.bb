SUMMARY = "Camx V4L2 Bridge - bridges cam-server to /dev/videoX"
DESCRIPTION = "Standalone daemon that uses qmmf::recorder::Recorder API \
to bridge Qualcomm cam-server to a v4l2loopback device, enabling \
standard V4L2 applications to access Qualcomm cameras."
SECTION = "multimedia"
LICENSE = "BSD-3-Clause-Clear"
LIC_FILES_CHKSUM = "file://${QCOM_COMMON_LICENSE_DIR}${LICENSE};md5=3771d4920bd6cdb8cbdf1e8344489ee0"

DEPENDS = "qcom-camera-server"

SRC_URI = "file://camx_v4l2_bridge.cpp \
           file://camx-v4l2-bridge@.service \
           file://v4l2loopback-setup.service \
           file://camx-v4l2-pwkick.sh"

S = "${WORKDIR}"

do_compile() {
    ${CXX} ${CXXFLAGS} ${LDFLAGS} -O2 -std=c++17 \
        ${WORKDIR}/camx_v4l2_bridge.cpp \
        -o camx-v4l2-bridge \
        -lqmmf_recorder_client -lqmmf_camera_metadata -lpthread
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 camx-v4l2-bridge ${D}${bindir}/

    # PipeWire/wireplumber kicker: forces spa-v4l2 to re-probe /dev/video10
    # after the bridge has flipped device_caps to pure capture, so the
    # Video/Source node's cached caps and format negotiation are correct on
    # the first camera open from gnome-snapshot.
    install -m 0755 ${WORKDIR}/camx-v4l2-pwkick.sh ${D}${bindir}/

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/camx-v4l2-bridge@.service ${D}${systemd_system_unitdir}/
    install -m 0644 ${WORKDIR}/v4l2loopback-setup.service ${D}${systemd_system_unitdir}/
}

inherit systemd

SYSTEMD_SERVICE:${PN} = "camx-v4l2-bridge@0.service \
                         v4l2loopback-setup.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

FILES:${PN} += "${bindir}/camx-v4l2-bridge \
                ${bindir}/camx-v4l2-pwkick.sh \
                ${systemd_system_unitdir}/camx-v4l2-bridge@.service \
                ${systemd_system_unitdir}/v4l2loopback-setup.service"

RDEPENDS:${PN} += "qcom-camera-server v4l2loopback"

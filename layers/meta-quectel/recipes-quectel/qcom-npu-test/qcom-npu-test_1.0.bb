SUMMARY = "Quectel PI (QCS6490) Qualcomm NPU Graphical Test Tool"
DESCRIPTION = "GTK3 GUI for Qualcomm NPU (Hexagon DSP/HTP) testing via SNPE/QNN SDK. \
Includes platform validation, single-model inference, and throughput benchmarking. \
Wraps snpe-platform-validator, qnn-platform-validator, snpe-net-run, and \
snpe-throughput-net-run already shipped on the device by qcom-snpe-sdk / qcom-qnn-sdk."
LICENSE = "CLOSED"

# Pure data/script package: no compilation.
inherit pkgconfig

# These are runtime deps that the GUI and the wrapped CLI tools rely on.
# The SNPE/QNN binaries themselves come from qcom-snpe-sdk / qcom-qnn-sdk,
# which are already part of the image via packagegroup-qcom-ml.
#
# NOTE: this image (quecpi-image / qcom-multimedia-image) merges a pre-built
# Debian GNOME rootfs via deploy_debian_gnome_rootfs(). GTK3, GObject
# introspection, pygobject and pycairo are therefore provided by the Debian
# side at runtime, not by OE packages. Pulling them into RDEPENDS with their
# OE names (gtk+3 / python3-pygobject / python3-pycairo) would still build,
# but the Debian rootfs overlay already supplies the .so / .typelib files,
# so they are RRECOMMENDS (soft) to avoid expanding the OE dependency graph
# and to tolerate the Debian-provided names.
RRECOMMENDS:${PN} = " \
    gtk+3 \
    python3-pygobject \
    python3-pycairo \
    python3-numpy \
"
# Hard runtime deps that the recipe absolutely cannot function without and
# that OE must be able to resolve to a provider.
RDEPENDS:${PN} = " \
    python3 \
    bash \
"
# NOTE: numpy is needed only by the YOLO post-processing path (NMS/box decode).
# It is a soft (RRECOMMENDS) dep rather than a hard one so that:
#   - pure Yocto builds pull python3-numpy via the OE recipe (meta-python),
#   - Debian-rootfs-overlaid builds tolerate it being provided as a dpkg,
#   - if absent at runtime, the GUI degrades gracefully and shows an
#     install hint rather than failing to launch.
# python3-numpy must NOT be a hard RDEPENDS because mixing the OE recipe name
# with a dpkg-provided name can break rootfs assembly on hybrid images.

SRC_URI = " \
    file://npu_test_gui.py \
    file://npu-test.desktop \
    file://npu-test.svg \
    file://images/bus.jpg \
    file://labels/coco_80_labels_list.txt \
    file://models/yolov7.dlc \
    file://models/yolov7_onnx/model.onnx \
    file://models/yolov7_onnx/model.data \
"

S = "${WORKDIR}"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    # Main GUI script
    install -d ${D}${bindir}
    install -m 0755 ${S}/npu_test_gui.py ${D}${bindir}/qcom-npu-test

    # .desktop entry (appears in GNOME application menu)
    install -d ${D}${datadir}/applications
    install -m 0644 ${S}/npu-test.desktop ${D}${datadir}/applications/

    # Vector icon (scalable + a couple of common sizes via the same svg)
    install -d ${D}${datadir}/icons/hicolor/scalable/apps
    install -m 0644 ${S}/npu-test.svg ${D}${datadir}/icons/hicolor/scalable/apps/

    # Default test image + COCO labels (so the GUI is usable out of the box)
    install -d ${D}/usr/share/qcom-npu-test/images
    install -d ${D}/usr/share/qcom-npu-test/labels
    install -m 0644 ${S}/images/bus.jpg ${D}/usr/share/qcom-npu-test/images/
    install -m 0644 ${S}/labels/coco_80_labels_list.txt ${D}/usr/share/qcom-npu-test/labels/

    # Pre-bundled YOLOv7 models (ready-to-run out of the box)
    # - yolov7.dlc: SNPE container, run via snpe-net-run
    # - yolov7_onnx/: QNN ONNX model + external weights, run via qnn-net-run
    install -d ${D}/usr/share/qcom-npu-test/models
    install -m 0644 ${S}/models/yolov7.dlc ${D}/usr/share/qcom-npu-test/models/
    install -d ${D}/usr/share/qcom-npu-test/models/yolov7_onnx
    install -m 0644 ${S}/models/yolov7_onnx/model.onnx ${D}/usr/share/qcom-npu-test/models/yolov7_onnx/
    install -m 0644 ${S}/models/yolov7_onnx/model.data ${D}/usr/share/qcom-npu-test/models/yolov7_onnx/
}

FILES:${PN} = " \
    ${bindir}/qcom-npu-test \
    ${datadir}/applications/npu-test.desktop \
    ${datadir}/icons/hicolor/scalable/apps/npu-test.svg \
    /usr/share/qcom-npu-test/ \
"

INSANE_SKIP:${PN} += "file-rdeps"

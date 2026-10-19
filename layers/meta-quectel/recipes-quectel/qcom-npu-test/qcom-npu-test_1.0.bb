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
    file://npu-daemon \
    file://npu_daemon.cpp \
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

    # 常驻 SNPE 会话 (npu_daemon.cpp 的产物, 已用板上 g++ 编好随包安装)
    # 为什么常驻: snpe-net-run 每点一次都要 进程启动 + 13.6MB DLC 解析 +
    # HTP 会话建立 ≈ 280ms, 而推理本身只要 10~13ms. 常驻进程把这段固定开销
    # 摊掉后, 点击端到端从 ~310ms 降到 ~55ms.
    # 重新编译 (板上, 无需交叉工具链):
    #   g++ -O2 -std=c++11 -I<SNPE 头文件目录> npu_daemon.cpp -L/usr/lib -lSNPE \
    #       -o npu-daemon
    # 头文件来自 qcom-snpe-sdk ($STAGING_INCDIR/SNPE), 板上没有, 所以这里
    # 直接装预编译产物; 若以后要在 recipe 内编译, 去掉 do_compile[noexec] 并
    # DEPENDS += "qcom-snpe-sdk" 即可 (版本需与镜像里的 libSNPE.so 一致).
    install -m 0755 ${S}/npu-daemon ${D}${bindir}/npu-daemon

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
    #   已用 `snpe-net-run --enable_init_cache` 烤进 HTP init cache (对应 GUI 的
    #   --set_output_tensors="yolov7 boxes,scores,class_idx"), 所以 6.9MB → 13.6MB.
    #   没有 cache 时每次调用都要重做 graph prepare (~2.2s, 推理本身才 10ms);
    #   换了 SNPE 版本或输出张量后 cache 自动失效并回退到重新 prepare, 不会报错.
    #   重新生成: 对原始 dlc 跑 snpe-net-run --use_dsp --enable_init_cache,
    #             并带上 GUI 用的 --set_output_tensors="yolov7 boxes,scores,class_idx"
    #   注意该命令会把文件改成 640 root:root, 装回包里要 chmod 644, 否则 GUI 用户读不了.
    # - yolov7_onnx/: QNN ONNX model + external weights, run via qnn-net-run
    install -d ${D}/usr/share/qcom-npu-test/models
    install -m 0644 ${S}/models/yolov7.dlc ${D}/usr/share/qcom-npu-test/models/
    install -d ${D}/usr/share/qcom-npu-test/models/yolov7_onnx
    install -m 0644 ${S}/models/yolov7_onnx/model.onnx ${D}/usr/share/qcom-npu-test/models/yolov7_onnx/
    install -m 0644 ${S}/models/yolov7_onnx/model.data ${D}/usr/share/qcom-npu-test/models/yolov7_onnx/
}

FILES:${PN} = " \
    ${bindir}/qcom-npu-test \
    ${bindir}/npu-daemon \
    ${datadir}/applications/npu-test.desktop \
    ${datadir}/icons/hicolor/scalable/apps/npu-test.svg \
    /usr/share/qcom-npu-test/ \
"

INSANE_SKIP:${PN} += "file-rdeps"

SUMMARY = "Qwen3.5-0.8B resident runner, server and tooling (int8 ONNX on the CPU)"
DESCRIPTION = "Board-side resident runner for a Qwen3_5ForConditionalGeneration decode \
graph.  This is not a genie model: 18 of the 24 layers are gated-DeltaNet with \nrecurrent state, which genie cannot express, so the graph is exported to ONNX and \ndriven directly. \\
\\
Two backends share one model loop: \\
  * ort -- qwen08b_i8.onnx, int8 weights with fp32 activations, onnxruntime on \
    the CPU.  Default.  Logits cosine 0.9925 against an fp32 run, 139 ms/token \
    at 4 threads (7.2 tok/s) on this board.  The prompt is pre-filled by \
    qwen08b_pf64_i8.onnx, the same model in 64-token blocks: 947 ms per block \
    instead of 132 ms per token, so a 100 token prompt costs 1.9 s and not \
    13.2 s. \\
  * htp -- kvc.bin, a W8A16 context binary on the Hexagon V68 NPU, 126 ms of \
    graph time per token.  Kept for measurement: 16-bit per-tensor activations \
    over 1621 tensors reach only cosine 0.9688, which does not keep the top-1 \
    token, so the generated text is noise.  qwen35ort.py and README.md carry \
    the measurements; there is no local fix, the error is spread over the whole \
    graph. \\
\\
npu-chat only offers the model when /run/qwen35.ok exists; the launcher writes \
that marker after its self-test, and reports the accuracy status separately."
LICENSE = "CLOSED"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# Python bindings for the ORT backend.  The image's python3 is the Debian 3.13
# from prebuild/debian-gnome-rootfs (rsync'd over the Yocto rootfs), so this is a
# manylinux wheel unpacked into Debian's dist-packages rather than a Yocto
# python3-onnxruntime recipe (that one builds ONNX Runtime from source).
# The wheel is self-contained: at import time it needs numpy and packaging
# (both already in the Debian rootfs) but not flatbuffers/protobuf, which are
# only used by the onnxruntime.quantization and transformers helpers.
ORT_WHEEL = "onnxruntime-1.31.0-cp313-cp313-manylinux_2_28_aarch64.whl"
ORT_WHEEL_URL = "https://files.pythonhosted.org/packages/8a/d0/3677fe93ec0fa3c637744aa4c3ae6ef89a93ee229cd3c5157820f267c7bd"

# Which artifacts the firmware carries.  Default: the int8 ORT decode graph, the
# 64-token prefill graph and the fp16 embedding table both backends share.  An
# HTP image sets QWEN35_MODEL_FILES = "kvc.bin emb08_f16.npy" instead.
QWEN35_MODEL_FILES ??= "qwen08b_i8.onnx qwen08b_pf64_i8.onnx emb08_f16.npy"
QWEN35_MODEL_IN_IMAGE ??= "1"

# Where those artifacts come from.  They are build products (1.9 GB of ONNX and
# numpy), not source, so they are not in this layer and not in git: the recipe
# fetches them from the GitHub release, verifies each file against the sha256
# below, and installs them from ${WORKDIR}/qwen35-model.  DL_DIR keeps them, so
# a rebuild does not download them again.
#
# An offline build (or a tree that already has the files) points this at a
# directory and nothing changes but the fetcher:
#     QWEN35_MODEL_URL = "file:///data/qwen35-model"
# QWEN35_MODEL_IN_IMAGE = "0" skips the weights entirely (runner-only image).
#
# The default tag is `latest`, which is a moving target by nature: the sha256
# per file below is what actually pins the build, and a re-upload that changes
# one byte fails do_fetch instead of silently rebuilding an image.  Point the
# tag at an immutable release (QWEN35_MODEL_TAG = "v1.0" and so on) when the
# firmware has to be reproducible from the tag alone.
QWEN35_MODEL_TAG ??= "latest"
QWEN35_MODEL_URL ??= "https://github.com/super617/Qwen3.5-for-qcs6490/releases/download/${QWEN35_MODEL_TAG}"

# label:file:sha256.  The label is what the fetcher keys SRC_URI[...sha256sum]
# off, so it has to be unique and stable; the sha256 is per file, not per tag.
QWEN35_MODEL_ARTIFACTS = " \
    model_i8:qwen08b_i8.onnx:696ea91d873e602bd228c8c926080879db2d72e4c19d1898f1098b422caef23d \
    model_pf64:qwen08b_pf64_i8.onnx:3352a5c05ec2e75756cee48fa1b31e6d1b946404567f839d6b663d9f5a7137ab \
    model_emb:emb08_f16.npy:329c9a43c2a66c03bea93f03eeab9c46c0e310400964b7899c67ae9c69600137 \
    model_kvc:kvc.bin:b3daff34b6a96cb7a0f9977d75aef60e92b230148e23ef10ef07b20adcaa470e \
"

# Text payload only: python, the layout table and the tokenizer. No downloads
# except the int8 ORT wheel; the weights are appended below only when the image
# carries them.
SRC_URI = " \
    file://qwen35-convert.sh \
    file://export_prefill.py \
    file://qwen35-run.sh \
    file://qwen35-run.py \
    file://qwen35rt.py \
    file://qwen35ort.py \
    file://qwen35-rt.py \
    file://qwen35-serve.py \
    file://qwen35-launch.sh \
    file://qwen35-model-install.sh \
    file://qwen35-ready.py \
    file://qwen35.service \
    file://qnnlib.py \
    file://qnnres.cpp \
    file://offload_kv.py \
    file://mklayout.py \
    file://layout.json \
    file://kv_manifest.json \
    file://reference.json \
    file://tokenizer.json.gz;unpack=0 \
    file://calib_gen.py \
    file://prep_native.py \
    file://README.md \
    ${ORT_WHEEL_URL}/${ORT_WHEEL};name=ortwheel;unpack=0 \
"
SRC_URI[ortwheel.sha256sum] = "ad663106f6eeff3d454f24a786450459d07f30e74863851104fc1b8b3f368127"

# Resolve QWEN35_MODEL_FILES into real SRC_URI entries plus their checksums.  An
# anonymous function so QWEN35_MODEL_FILES / _URL / _IN_IMAGE can be overridden
# from local.conf without the recipe listing a URL it will not fetch; a file the
# table does not know is a parse-time error instead of a 1.9 GB download that
# fails at do_fetch.
#
# setVarFlag, not setVar("SRC_URI[name.sha256sum]"): the datastore has no bracket
# syntax, so setVar() would create a variable whose *name* contains the brackets
# and bb.fetch2's getVarFlag("SRC_URI", "name.sha256sum") would still see nothing
# -- the download then completes and do_fetch fails with "Missing SRC_URI
# checksum" after 1.9 GB.
python __anonymous() {
    if d.getVar("QWEN35_MODEL_IN_IMAGE") != "1":
        return
    base = (d.getVar("QWEN35_MODEL_URL") or "").rstrip("/")
    table = {}
    for row in (d.getVar("QWEN35_MODEL_ARTIFACTS") or "").split():
        label, fname, sha = row.split(":")
        table[fname] = (label, sha)
    uris = []
    for f in (d.getVar("QWEN35_MODEL_FILES") or "").split():
        if f not in table:
            bb.fatal("qwen35-npu: %s is not in QWEN35_MODEL_ARTIFACTS" % f)
        label, sha = table[f]
        uris.append("%s/%s;name=%s;subdir=qwen35-model" % (base, f, label))
        d.setVarFlag("SRC_URI", "%s.sha256sum" % label, sha)
    if not uris:
        bb.fatal("qwen35-npu: QWEN35_MODEL_IN_IMAGE=1 but QWEN35_MODEL_FILES is empty")
    d.appendVar("SRC_URI", " " + " ".join(uris))
}

# bitbake decompresses a .gz source into the workdir on unpack (bb.fetch2
# unpacks .gz to a file with the extension stripped), and the runner wants the
# 3.5 MB gzip stream, not the 12.8 MB json. unpack=0 keeps the file intact.
S = "${WORKDIR}"
do_configure[noexec] = "1"
do_compile[noexec] = "1"

inherit systemd
SYSTEMD_SERVICE:${PN} = "qwen35.service"
# Start at boot so qwen3.5 is the default model in npu-chat. The unit has
# ConditionPathExists on the two weights, so enabling it is harmless before the
# model is installed: it is skipped instead of failing in a restart loop.
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

# The weights are fetched, not carried in the layer, so there is nothing to
# point at: do_install takes them from where do_unpack put them.

do_install() {
    install -d ${D}${datadir}/qwen35-npu/tools
    install -d ${D}${systemd_system_unitdir}

    install -m 0755 ${S}/qwen35-convert.sh ${D}${datadir}/qwen35-npu/tools/
    install -m 0755 ${S}/export_prefill.py ${D}${datadir}/qwen35-npu/tools/
    install -m 0755 ${S}/qwen35-run.sh ${D}${datadir}/qwen35-npu/tools/
    install -m 0755 ${S}/qwen35-run.py ${D}${datadir}/qwen35-npu/tools/
    install -m 0755 ${S}/qwen35rt.py ${D}${datadir}/qwen35-npu/tools/
    install -m 0755 ${S}/qwen35ort.py ${D}${datadir}/qwen35-npu/tools/
    install -m 0755 ${S}/qwen35-rt.py ${D}${datadir}/qwen35-npu/tools/
    install -m 0755 ${S}/qwen35-serve.py ${D}${datadir}/qwen35-npu/tools/
    install -m 0755 ${S}/qwen35-launch.sh ${D}${datadir}/qwen35-npu/tools/
    install -m 0755 ${S}/qwen35-model-install.sh ${D}${datadir}/qwen35-npu/tools/
    install -m 0755 ${S}/qwen35-ready.py ${D}${datadir}/qwen35-npu/tools/
    install -m 0755 ${S}/offload_kv.py ${D}${datadir}/qwen35-npu/tools/
    install -m 0755 ${S}/mklayout.py ${D}${datadir}/qwen35-npu/tools/
    install -m 0644 ${S}/qnnlib.py ${D}${datadir}/qwen35-npu/tools/
    install -m 0644 ${S}/qnnres.cpp ${D}${datadir}/qwen35-npu/tools/
    install -m 0644 ${S}/calib_gen.py ${D}${datadir}/qwen35-npu/tools/
    install -m 0644 ${S}/prep_native.py ${D}${datadir}/qwen35-npu/tools/
    install -m 0644 ${S}/layout.json ${D}${datadir}/qwen35-npu/tools/
    install -m 0644 ${S}/kv_manifest.json ${D}${datadir}/qwen35-npu/tools/
    install -m 0644 ${S}/reference.json ${D}${datadir}/qwen35-npu/tools/
    install -m 0644 ${S}/tokenizer.json.gz ${D}${datadir}/qwen35-npu/tools/
    install -m 0644 ${S}/README.md ${D}${datadir}/qwen35-npu/

    install -m 0644 ${S}/qwen35.service ${D}${systemd_system_unitdir}/

    # onnxruntime for the ORT backend, into the Debian rootfs's dist-packages:
    # the image's python3 is the Debian 3.13 overlaid by quecpi-image, whose
    # sys.path has /usr/lib/python3/dist-packages and not the Yocto
    # site-packages directory.
    install -d ${D}/usr/lib/python3/dist-packages
    python3 -m zipfile -e ${WORKDIR}/${ORT_WHEEL} \
        ${D}/usr/lib/python3/dist-packages

    if [ "${QWEN35_MODEL_IN_IMAGE}" = "1" ]; then
        install -d ${D}${localstatedir}/lib/qwen35
        for f in ${QWEN35_MODEL_FILES}; do
            src="${WORKDIR}/qwen35-model/$f"
            if [ ! -f "$src" ]; then
                bbfatal "qwen35-npu: $src is missing. The fetcher should have \
put it there from ${QWEN35_MODEL_URL} (QWEN35_MODEL_FILES=${QWEN35_MODEL_FILES}); \
see files/README.md, 'Model artifacts'."
            fi
            install -m 0644 "$src" ${D}${localstatedir}/lib/qwen35/$f
        done
    fi
}

# qwen35rt.py / qwen35ort.py / qwen35-serve.py need numpy for the tensor views,
# gzip for the tokenizer and http.server (which oe-core ships in netclient) for
# the API.  onnxruntime itself comes from this recipe (the ORT_WHEEL above), not
# from a Yocto recipe; python3-ctypes is only needed by the HTP backend.
RDEPENDS:${PN} = " \
    python3 \
    python3-numpy \
    python3-ctypes \
    python3-json \
    python3-compression \
    python3-netclient \
    python3-netserver \
"

FILES:${PN} = " \
    ${datadir}/qwen35-npu/ \
    ${systemd_system_unitdir}/qwen35.service \
    /usr/lib/python3/dist-packages/onnxruntime* \
"
# The weights are huge, so they get their own package: qwen35-npu-model is what
# quecpi-image installs to carry Qwen3.5 in the firmware (and what makes
# qwen3.5 npu-chat's default). Builds that only want the runner skip it.
#
# The context is already dense, so compressing the deb only costs build time
# and gains nothing.
DPKG_BUILDCMD = "dpkg-deb -Znone"
PACKAGES =+ "${PN}-model"
ALLOW_EMPTY:${PN}-model = "1"
FILES:${PN}-model = "${localstatedir}/lib/qwen35/"
RDEPENDS:${PN}-model = "${PN}"

# The onnxruntime wheel is a prebuilt manylinux binary: its three .so files ship
# stripped, so the already-stripped QA check (a fatal error by default) fires on
# them.  There are no sources to rebuild them from here, and the debug symbols
# are not in the wheel anyway, so skip that one check for this package.
INSANE_SKIP:${PN} += "already-stripped"
# Nothing in SRC_URI is a patch or a local file that skips the fetcher, and the
# four model URLs are on github.com, so bitbake's default network/checksum rules
# apply unchanged; the sstate signature covers the four sha256 values above, so
# a re-tagged release with the same bytes does not rebuild the image.
# IMAGE_INSTALL carries qwen35-npu-model (see quecpi-image), so the firmware
# ships the weights; the empty package is the runner-only build.

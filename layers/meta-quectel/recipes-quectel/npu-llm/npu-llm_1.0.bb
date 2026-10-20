SUMMARY = "LLM inference on the QCS6490 Hexagon NPU (V68) with a GTK4 chat UI"
DESCRIPTION = "Pre-compiled QAIRT context binaries (Qwen2.5-0.5B, Llama-3.2-1B) for \
Hexagon V68, the genie-t2t-run runtime and its QNN HTP V68 libraries, plus the npu-chat \
GUI and npu-llm CLI launchers. \
GGUF models cannot reach the NPU on this SoC: ggml-hexagon ships HTP v73/v75/v79/v81 \
backends only, so this recipe is the NPU path."
HOMEPAGE = "https://aihub.qualcomm.com/"
# Third-party Qualcomm redistributables (radxa ModelScope bundles, built with QAIRT
# 2.40.1) plus our own scripts. Check the redistributable terms before shipping a
# commercial image; the launchers are trivial to drop if you only want the models.
LICENSE = "CLOSED"

# Everything this recipe ships lives in files/; build-deb.sh packs the same
# payload as a deb for adb install.
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

QWEN = "https://modelscope.cn/models/radxa/Qwen2.5-0.5B-v68/resolve/master"
LLAMA = "https://modelscope.cn/models/radxa/Llama3.2-1B-4096-qairt-v68/resolve/master"

# ~2.3 GB of payload. Cached in DL_DIR after the first build; on a host without a
# route to modelscope.cn, drop the files into ${DL_DIR} under the names given by
# downloadfilename and bitbake will use the cache.
SRC_URI = " \
    file://npu-llm;subdir=npu-llm-files \
    file://npu-chat;subdir=npu-llm-files \
    file://npu-chat.desktop;subdir=npu-llm-files \
    file://npu-chat.svg;subdir=npu-llm-files \
    file://README.Debian;subdir=npu-llm-files \
    ${QWEN}/genie-t2t-run;name=rt_genie;subdir=npu-llm/runtime;downloadfilename=npu-llm-genie-t2t-run \
    ${QWEN}/libGenie.so;name=rt_genie_so;subdir=npu-llm/runtime;downloadfilename=npu-llm-libGenie.so \
    ${QWEN}/libQnnHtp.so;name=rt_htp;subdir=npu-llm/runtime;downloadfilename=npu-llm-libQnnHtp.so \
    ${QWEN}/libQnnHtpNetRunExtensions.so;name=rt_htp_ext;subdir=npu-llm/runtime;downloadfilename=npu-llm-libQnnHtpNetRunExtensions.so \
    ${QWEN}/libQnnHtpV68Skel.so;name=rt_skel;subdir=npu-llm/runtime;downloadfilename=npu-llm-libQnnHtpV68Skel.so \
    ${QWEN}/libQnnHtpV68Stub.so;name=rt_stub;subdir=npu-llm/runtime;downloadfilename=npu-llm-libQnnHtpV68Stub.so \
    ${QWEN}/libQnnSystem.so;name=rt_sys;subdir=npu-llm/runtime;downloadfilename=npu-llm-libQnnSystem.so \
    ${QWEN}/htp_backend_ext_config.json;name=qwen_ext;subdir=npu-llm/qwen2.5-0.5b;downloadfilename=npu-llm-qwen-htp_backend_ext_config.json \
    ${QWEN}/qwen2.5-0.5B-1k-htp.json;name=qwen_cfg;subdir=npu-llm/qwen2.5-0.5b;downloadfilename=npu-llm-qwen-model-config.json \
    ${QWEN}/tokenizer.json;name=qwen_tok;subdir=npu-llm/qwen2.5-0.5b;downloadfilename=npu-llm-qwen-tokenizer.json \
    ${QWEN}/models/qwen2.5-0.5b-1k.bin;name=qwen_bin;subdir=npu-llm/qwen2.5-0.5b/models;downloadfilename=npu-llm-qwen-model.bin \
    ${LLAMA}/htp-model-config-llama32-1b-gqa.json;name=llama_cfg;subdir=npu-llm/llama3.2-1b;downloadfilename=npu-llm-llama-model-config.json \
    ${LLAMA}/htp_backend_ext_config.json;name=llama_ext;subdir=npu-llm/llama3.2-1b;downloadfilename=npu-llm-llama-htp_backend_ext_config.json \
    ${LLAMA}/tokenizer.json;name=llama_tok;subdir=npu-llm/llama3.2-1b;downloadfilename=npu-llm-llama-tokenizer.json \
    ${LLAMA}/models/weight_sharing_model_1_of_1.serialized.bin;name=llama_bin;subdir=npu-llm/llama3.2-1b/models;downloadfilename=npu-llm-llama-model.bin \
"

SRC_URI[rt_genie.sha256sum]     = "db6ba32ae2040cf25ca10c8b2fff5a79cad00e08a6382628e9ae4f2ee8bbac21"
SRC_URI[rt_genie_so.sha256sum]  = "468972fb7949c1d8d71fe5b26a684c0d1745f6f45d93168a56c03702fe02ad1a"
SRC_URI[rt_htp.sha256sum]       = "1542866d5210c5190bfb7c2d9e92f5fe8217d0e66daba59309ae831d53b3b3a2"
SRC_URI[rt_htp_ext.sha256sum]   = "c1759ef969d9a021494e29eabbbb01b707961617897d683b442c357cf448606d"
SRC_URI[rt_skel.sha256sum]      = "4760f9173629b9197ef557cbda0e07961c55452bd6376bfa694179adb8214987"
SRC_URI[rt_stub.sha256sum]      = "49496a7d82ff734e4d305abb0ef127a23e4798bcaed5c02c95543b17b148c808"
SRC_URI[rt_sys.sha256sum]       = "5e47bfbc4a89acc58cde324fb6b041b2bc741d21e378b4fe39ccc2eb21e82e77"
SRC_URI[qwen_ext.sha256sum]     = "11553de7b4b70f8f3ee699bcdb385c6a7a74366ec4728b54bac345dc79b369f8"
SRC_URI[qwen_cfg.sha256sum]     = "41258863a22da4a20c9493dec476cea2ab4c12df0f528f0a35ba0994b92d2e64"
SRC_URI[qwen_tok.sha256sum]     = "a8506e7111b80c6d8635951a02eab0f4e1a8e4e5772da83846579e97b16f61bf"
SRC_URI[qwen_bin.sha256sum]     = "ca1a502fc11a1c4ef4e0d0adb1c31318fbd0b9dc017a0e8292e530fa7077bf39"
SRC_URI[llama_cfg.sha256sum]    = "92d7bacf93c0cea7de0f35380cd5c27da121744db292fcd5127358f61a619a16"
SRC_URI[llama_ext.sha256sum]    = "11553de7b4b70f8f3ee699bcdb385c6a7a74366ec4728b54bac345dc79b369f8"
SRC_URI[llama_tok.sha256sum]    = "79e3e522635f3171300913bb421464a87de6222182a0570b9b2ccba2a964b2b4"
SRC_URI[llama_bin.sha256sum]    = "d4a284cb9a40d1178daee0c8da2f6a6144d5835b32f5f05e659c3de918aff586"

S = "${WORKDIR}"

# Pure data/script package: nothing is compiled.
do_configure[noexec] = "1"
do_compile[noexec] = "1"

# The QNN libraries are private to this package -- they are the same sonames as
# the ones already in /usr/lib from qcom-qnn-sdk, so they go to ${libdir}/npu-llm
# and the launchers point LD_LIBRARY_PATH at it.
#
# Files unpack under their SRC_URI downloadfilename, which is deliberately not
# the upstream basename (two bundles ship a tokenizer.json and an
# htp_backend_ext_config.json each, and DL_DIR is flat). Every install below
# therefore renames back to what the launchers and the dialog JSON expect:
# model-config.json, tokenizer.json, htp_backend_ext_config.json and the
# models/*.bin paths that model-config.json references relatively.
do_install() {
    install -d ${D}${bindir} ${D}${libdir}/npu-llm ${D}${datadir}/npu-llm

    install -m 0755 ${S}/npu-llm-files/npu-llm ${D}${bindir}/npu-llm
    install -m 0755 ${S}/npu-llm-files/npu-chat ${D}${bindir}/npu-chat

    install -m 0755 ${S}/npu-llm/runtime/npu-llm-genie-t2t-run \
        ${D}${libdir}/npu-llm/genie-t2t-run
    for so in libGenie.so libQnnHtp.so libQnnHtpNetRunExtensions.so \
              libQnnHtpV68Skel.so libQnnHtpV68Stub.so libQnnSystem.so; do
        install -m 0644 ${S}/npu-llm/runtime/npu-llm-$so ${D}${libdir}/npu-llm/$so
    done

    Q="${S}/npu-llm/qwen2.5-0.5b"
    install -d ${D}${datadir}/npu-llm/qwen2.5-0.5b/models
    install -m 0644 $Q/npu-llm-qwen-model-config.json \
        ${D}${datadir}/npu-llm/qwen2.5-0.5b/model-config.json
    install -m 0644 $Q/npu-llm-qwen-tokenizer.json \
        ${D}${datadir}/npu-llm/qwen2.5-0.5b/tokenizer.json
    install -m 0644 $Q/npu-llm-qwen-htp_backend_ext_config.json \
        ${D}${datadir}/npu-llm/qwen2.5-0.5b/htp_backend_ext_config.json
    install -m 0644 $Q/models/npu-llm-qwen-model.bin \
        ${D}${datadir}/npu-llm/qwen2.5-0.5b/models/qwen2.5-0.5b-1k.bin

    L="${S}/npu-llm/llama3.2-1b"
    install -d ${D}${datadir}/npu-llm/llama3.2-1b/models
    install -m 0644 $L/npu-llm-llama-model-config.json \
        ${D}${datadir}/npu-llm/llama3.2-1b/model-config.json
    install -m 0644 $L/npu-llm-llama-tokenizer.json \
        ${D}${datadir}/npu-llm/llama3.2-1b/tokenizer.json
    install -m 0644 $L/npu-llm-llama-htp_backend_ext_config.json \
        ${D}${datadir}/npu-llm/llama3.2-1b/htp_backend_ext_config.json
    install -m 0644 $L/models/npu-llm-llama-model.bin \
        ${D}${datadir}/npu-llm/llama3.2-1b/models/weight_sharing_model_1_of_1.serialized.bin

    # The Llama bundle ships greedy sampling (top-k 1), which loops on this 1B
    # model and burns the whole 512-token budget; 40 matches the Qwen bundle and
    # stops at EOS in ~6 s. Same edit in build-deb.sh next to this recipe.
    sed -i 's/"top-k": 1,/"top-k": 40,/' ${D}${datadir}/npu-llm/llama3.2-1b/model-config.json
    grep -q '"top-k": 40,' ${D}${datadir}/npu-llm/llama3.2-1b/model-config.json || \
        bbfatal "llama top-k patch did not apply -- upstream bundle changed?"

    install -d ${D}${docdir}/${PN}
    install -m 0644 ${S}/npu-llm-files/README.Debian ${D}${docdir}/${PN}/README.Debian

    install -d ${D}${datadir}/applications ${D}${datadir}/icons/hicolor/scalable/apps
    install -m 0644 ${S}/npu-llm-files/npu-chat.desktop ${D}${datadir}/applications/npu-chat.desktop
    install -m 0644 ${S}/npu-llm-files/npu-chat.svg \
        ${D}${datadir}/icons/hicolor/scalable/apps/npu-chat.svg
}

FILES:${PN} = " \
    ${bindir}/npu-llm \
    ${bindir}/npu-chat \
    ${libdir}/npu-llm/ \
    ${datadir}/npu-llm/ \
    ${datadir}/applications/npu-chat.desktop \
    ${datadir}/icons/hicolor/scalable/apps/npu-chat.svg \
    ${docdir}/${PN}/ \
"

# npu-chat needs GTK4 + PyGObject, which the Debian GNOME rootfs already ships.
# They are NOT pulled in as RDEPENDS: this image is a Debian rootfs overlay and
# dragging the OE gtk4 stack in would duplicate it. Add gtk4 + python3-pygobject
# yourself if you build a pure-OE image.
RDEPENDS:${PN} = "python3 coreutils"

# Prebuilt arm64 binaries: skip the QA that inspects them.
#
# ldflags: libQnnHtpV68Skel.so is not an ARM object at all -- it is
# "ELF 32-bit LSB shared object, QUALCOMM DSP6", the Hexagon side of the HTP
# backend, built by Qualcomm's own toolchain and hashed with SysV HASH. The
# check wants .gnu.hash from our LDFLAGS, which no cross-linker here can give
# it, so it can only ever fail. The ARM binaries in the package all have
# .gnu.hash and pass; skipping covers those too, but none of them are ours to
# relink either.
INSANE_SKIP:${PN} += "already-stripped file-rdeps arch ldflags"

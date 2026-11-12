SUMMARY = "LLM inference on the QCS6490 Hexagon NPU (V68) with a GTK4 chat UI"
DESCRIPTION = "Pre-compiled QAIRT context binaries (Qwen2.5-0.5B, Llama-3.2-1B) for \
Hexagon V68, the genie-t2t-run runtime and its QNN HTP V68 libraries, plus the npu-chat \
GUI and npu-llm CLI launchers. Offline speech is included too: SenseVoice ASR and \
Matcha-TTS (Chinese and English) through sherpa-onnx, so the chat can be driven \
by a microphone and read the answer out loud without a network. \
GGUF models cannot reach the NPU on this SoC: ggml-hexagon ships HTP v73/v75/v79/v81 \
backends only, so this recipe is the NPU path."
HOMEPAGE = "https://aihub.qualcomm.com/"
# Third-party Qualcomm redistributables (radxa ModelScope bundles, built with QAIRT
# 2.40.1) plus our own scripts. Check the redistributable terms before shipping a
# commercial image; the launchers are trivial to drop if you only want the models.
LICENSE = "CLOSED"

# do_install unpacks the sherpa-onnx wheels with the build host's python.
inherit python3native

# Everything this recipe ships lives in files/; build-deb.sh packs the same
# payload as a deb for adb install.
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

QWEN = "https://modelscope.cn/models/radxa/Qwen2.5-0.5B-v68/resolve/master"
LLAMA = "https://modelscope.cn/models/radxa/Llama3.2-1B-4096-qairt-v68/resolve/master"
# Voice models come from the Hugging Face mirror: ModelScope has no anonymous
# HTTP API for these repos (every endpoint 404s) and the github release is
# ~140 kB/s.
HF = "https://hf-mirror.com"
SENSE = "csukuangfj/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17"
# Chinese+English speech out: Matcha-TTS (single female voice, MIT) plus the
# 16 kHz universal vocoder it requires. The VITS that used to be here
# (vits-zh-hf-fanchen-C) has a character lexicon with no Latin entry at all, so
# English answers were dropped word by word and came out as silence. The vocoder
# only exists on the sherpa-onnx github release, not on HF.
MATCHA = "csukuangfj/matcha-icefall-zh-en"
VOCOS = "https://github.com/k2-fsa/sherpa-onnx/releases/download/vocoder-models"

# ~2.7 GB of payload. Cached in DL_DIR after the first build; on a host without a
# route to modelscope.cn, drop the files into ${DL_DIR} under the names given by
# downloadfilename and bitbake will use the cache.
#
# sherpa-onnx 1.13.8 is a compiled extension, so its wheel is per-ABI: the
# module is _sherpa_onnx.cpython-XXX-aarch64-linux-gnu.so and an interpreter
# refuses a foreign tag outright (the import fails with "No module named
# 'sherpa_onnx.lib._sherpa_onnx'" while the file is sitting right there -- which
# is exactly how the voice features died on the Ubuntu 26.04 rootfs).  The ABI
# follows the OS dimension like the qwen35 wheels do (QUECTEL_OS_DIM, see
# ../onnxruntime/qwen35-wheel.inc):
#   LINUX -> cp312 (OE rootfs python 3.12)
#   DEBIAN -> cp313 (rootfs python 3.13)
#   UBUNTU -> cp314 (rootfs python 3.14)
# downloadfilename carries the ABI on purpose: DL_DIR is flat and the three
# wheels unpack to the same paths.
NPU_LLM_SHERPA_CP312 = "https://files.pythonhosted.org/packages/1f/5f/22e1571146b2c0b581657562d5919b69da13ed93dc70049f42d45fe9e84c/sherpa_onnx-1.13.8-cp312-cp312-manylinux2014_aarch64.manylinux_2_17_aarch64.whl;name=whl_sherpa312;subdir=npu-llm/voice;downloadfilename=npu-llm-sherpa-onnx-cp312.whl"
NPU_LLM_SHERPA_CP313 = "https://files.pythonhosted.org/packages/13/e3/115476caa9f80cd5f55e4e7b777a6c2d01a133d10f8fae574ba869fc48c3/sherpa_onnx-1.13.8-cp313-cp313-manylinux2014_aarch64.manylinux_2_17_aarch64.whl;name=whl_sherpa313;subdir=npu-llm/voice;downloadfilename=npu-llm-sherpa-onnx-cp313.whl"
NPU_LLM_SHERPA_CP314 = "https://files.pythonhosted.org/packages/4f/9a/51821829b5735b3d7ce607992a62fe93d5f7215cda324c11191c77d49b9b/sherpa_onnx-1.13.8-cp314-cp314-manylinux2014_aarch64.manylinux_2_17_aarch64.whl;name=whl_sherpa314;subdir=npu-llm/voice;downloadfilename=npu-llm-sherpa-onnx-cp314.whl"
NPU_LLM_SHERPA_WHEEL ??= "${NPU_LLM_SHERPA_CP313}"
NPU_LLM_SHERPA_WHEEL = "${@ d.getVar('NPU_LLM_SHERPA_CP312') if d.getVar('QUECTEL_OS_DIM') == 'LINUX' else (d.getVar('NPU_LLM_SHERPA_CP314') if d.getVar('QUECTEL_OS_DIM') == 'UBUNTU' else d.getVar('NPU_LLM_SHERPA_CP313')) }"
# The minor version of the *rootfs* interpreter the selected wheel was built for.
# Only used for the build-time guard in do_install, so a mismatch fails here
# instead of turning into "No module named 'sherpa_onnx.lib._sherpa_onnx'" on the
# board (the .so is present; the interpreter just skips a foreign ABI tag).
# '.cpython-314-' -- how the tag appears inside the module name (_sherpa_onnx.
# cpython-314-aarch64-linux-gnu.so).  NOT the cpXXX wheel tag: the two differ by
# the 'cpython' vs 'cp' spelling, and a guard that looks for the wrong one just
# never matches.
NPU_LLM_SHERPA_ABI = "${QUECTEL_PY_EXT}"

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
    ${HF}/${SENSE}/resolve/main/model.int8.onnx;name=asr_model;subdir=npu-llm/voice;downloadfilename=npu-llm-asr-model.int8.onnx \
    ${HF}/${SENSE}/resolve/main/tokens.txt;name=asr_tok;subdir=npu-llm/voice;downloadfilename=npu-llm-asr-tokens.txt \
    ${HF}/${MATCHA}/resolve/main/model-steps-3.onnx;name=tts_model;subdir=npu-llm/voice;downloadfilename=npu-llm-tts-matcha.onnx \
    ${HF}/${MATCHA}/resolve/main/lexicon.txt;name=tts_lex;subdir=npu-llm/voice;downloadfilename=npu-llm-tts-matcha-lexicon.txt \
    ${HF}/${MATCHA}/resolve/main/tokens.txt;name=tts_tok;subdir=npu-llm/voice;downloadfilename=npu-llm-tts-matcha-tokens.txt \
    ${VOCOS}/vocos-16khz-univ.onnx;name=tts_voc;subdir=npu-llm/voice;downloadfilename=npu-llm-tts-vocoder.onnx \
    ${NPU_LLM_SHERPA_WHEEL} \
    https://files.pythonhosted.org/packages/9c/72/02227b14cd3fb3d504a8814ad144ed321a53331948c893821e02f4abb1d7/sherpa_onnx_core-1.13.8-py3-none-manylinux2014_aarch64.whl;name=whl_core;subdir=npu-llm/voice;downloadfilename=npu-llm-sherpa-onnx-core.whl \
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
SRC_URI[asr_model.sha256sum]    = "c71f0ce00bec95b07744e116345e33d8cbbe08cef896382cf907bf4b51a2cd51"
SRC_URI[asr_tok.sha256sum]      = "f449eb28dc567533d7fa59be34e2abca8784f771850c78a47fb731a31429a1dc"
SRC_URI[tts_model.sha256sum]    = "524286bf6cf11be74329ae1c682ac69e34d6860c2ea9fd1290319d561540b16a"
SRC_URI[tts_lex.sha256sum]      = "599efdcdaff4df2a123ce988c2cb90abcd59b919ef609ce36eb587d68b7ca2c0"
SRC_URI[tts_tok.sha256sum]      = "77fee8e5e5dd96b3547119e6159292c648e99e065c54c97777722e3ce710b9a4"
SRC_URI[tts_voc.sha256sum]      = "b599142a1fb8ff03de3e84ac35ff537c619e56f4267a6fe894851a42844acf9e"
# sherpa-onnx wheel checksums, one per ABI.  The name=whl_sherpa312/313/314 tags
# in the URLs above are what routes the hash: only the entry NPU_LLM_SHERPA_WHEEL
# selects lands in SRC_URI, so only that entry's varflag is consulted, and a
# URL/hash mismatch fails do_fetch naming the file.
SRC_URI[whl_sherpa312.sha256sum] = "2520b1e7b779a29a493a8cec893612218dfad7304786bf3f64df2ff7ac6c3302"
SRC_URI[whl_sherpa313.sha256sum] = "916ec38242e779ee3a99f7b161bd1bf3b02a7fad34cb073d091021c0469e0b58"
SRC_URI[whl_sherpa314.sha256sum] = "45f16c9fabc3d7ce31ba9722ff8196627ddcaa5ffd02413ad0664ad1952ba419"
SRC_URI[whl_core.sha256sum]     = "a51a03d55c376c32e15dd63ea852236042ba91a5cd73e253bbf7cf21f6582ba9"

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

    # Offline voice: the SenseVoice/Matcha-TTS models plus the sherpa-onnx
    # python bindings. npu-chat appends ${libdir}/npu-llm/voice to sys.path, so
    # the bindings sit next to the models instead of in site-packages.
    install -d ${D}${datadir}/npu-llm/voice/asr ${D}${datadir}/npu-llm/voice/tts
    install -m 0644 ${S}/npu-llm/voice/npu-llm-asr-model.int8.onnx \
        ${D}${datadir}/npu-llm/voice/asr/model.int8.onnx
    install -m 0644 ${S}/npu-llm/voice/npu-llm-asr-tokens.txt \
        ${D}${datadir}/npu-llm/voice/asr/tokens.txt
    install -m 0644 ${S}/npu-llm/voice/npu-llm-tts-matcha.onnx \
        ${D}${datadir}/npu-llm/voice/tts/model-steps-3.onnx
    install -m 0644 ${S}/npu-llm/voice/npu-llm-tts-vocoder.onnx \
        ${D}${datadir}/npu-llm/voice/tts/vocos-16khz-univ.onnx
    install -m 0644 ${S}/npu-llm/voice/npu-llm-tts-matcha-lexicon.txt \
        ${D}${datadir}/npu-llm/voice/tts/lexicon.txt
    install -m 0644 ${S}/npu-llm/voice/npu-llm-tts-matcha-tokens.txt \
        ${D}${datadir}/npu-llm/voice/tts/tokens.txt

    install -d ${D}${libdir}/npu-llm/voice
    for whl in ${S}/npu-llm/voice/npu-llm-sherpa-onnx*.whl; do
        python3 -m zipfile -e "$whl" ${D}${libdir}/npu-llm/voice
    done
    rm -rf ${D}${libdir}/npu-llm/voice/*.dist-info
    # Exactly one sherpa wheel is in SRC_URI (NPU_LLM_SHERPA_WHEEL), so exactly
    # one ABI-tagged extension must be left.  npu-chat puts this directory on
    # sys.path itself (SHERPA = /usr/lib/npu-llm/voice), which is why the tree
    # lives here and not in dist-packages -- and why a foreign ABI tag cannot be
    # rescued by any packaging path: python only ever looks for the tag matching
    # the running interpreter.
    if [ ! -f ${D}${libdir}/npu-llm/voice/sherpa_onnx/lib/_sherpa_onnx.${NPU_LLM_SHERPA_ABI}-aarch64-linux-gnu.so ]; then
        bbfatal "npu-llm: no _sherpa_onnx.${NPU_LLM_SHERPA_ABI} in the unpacked sherpa wheel -- npu-chat's voice features would fail to import on the rootfs python ${QUECTEL_PY_MINOR}"
    fi
    # The wheel carries exactly one ABI-tagged extension, so a second one here
    # means an earlier build's tree was reused (same live-loop hazard as the
    # wheels installed into dist-packages -- see qwen35_prune_foreign_abi in
    # ../onnxruntime/qwen35-wheel.inc, which this recipe cannot require because
    # it is a native C program that does not use the wheel helpers).  One line,
    # same rule: keep only the tag this dimension builds for.
    for so in ${D}${libdir}/npu-llm/voice/sherpa_onnx/lib/*.cpython-*-aarch64-linux-gnu.so; do
        [ -e "$so" ] || continue
        case "$so" in
            *".${NPU_LLM_SHERPA_ABI}-aarch64-linux-gnu.so") ;;
            *) rm -f "$so" ;;
        esac
    done

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
# yourself if you build a pure-OE image. coreutils is for the launcher's stdbuf.
# 识图 borrows the rootfs' gst-launch-1.0 (v4l2src -> fdsink) and gdk-pixbuf
# (JPEG encode) the same way: both come with the GNOME desktop, and the UI
# degrades with a clear message rather than crashing if one is missing.
#
# numpy is conditional, and the reason is a defect that shipped:
# npu-chat's transcribe() converts the recorded PCM with
# `np.frombuffer(...)` (files/npu-chat, in transcribe()), so numpy IS required --
# but only *a* numpy for the rootfs interpreter, and the three variants get it
# from different places (conf/quectel-os-dim.inc):
#
#   DEBIAN 13  dist-packages/numpy 2.2.4 for python 3.13  -> the rootfs has it
#   UBUNTU 26  dist-packages/ was seeded without numpy      -> python3-numpy-qwen35
#   LINUX      OE rootfs, python 3.12 site-packages         -> poky's python3-numpy
#
# A hard `python3-numpy` here resolved to poky's recipe on EVERY variant.  On the
# Debian/Ubuntu overlays that copy lands in /usr/lib/python3.12/site-packages,
# which neither board interpreter puts on sys.path -- so it never satisfied
# `import numpy` (npu-chat's speech path kept failing) while still costing 35 MB,
# and because the overlay rsync is --inplace without --delete, the files stayed
# behind as orphans with no dpkg owner (verified on a built rootfs:
# /usr/lib/python3.12/site-packages/numpy is 1.26.4 from OE while
# `dpkg -S` finds nothing and var/lib/dpkg/status has no python3-numpy entry).
# Same split as ../qwen35-npu/qwen35-npu_1.0.bb; keep the two in step.
RDEPENDS:${PN} = " \
    python3 \
    coreutils \
    ${@ 'python3-numpy' if d.getVar('QUECTEL_OS_DIM') == 'LINUX' else ''} \
    ${@ 'python3-numpy-qwen35' if 'numpy' not in (d.getVar('QUECTEL_ROOTFS_PYHAS') or '').split() and d.getVar('QUECTEL_OS_DIM') != 'LINUX' else ''} \
"
# The Matcha TTS phonemises Latin with espeak-ng, whose data lives in the
# Debian rootfs (/usr/lib/aarch64-linux-gnu/espeak-ng-data, from the
# espeak-ng-data package) -- same story as gtk4 above: there is no espeak-ng
# recipe in these layers to RDEPEND on, and pulling OE's eSpeak 1.48 in would
# not help. It is not optional: sherpa-onnx aborts with "Please provide data
# dir for this model" instead of raising, so npu-chat requires the directory
# before it offers the speak button and the rest of the app keeps working.

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

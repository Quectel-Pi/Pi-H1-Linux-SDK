#!/usr/bin/env bash
# npu-llm — pre-compiled QAIRT LLM bundles for the QCS6490 (Hexagon V68) NPU.
#
# on QCS6490 the GGUF/llama.cpp NPU path is dead (ggml-hexagon ships v73+ only),
# so the only way onto the Hexagon is a QAIRT context binary (V68) run by
# genie-t2t-run. This script stages those bundles and packs them as a deb.
#
#   ./build-deb.sh                 # download missing files, build the .deb
#   ./build-deb.sh --no-download   # rebuild from the local cache only
#   ./build-deb.sh --output <dir>  # where to put the .deb (default: downloads/)
#
# The payload is ~2.3 GB; the cache lives in downloads/npu-llm-cache/.
# Keep FILES below in sync with layers/meta-quectel/recipes-quectel/npu-llm/npu-llm_1.0.bb

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
TOP="$(cd "$HERE/../../../.." && pwd)"
PAYLOAD="$HERE/files"
OUT_DIR="$TOP/downloads"
CACHE="$TOP/downloads/npu-llm-cache"
DOWNLOAD=1

die() { echo "error: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --no-download) DOWNLOAD=0; shift ;;
        --output)      OUT_DIR="${2:?}"; shift 2 ;;
        -h|--help)     sed -n '2,13p' "$0"; exit 0 ;;
        *) die "unknown option: $1" ;;
    esac
done

for c in dpkg-deb curl sha256sum; do
    command -v "$c" >/dev/null || die "missing $c"
done

VERSION=1.0
QWEN="https://modelscope.cn/models/radxa/Qwen2.5-0.5B-v68/resolve/master"
LLAMA="https://modelscope.cn/models/radxa/Llama3.2-1B-4096-qairt-v68/resolve/master"

# url|sha256|path inside the package
FILES=(
"$QWEN/genie-t2t-run|db6ba32ae2040cf25ca10c8b2fff5a79cad00e08a6382628e9ae4f2ee8bbac21|usr/lib/npu-llm/genie-t2t-run"
"$QWEN/libGenie.so|468972fb7949c1d8d71fe5b26a684c0d1745f6f45d93168a56c03702fe02ad1a|usr/lib/npu-llm/libGenie.so"
"$QWEN/libQnnHtp.so|1542866d5210c5190bfb7c2d9e92f5fe8217d0e66daba59309ae831d53b3b3a2|usr/lib/npu-llm/libQnnHtp.so"
"$QWEN/libQnnHtpNetRunExtensions.so|c1759ef969d9a021494e29eabbbb01b707961617897d683b442c357cf448606d|usr/lib/npu-llm/libQnnHtpNetRunExtensions.so"
"$QWEN/libQnnHtpV68Skel.so|4760f9173629b9197ef557cbda0e07961c55452bd6376bfa694179adb8214987|usr/lib/npu-llm/libQnnHtpV68Skel.so"
"$QWEN/libQnnHtpV68Stub.so|49496a7d82ff734e4d305abb0ef127a23e4798bcaed5c02c95543b17b148c808|usr/lib/npu-llm/libQnnHtpV68Stub.so"
"$QWEN/libQnnSystem.so|5e47bfbc4a89acc58cde324fb6b041b2bc741d21e378b4fe39ccc2eb21e82e77|usr/lib/npu-llm/libQnnSystem.so"
"$QWEN/htp_backend_ext_config.json|11553de7b4b70f8f3ee699bcdb385c6a7a74366ec4728b54bac345dc79b369f8|usr/share/npu-llm/qwen2.5-0.5b/htp_backend_ext_config.json"
"$QWEN/htp_backend_ext_config.json|11553de7b4b70f8f3ee699bcdb385c6a7a74366ec4728b54bac345dc79b369f8|usr/share/npu-llm/llama3.2-1b/htp_backend_ext_config.json"
"$QWEN/qwen2.5-0.5B-1k-htp.json|41258863a22da4a20c9493dec476cea2ab4c12df0f528f0a35ba0994b92d2e64|usr/share/npu-llm/qwen2.5-0.5b/model-config.json"
"$QWEN/tokenizer.json|a8506e7111b80c6d8635951a02eab0f4e1a8e4e5772da83846579e97b16f61bf|usr/share/npu-llm/qwen2.5-0.5b/tokenizer.json"
"$QWEN/models/qwen2.5-0.5b-1k.bin|ca1a502fc11a1c4ef4e0d0adb1c31318fbd0b9dc017a0e8292e530fa7077bf39|usr/share/npu-llm/qwen2.5-0.5b/models/qwen2.5-0.5b-1k.bin"
"$LLAMA/htp-model-config-llama32-1b-gqa.json|92d7bacf93c0cea7de0f35380cd5c27da121744db292fcd5127358f61a619a16|usr/share/npu-llm/llama3.2-1b/model-config.json"
"$LLAMA/tokenizer.json|79e3e522635f3171300913bb421464a87de6222182a0570b9b2ccba2a964b2b4|usr/share/npu-llm/llama3.2-1b/tokenizer.json"
"$LLAMA/models/weight_sharing_model_1_of_1.serialized.bin|d4a284cb9a40d1178daee0c8da2f6a6144d5835b32f5f05e659c3de918aff586|usr/share/npu-llm/llama3.2-1b/models/weight_sharing_model_1_of_1.serialized.bin"
)

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
root="$work/npu-llm_${VERSION}_arm64"
mkdir -p "$CACHE" "$root/DEBIAN" "$root/usr/bin" "$root/usr/share/doc/npu-llm"

# fetch + verify once, then lay out
for entry in "${FILES[@]}"; do
    IFS='|' read -r url sum dest <<<"$entry"
    f="$CACHE/$(printf '%s' "$url" | sha256sum | cut -c1-12)-$(basename "$url")"
    if [ ! -f "$f" ]; then
        [ "$DOWNLOAD" = 1 ] || die "not cached: $url"
        echo "fetch $(basename "$url")"
        curl -fsSL --retry 3 -o "$f.part" "$url" || die "download failed: $url"
        mv "$f.part" "$f"
    fi
    echo "$sum  $f" | sha256sum -c --quiet - || die "checksum mismatch: $url"
    install -D -m 0755 "$f" "$root/$dest"
done
chmod 0644 "$root"/usr/share/npu-llm/*/*.json "$root"/usr/share/npu-llm/*/models/*

# The Llama bundle ships greedy sampling (top-k 1). On this 1B model that loops
# and burns the whole 512-token budget (65 s of repeated lines); 40 is what the
# Qwen bundle uses and it stops at EOS in ~6 s. Same edit in npu-llm_1.0.bb.
llama_cfg="$root/usr/share/npu-llm/llama3.2-1b/model-config.json"
sed -i 's/"top-k": 1,/"top-k": 40,/' "$llama_cfg"
grep -q '"top-k": 40' "$llama_cfg" || die "top-k patch did not apply"

install -m 0755 "$PAYLOAD/npu-llm" "$root/usr/bin/npu-llm"
install -m 0755 "$PAYLOAD/npu-chat" "$root/usr/bin/npu-chat"
install -D -m 0644 "$PAYLOAD/npu-chat.desktop" "$root/usr/share/applications/npu-chat.desktop"
install -D -m 0644 "$PAYLOAD/npu-chat.svg" \
    "$root/usr/share/icons/hicolor/scalable/apps/npu-chat.svg"
install -m 0644 "$PAYLOAD/README.Debian" "$root/usr/share/doc/npu-llm/README.Debian"

size=$(du -sk "$root" | cut -f1)
cat > "$root/DEBIAN/control" <<EOF
Package: npu-llm
Version: ${VERSION}
Architecture: arm64
Maintainer: Quectel PI local build <root@localhost>
Installed-Size: ${size}
Section: misc
Priority: optional
Depends: libc6 (>= 2.39), libstdc++6, libgcc-s1, python3-gi, gir1.2-gtk-4.0
Homepage: https://aihub.qualcomm.com/
Description: LLMs on the QCS6490 Hexagon NPU (V68), plus a chat UI
 Qwen2.5-0.5B and Llama-3.2-1B as QAIRT context binaries, the
 genie-t2t-run runtime, its QNN HTP V68 libraries, and a fullscreen
 GTK4 chat application.
 .
 CLI: npu-llm llama3.2-1b -p "hello"
 GUI: npu-chat
 .
 GGUF models cannot use the NPU on this SoC: ggml-hexagon ships HTP
 v73/v75/v79/v81 backends only. This package is the V68 path.
EOF

mkdir -p "$OUT_DIR"
out="$OUT_DIR/npu-llm_${VERSION}_arm64.deb"
dpkg-deb -Zzstd --root-owner-group -b "$root" "$out" >/dev/null
echo "Built $out ($(du -h "$out" | cut -f1), installed $(du -sh "$root" | cut -f1))"

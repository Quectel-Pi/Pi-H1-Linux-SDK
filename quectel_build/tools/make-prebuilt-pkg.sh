#!/bin/sh
# 从一个 recipe 的 deb 编译产物生成"闭源预编译包"（只含二进制/数据，不含源码）。
# 配合 layers/meta-quectel 里对应的 <recipe>.bbappend 使用：
#   chicdk-kt    -> layers/meta-quectel/recipes-multimedia/camx/chicdk-kt_1.0.qcom.bbappend
#   qca1023-wlan -> layers/meta-quectel/recipes-quectel/wifi-bt-drv/qca1023-wlan_1.0.bbappend
#
# 用法：
#   1) 先用源码版 recipe 编一次：
#        source quectel_build/compile/build.sh
#        buildconfig QSM565DWF QSM565DWFPARL1A01_BP01.001_Linux6.6.38_V01 STD
#        bitbake <PN>
#   2) ./quectel_build/tools/make-prebuilt-pkg.sh <PN> <输出 tar.gz 路径> [deb 路径]
#   3) 把脚本打印的 sha256 填进对应 bbappend 的 SRC_URI[sha256sum]
set -e

PN=$1
OUT=$2
DEB=${3:-}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
[ -n "$PN" ] && [ -n "$OUT" ] || { echo "用法: $0 <PN> <输出 tar.gz 路径> [deb 路径]" >&2; exit 1; }

if [ -z "$DEB" ]; then
    DEB=$(find "$ROOT/build-qcom-wayland/tmp-glibc/deploy/deb" -name "${PN}_*.deb" 2>/dev/null \
          | grep -vE -- '-(dbg|dev|src|staticdev)_' | head -1)
fi
[ -n "$DEB" ] && [ -f "$DEB" ] || { echo "找不到 ${PN} 的 deb，请先用源码版 recipe bitbake ${PN}" >&2; exit 1; }
echo "来源: $DEB"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
# 顶层目录名 = PN，与 bbappend 的 S = "${WORKDIR}/<PN>" 对应
dpkg-deb -x "$DEB" "$TMP/$PN"
# 不放 LICENSE：授权文件由 recipe 的 LIC_FILES_CHKSUM 从 common-licenses 取，
# 塞进包里会变成 FILES 未覆盖的 installed-vs-shipped QA 错误。

mkdir -p "$(dirname "$OUT")"
tar -czf "$OUT" -C "$TMP" "$PN"

echo "生成: $OUT ($(du -h "$OUT" | cut -f1), $(find "$TMP/$PN" -type f | wc -l) 个文件)"
echo "把下面这行加进对应 bbappend 锁定校验（去掉行首 #）："
echo "SRC_URI[sha256sum] = \"$(sha256sum "$OUT" | cut -d' ' -f1)\""

# 闭源发布：用预编译包替换 qca1023-wlan_1.0.bb 的源码编译（wlan.ko + WiFi 固件 + bt 工具 + 服务脚本）。
# 预编译包由 quectel_build/tools/make-prebuilt-pkg.sh 从 bitbake 产物生成，
# layers/meta-quectel/recipes-quectel/wifi-bt-drv/files/WiFi 源码树不再随 SDK 发布。
FILESEXTRAPATHS:prepend := "${THISDIR}/qca1023-wlan:"

SRC_URI = "file://qca1023-wlan_1.0_${SOC_ARCH}.tar.gz"
# 重新生成预编译包后，把脚本打印的 sha256 更新到这里：
SRC_URI[sha256sum] = "b5cc6df3af1e146c708bde33ef783d13e6994035f513fe8a3c44b681220992e1"

# 原 recipe 的 S = ${WORKDIR}（源码树），闭源后改为预编译包根目录
S = "${WORKDIR}/qca1023-wlan"

do_configure[noexec] = "1"

# wlan.ko 已编好，不再调内核 make（原 do_compile 也依赖源码树）
do_compile[noexec] = "1"

do_install() {
    cp -a ${S}/. ${D}/
}

# 预编译包里的二进制已经是 strip 过的，跳过 QA 报错
INSANE_SKIP:${PN} += "already-stripped"

# FILES:${PN} = "/" 等打包规则沿用原 recipe

# 闭源发布：用预编译包替换 meta-qcom-hwe/recipes-multimedia/camx/chicdk-kt_1.0.qcom.bb 的源码编译。
# 预编译包（chicdk-kt/ 目录下）由 quectel_build/tools/make-prebuilt-pkg.sh 从 bitbake 产物生成，
# sources/quectel-src/chicdk-kt 源码不再随 SDK 发布。
FILESEXTRAPATHS:prepend := "${THISDIR}/chicdk-kt:"

SRC_URI = "file://chicdk-kt_1.0_${SOC_ARCH}.tar.gz"
# 重新生成预编译包后，把脚本打印的 sha256 更新到这里：
SRC_URI[sha256sum] = "29ab2c17cc63008a615ce549e222cf2cd661daf8e3ce49bed06063690d901cb1"

S = "${WORKDIR}/chicdk-kt"

# 原 recipe 的解包后置动作（autogen.sh）依赖源码，闭源后必须去掉
do_unpack[postfuncs] := ""

# 解包后直接拷贝，不再走 cmake/autogen
do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    cp -r ${S}/* ${D}/
}

# FILES:${PN}、INSANE_SKIP 等打包规则沿用原 recipe

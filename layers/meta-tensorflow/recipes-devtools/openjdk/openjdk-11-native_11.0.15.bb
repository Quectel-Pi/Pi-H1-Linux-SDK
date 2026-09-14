DESCRIPTION = "Java runtime based upon the OpenJDK Project, the community \
builds using source code from OpenJDK project"
LICENSE = "GPL-2.0-with-classpath-exception"
LIC_FILES_CHKSUM = "file://legal/java.base/LICENSE;md5=3e0b59f8fac05c3c03d4a26bbda13f8f"

SRC_URI[md5sum] = "7a1d931c0454981d85ed0d2949a91b7f"
SRC_URI[sha256sum] = "78e4e52c31600a99bb8050e94017123e447c6683d0f9d7a6c5b0d49c0da6f29a"
SRC_URI = " \
    https://github.com/ojdkbuild/contrib_jdk11u-ci/releases/download/jdk-11.0.15%2B10/jdk-11.0.15-ojdkbuild-linux-x64.zip \
"

S = "${WORKDIR}/jdk-11.0.15-ojdkbuild-linux-x64"

do_patch[noexec] = "1"
do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install () {
	install -d ${D}${libdir}/jvm/openjdk-11-native
	cp -rf ${S}/* ${D}${libdir}/jvm/openjdk-11-native
}

inherit native
INHIBIT_SYSROOT_STRIP = "1"

python __anonymous() {
    if d.getVar("BUILD_ARCH") != "x86_64":
        msg =  "\nThe pre-build openjdk-11-native does not support %s host," % d.getVar("BUILD_ARCH")
        msg += "\nplease use the one in meta-java to replace,"
        msg += "\nadd meta-java to BBLAYERS in conf/bblayers.conf"
        raise bb.parse.SkipPackage(msg)
}

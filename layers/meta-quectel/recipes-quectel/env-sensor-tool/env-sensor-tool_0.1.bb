SUMMARY = "微雪40pin接口环境传感器读取工具"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://main.c \
           file://Makefile"

S = "${WORKDIR}"

DEPENDS += "lgpio"

#LDFLAGS:append = " -Wl,--hash-style=gnu"

do_compile() {
    cd ${S}
    oe_runmake
}

do_install() {
    install -d ${D}${bindir}
    install -m 755 envtest ${D}${bindir}
}

FILES_${PN} = "${bindir}/envtest"

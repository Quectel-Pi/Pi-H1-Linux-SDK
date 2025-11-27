DESCRIPTION = "Provide /usr/bin/gcc symlinks to cross toolchain"
LICENSE = "MIT"
RDEPENDS_${PN} = "gcc"

do_install () {
    install -d ${D}${bindir}
    ln -sf ${TARGET_PREFIX}gcc ${D}${bindir}/cc
    ln -sf ${TARGET_PREFIX}gcc ${D}${bindir}/CC
    ln -sf ${TARGET_PREFIX}gcc ${D}${bindir}/gcc
    ln -sf ${TARGET_PREFIX}g++ ${D}${bindir}/g++
}

FILES_${PN} = "${bindir}/cc ${bindir}/CC ${bindir}/gcc ${bindir}/g++"

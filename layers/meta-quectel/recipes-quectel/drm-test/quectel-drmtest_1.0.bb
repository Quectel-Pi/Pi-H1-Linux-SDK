DESCRIPTION = "Quectel libdrm test"
LICENSE = "CLOSED"

inherit cmake pkgconfig

DEPENDS += "libdrm"
SRC_URI = "file://drmtest"
S = "${WORKDIR}/drmtest"

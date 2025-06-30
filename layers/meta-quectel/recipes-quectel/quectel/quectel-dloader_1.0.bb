DESCRIPTION = "Quectel QDloader"
LICENSE = "CLOSED"

inherit cmake 

SRC_URI = "file://QDloader.tgz"

S = "${WORKDIR}/QDloader"

OECMAKE_C_FLAGS:append = "-Wno-sign-compare -Wno-missing-field-initializers -Wno-unused-parameter"

DESCRIPTION = "Waveshare Environment Sensor_HAT"
LICENSE = "CLOSED"

DEPENDS += "p7zip-native lgpio"
do_unpack[depends] += "p7zip-native:do_populate_sysroot"

SRC_URI = "https://www.waveshare.net/w/upload/b/bc/Environment_Sensor_HAT_Code.7z;name=hat \
          "
SRC_URI[hat.sha256sum] = "99d00bab0fd37efdb10a26359bf01180ee842bb52dddc20a7cc08dfa11f35893"
S = "${WORKDIR}/Environment_Sensor_HAT_Code/c"

do_compile:prepend() {
	sed -i '/^CC\s*=/d' Makefile
}

do_install() {
	install -d ${D}${bindir} 
	cp ${S}/main ${D}${bindir}/${PN}
}

INSANE_SKIP:${PN} += "ldflags"

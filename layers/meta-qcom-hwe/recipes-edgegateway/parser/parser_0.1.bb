SUMMARY = "Sampled Value Parser for edgegateway"
DESCRIPTION = "Application to parse the IEC61850 data, detect anomalies, run protection Unit load with real time latencies and jitter"
LICENSE = "BSD-3-Clause-Clear"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/${LICENSE};md5=7a434440b651f4a472ca93716d01033a"

SRC_URI = "git://git.codelinaro.org/clo/le/platform/vendor/qcom-opensource/edgegateway.git;branch=edgegateway.lnx.1.0;protocol=https"
SRCREV = "${AUTOREV}"
S="${WORKDIR}/git/parserSystem"

CFLAGS += "\
    -I${S}/parser/inc \
    -I${S}/processController/inc \
"

DEPENDS += "dpdk rapidjson paho-mqtt-cpp mosquitto sqlite3"

inherit autotools-brokensep pkgconfig

TARGET_CC_ARCH += "${LDFLAGS}"

do_compile() {
       oe_runmake -C parser
       if [ -d ${S}/processController ]; then
           oe_runmake -C processController
       else
           bbwarn "processController directory not found. Skipping build."
       fi
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/parser/build/parser-shared ${D}${bindir}
    if [ -d ${S}/processController/build ]; then
        install -m 0755 ${S}/processController/build/qcontroller-shared ${D}${bindir}
    else
        bbwarn "processController directory not found. Skipping installation of qcontroller-shared."
    fi
}

PACKAGES = "${PN} ${PN}-dbg"
FILES:${PN} += "${bindir}/parser-shared ${bindir}/qcontroller-shared"
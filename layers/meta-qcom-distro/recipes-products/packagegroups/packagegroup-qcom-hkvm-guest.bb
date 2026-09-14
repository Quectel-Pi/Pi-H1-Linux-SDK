SUMMARY = "Basic programs and scripts"
DESCRIPTION = "Package group to bring up basic utilities for non critical VM"

LICENSE = "BSD-3-Clause-Clear"

PACKAGE_ARCH = "${TUNE_PKGARCH}"

inherit packagegroup

PROVIDES = "${PACKAGES}"

PACKAGES = ' \
    ${PN} \
    packagegroup-hkvm-utils \
'

RDEPENDS:${PN} = "\
    packagegroup-filesystem-utils \
    packagegroup-support-utils \
    packagegroup-hkvm-utils \
    "

RDEPENDS:packagegroup-hkvm-utils = "\
    mosquitto \
    python3-pip \
    python3-pandas \
    python3-requests \
    stress-ng \
    "

SUMMARY = "Google Authentication Library"
HOMEPAGE = "https://github.com/googleapis/google-auth-library-python"
DESCRIPTION = "This library simplifies using Google’s various \
server-to-server authentication mechanisms to access Google APIs."
SECTION = "devel/python"

LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=86d3f3a95c324c9479bd8986968f4327"

inherit pypi

SRC_URI[md5sum] = "2d7fb4328377b94603823e3f029a3e1d"
SRC_URI[sha256sum] = "9e00c76e1bc7b1a7fe80472f1347f3802e4caa1fa53ba1f11b7af3772b484a82"

RDEPENDS:${PN} += " \
    python3-cachetools \
    python3-rsa \
    python3-pyasn1-modules \
"

BBCLASSEXTEND = "native"

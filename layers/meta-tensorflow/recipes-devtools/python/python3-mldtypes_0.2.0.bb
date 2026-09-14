SUMMARY = "The ml_dtypes is a stand-alone implementation of several NumPy dtype extensions used in machine learning libraries"
HOMEPAGE = "https://github.com/jax-ml/ml_dtypes"
SECTION = "devel/python"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=3b83ef96387f14655fc854ddc3c6bd57"

SRC_URI[md5sum] = "ed4f6491b168ff2050c4ad373e9d76df"
SRC_URI[sha256sum] = "6488eb642acaaf08d8020f6de0a38acee7ac324c1e6e92ee0c0fea42422cb797"

inherit pypi setuptools3

SRCNAME ?= "ml_dtypes"
PYPI_SRC_URI = "https://files.pythonhosted.org/packages/fa/47/09ca9556bf99cfe7ddf129a3423642bd482a27a717bf115090493fa42429/${SRCNAME}-${PV}.tar.gz"

S = "${WORKDIR}/${SRCNAME}-${PV}"

DEPENDS = " \
    python3-pybind11-native \
    python3-numpy-native \
"

RDEPENDS:${PN} = " \
    python3-pybind11 \
    python3-numpy \
"

BBCLASSEXTEND = "native"


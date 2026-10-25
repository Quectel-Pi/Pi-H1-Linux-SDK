SUMMARY = "Python Imaging Library fork (Fork, prebuilt cp313 aarch64 wheel)"
DESCRIPTION = "Pillow 11.3.0, needed only for image input: \
request_frontend.py:121 does 'from PIL import Image, ImageOps' inside \
preprocess_image(), which resizes/decodes the data URL a chat request carries. \
Without it POST /v1/chat/completions {content:[{type:image_url}]} dies with \
ModuleNotFoundError: No module named 'PIL' and the client sees a 500 \
'generation failed: frontend: ...' instead of an answer. \
\
Why a wheel and not meta-python's python3-pillow_10.3.0.bb: that recipe builds \
against the Yocto python (3.12) and installs into \
/usr/lib/python3.12/site-packages, a directory the board's Debian 3.13 \
interpreter never searches (see qwen35-wheel.inc).  The manylinux wheel is \
also self-contained: libjpeg-turbo, libtiff, libopenjp2, libfreetype, liblcms2, \
libwebp, libpng and libavif arrive in pillow.libs/ and are found through \
$ORIGIN/../pillow.libs, so no -dev package or extra build dependency is \
pulled into the image. \
\
The OE package name is deliberately 'pillow', NOT 'python3-pillow': that name \
belongs to the meta-python recipe above, and RPROVIDING it would put two \
providers of PIL into the dependency graph."

HOMEPAGE = "https://python-pillow.org"
LICENSE = "HPND"
# MIT-CMU is the HPND text (the wheel's own LICENSE says so); Pillow's file also
# carries the licences of the libraries it bundles, which is why the wheel's
# copy -- not this short one -- is what do_install puts in the docdir.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/HPND;md5=faa364b3e3c6db0f74cc0e704ddf6b9c"

require qwen35-wheel.inc

# 5973503 bytes; sha256 is PyPI's, re-hashed after fetching into DL_DIR.
QWEN35_WHEEL_URL = "https://files.pythonhosted.org/packages/ba/c9/09e6746630fe6372c67c648ff9deae52a2bc20897d51fa293571977ceb5d"
QWEN35_WHEEL_NAME = "pillow-${PV}-cp313-cp313-manylinux_2_27_aarch64.manylinux_2_28_aarch64.whl"
SRC_URI[wheel.sha256sum] = "c37d8ba9411d6003bba9e518db0db0c58a680ab9fe5179f040b0463644bc9805"

# The module directory is PIL/, the distribution (and therefore the dist-info
# directory) is pillow.
QWEN35_PIL_DIR = "${PYTHON_DIST_PACKAGES}/PIL"
QWEN35_PIL_LIBS = "${PYTHON_DIST_PACKAGES}/pillow.libs"

RDEPENDS:${PN} = "python3-core"

do_install() {
	qwen35_install_wheel_tree ${D}${PYTHON_DIST_PACKAGES}

	# A pure-python fallback would import but cannot decode a PNG; the cp313
	# tag is the whole point of using this wheel, so fail the build rather than
	# the request.
	if [ ! -f ${D}${QWEN35_PIL_DIR}/_imaging.cpython-313-aarch64-linux-gnu.so ]; then
		bbfatal "${PN}: ${QWEN35_WHEEL_NAME} has no cp313 _imaging extension -- it will not import on the board's python 3.13"
	fi
	# $ORIGIN/../pillow.libs is where _imaging looks for libjpeg/libtiff/...;
	# a wheel whose libraries landed elsewhere would import and then fail on
	# the first decode.
	if [ ! -d ${D}${QWEN35_PIL_LIBS} ]; then
		bbfatal "${PN}: pillow.libs/ missing -- the bundled codecs would not be found through the RPATH"
	fi

	# Pillow's LICENSE covers Pillow *and* every bundled library; keep it with
	# the binaries we redistribute.
	install -d ${D}${docdir}/${PN}
	install -m 0644 ${QWEN35_WHEEL_ROOT}/pillow-${PV}.dist-info/licenses/LICENSE \
		${D}${docdir}/${PN}/LICENSE
}

FILES:${PN} = "${QWEN35_PIL_DIR} \
               ${QWEN35_PIL_LIBS} \
               ${PYTHON_DIST_PACKAGES}/${PN}-${PV}.dist-info \
               ${docdir}/${PN} \
"

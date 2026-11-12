SUMMARY = "Python Imaging Library fork (Fork, prebuilt cpXXX aarch64 wheel)"
DESCRIPTION = "Pillow 11.3.0, needed only for image input: \
request_frontend.py:118 does 'from PIL import Image, ImageOps' inside \
preprocess_image(), which resizes/decodes the data URL a chat request carries. \
Without it POST /v1/chat/completions {content:[{type:image_url}]} dies with \
ModuleNotFoundError: No module named 'PIL' and the client sees a 500 \
'generation failed: frontend: ...' instead of an answer. \
\
The wheel ABI follows the OS dimension (qwen35-wheel.inc), and the package is \
only pulled in where the overlaid sample rootfs does NOT already carry PIL: \
the Ubuntu 26.04 tree ships python3-pil 12.1.1 (cp314) for its own 3.14, so \
quecpi-image installs this recipe on DEBIAN only.  A wheel built for the wrong \
minor would import as far as a file, then fail on the first decode -- the \
cpXXX guard in do_install catches that at build time.  When the OS does carry \
PIL, this recipe's copy would land in the same directory as Debian's and mix \
two versions (the rsync overlay is --inplace without --delete), which is the \
second reason the image gates on QWEN35_ROOTFS_PYHAS. \
\
Why a wheel and not meta-python's python3-pillow_10.3.0.bb: that recipe builds \
against the Yocto python (3.12) and installs into \
/usr/lib/python3.12/site-packages, a directory the board's 3.13/3.14 \
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

# 5973503 bytes (cp313) / 5973336 bytes (cp314); sha256 is PyPI's, re-hashed
# after fetching into DL_DIR.  One URL per ABI -- SRC_URI varflags cannot be
# overridden, so the ABI rides on the name= tag (same pattern as
# ../onnxruntime_1.27.0.bb).
QWEN35_WHEEL_BASE:cp312 = "https://files.pythonhosted.org/packages/41/f1/6f2427a26fc683e00d985bc391bdd76d8dd4e92fac33d841127eb8fb2313"
QWEN35_WHEEL_BASE:cp313 = "https://files.pythonhosted.org/packages/ba/c9/09e6746630fe6372c67c648ff9deae52a2bc20897d51fa293571977ceb5d"
QWEN35_WHEEL_BASE:cp314 = "https://files.pythonhosted.org/packages/09/b5/0487044b7c096f1b48f0d7ad416472c02e0e4bf6919541b111efd3cae690"

QWEN35_WHEEL_URL = "${@ d.getVar('QWEN35_WHEEL_BASE:' + (d.getVar('QWEN35_PY_ABI') or 'cp313')) }"
QWEN35_WHEEL_NAME = "pillow-${PV}-${QWEN35_PY_TAG}-manylinux_2_27_aarch64.manylinux_2_28_aarch64.whl"
SRC_URI = "${QWEN35_WHEEL_URL}/${QWEN35_WHEEL_NAME};name=wheel_${QWEN35_PY_ABI};unpack=0;subdir=${QWEN35_WHEEL_SUBDIR}"

SRC_URI[wheel_cp312.sha256sum] = "97f07ed9f56a3b9b5f49d3661dc9607484e85c67e27f3e8be2c7d28ca032fec7"
SRC_URI[wheel_cp313.sha256sum] = "c37d8ba9411d6003bba9e518db0db0c58a680ab9fe5179f040b0463644bc9805"
SRC_URI[wheel_cp314.sha256sum] = "41742638139424703b4d01665b807c6468e23e699e8e90cffefe291c5832b027"

# The module directory is PIL/, the distribution (and therefore the dist-info
# directory) is pillow.
QWEN35_PIL_DIR = "${PYTHON_DIST_PACKAGES}/PIL"
QWEN35_PIL_LIBS = "${PYTHON_DIST_PACKAGES}/pillow.libs"

RDEPENDS:${PN} = "python3-core"

do_install() {
	qwen35_install_wheel_tree ${D}${PYTHON_DIST_PACKAGES}

	# A pure-python fallback would import but cannot decode a PNG; the ABI tag
	# is the whole point of using this wheel, so fail the build rather than
	# the request.  QWEN35_PY_EXT (conf/quectel-os-dim.inc) is 'cpython-313'/'cpython-314' -- the spelling
	# inside the wheel, not the cpXXX wheel tag.
	if [ ! -f ${D}${QWEN35_PIL_DIR}/_imaging.${QWEN35_PY_EXT}-aarch64-linux-gnu.so ]; then
		bbfatal "${PN}: ${QWEN35_WHEEL_NAME} has no ${QWEN35_PY_EXT} _imaging extension -- it will not import on the rootfs python ${QWEN35_PY_MINOR}"
	fi
	# Every one of the wheel's 8 extensions is ABI-tagged (_imaging, _imagingcms,
	# _imagingft, _imagingmath, _imagingmorph, _imagingtk, _avif, _webp).  Checking
	# only _imaging leaves the other 7 of a foreign-tagged tree behind, and PIL/ is
	# exactly where that happened for real: a board built with the cp313 wheel onto
	# a 3.14 rootfs kept 8 cpython-313 .so files that `dpkg -S` could not attribute,
	# while `import PIL` succeeded via the cp314 twin.  Prune the mismatched set.
	qwen35_prune_foreign_abi ${D}${QWEN35_PIL_DIR}
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

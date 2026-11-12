SUMMARY = "ONNX Runtime 1.27.0 runtime and cpXXX aarch64 python bindings (prebuilt wheel)"
DESCRIPTION = "The ONNX Runtime runtime that the Qwen3.5 NPU server and its native \
decoder engine link/import: libonnxruntime.so.1.27.0 (SONAME libonnxruntime.so.1), \
libonnxruntime_providers_shared.so and, from the same wheel, the cpXXX python \
extension onnxruntime_pybind11_state.cpython-XXX-aarch64-linux-gnu.so. \
\
The wheel is the upstream PyPI manylinux_2_27/2_28 aarch64 build, unpacked into \
/usr/lib/python3/dist-packages.  Which cpXXX wheel is built depends on the OS \
dimension (qwen35-wheel.inc): DEBIAN 13 overlays a rootfs whose python is 3.13 \
(cp313), UBUNTU 26.04 overlays one whose python is 3.14 (cp314), and LINUX has \
the OE-rootfs 3.12 (cp312).  A wheel of the wrong ABI is silently skipped by \
the interpreter -- the file is there, the import is not -- so the recipe picks \
the tag and do_install fails the build if it is missing.  An OE \
python3-onnxruntime built for one minor would be dead weight for the other two \
(qwen35-wheel.inc, ../qwen35-npu/DEPENDS_NOTES.md).  The C API has no headers \
here: they are not in the wheel, qwen35-npu vendors the matching 1.27.0 set in \
its files/native/ort_include/ and adds -I${STAGING_INCDIR} first, so a future \
headers package would win. \
\
libonnxruntime.so.1 is also published under /usr/lib/qwen35/lib, which is the \
$ORIGIN/lib RUNPATH of qwen35_text_engine and the first entry of the launcher's \
LD_LIBRARY_PATH.  Debian 13 ships /usr/lib/aarch64-linux-gnu/libonnxruntime.so.1 \
(1.21) with the same SONAME, so the engine must never depend on ld.so.cache \
order -- it does not: DT_RUNPATH is searched before the cache."
HOMEPAGE = "https://onnxruntime.ai/"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

require qwen35-wheel.inc

# 16438180 bytes (cp313) / 16439970 bytes (cp314); the sha256 is PyPI's, the
# file was fetched and re-hashed locally.
#
# One URL per python ABI.  The wheels unpack to the same paths and differ only in
# the ABI tag, so each gets its own downloadfilename -- a shared one would make
# DEBIAN and UBUNTU fight over a single DL_DIR entry and the second variant would
# fail its checksum.
#
# The selector is a plain :append-less variable because SRC_URI[...] varflags do
# NOT accept overrides ("SRC_URI[x.sha256sum:cp312] = ..." is a parse error).
# Same trick as ../npu-llm/npu-llm_1.0.bb's sherpa wheel: one varflag per ABI,
# named after the URL's name= tag.
QWEN35_WHEEL_BASE:cp312 = "https://files.pythonhosted.org/packages/84/86/c3b6b17745a1997d784dadc9bd88d713d2e6721139a5a0e885b28cfb79b1"
QWEN35_WHEEL_BASE:cp313 = "https://files.pythonhosted.org/packages/ce/88/24fc51fcbb126da6d032372314e47b55c3faad58f2aa78c0e199ccd20b9c"
QWEN35_WHEEL_BASE:cp314 = "https://files.pythonhosted.org/packages/6e/24/d535ca8a09dbf697f853377c8dc0820dbcaae5f334316b400b953afbcba8"

# The one wheel this build fetches, with the ABI in both the filename and the
# name= tag that keys its checksum.
QWEN35_WHEEL_URL = "${@ d.getVar('QWEN35_WHEEL_BASE:' + (d.getVar('QWEN35_PY_ABI') or 'cp313')) }"
QWEN35_WHEEL_NAME = "onnxruntime-${PV}-${QWEN35_PY_TAG}-manylinux_2_27_aarch64.manylinux_2_28_aarch64.whl"
SRC_URI = "${QWEN35_WHEEL_URL}/${QWEN35_WHEEL_NAME};name=wheel_${QWEN35_PY_ABI};unpack=0;subdir=${QWEN35_WHEEL_SUBDIR}"

SRC_URI[wheel_cp312.sha256sum] = "c6fddce0539a4898c7bef35b052ffd37935b2190e35488eab99ce91887743ea1"
SRC_URI[wheel_cp313.sha256sum] = "48b3d87eb560ff6a772240506f3c78d6d27c63cafedd5c775672e1194f968cfd"
SRC_URI[wheel_cp314.sha256sum] = "5b51c014cf1a4fcd93c29a97eac8071fa27710dae05a4d0380bb60a66d60a62c"

# numpy is deliberately NOT listed as a runtime dependency: on DEBIAN the
# sample rootfs already carries python3-numpy 2.2.4 for its own 3.13, which is
# what DEPENDS_NOTES.md W3 is about.  UBUNTU has no numpy at all, so image
# pulls ../python3-numpy-qwen35_2.3.5.bb from there (see quecpi-image.bb).  Either way OE's
# python3-numpy would land in the 3.12 site-packages this interpreter never
# imports.
# Same for flatbuffers/protobuf/packaging: onnxruntime's METADATA requires them,
# but nothing in capi/__init__.py imports them (checked), so installing them
# would only add a second protobuf on top of the distro's.
RDEPENDS:${PN} = "python3-core"

do_install() {
	qwen35_install_wheel_tree ${D}${PYTHON_DIST_PACKAGES}

	# Guard against a wheel bump that silently changes the ABI tag: the module
	# must match the interpreter of the rootfs being overlaid, or it is
	# unimportable (this is exactly the bug that made qwen3.5 dead on Ubuntu:
	# a cp313 extension tree in front of a 3.14 interpreter).
	# The tag inside the wheel is 'cpython-314', not 'cp314' -- see
	# QUECTEL_PY_EXT in conf/quectel-os-dim.inc.
	if ! ls ${D}${PYTHON_DIST_PACKAGES}/onnxruntime/capi/onnxruntime_pybind11_state.${QWEN35_PY_EXT}-*.so >/dev/null 2>&1; then
		bbfatal "onnxruntime: no ${QWEN35_PY_EXT} python extension in ${QWEN35_WHEEL_NAME} -- it will not import on the rootfs python ${QWEN35_PY_MINOR}"
	fi
	# A foreign ABI module next to it would shadow nothing but still confuse the
	# scan (and dpkg -S): keep exactly one.  The wheel ships a single
	# ABI-tagged extension, so the shared helper's tree walk finds just it.
	qwen35_prune_foreign_abi ${D}${PYTHON_DIST_PACKAGES}/onnxruntime/capi

	# The wheel ships only libonnxruntime.so.1.27.0 (verified: no .so.1 entry),
	# but the SONAME is libonnxruntime.so.1, so the loader -- and any -lonnxruntime
	# link -- asks for the SONAME name.  pip's installer created this symlink; do
	# the same, relative, so the tree stays relocatable.
	ln -sf libonnxruntime.so.1.27.0 \
		${D}${PYTHON_DIST_PACKAGES}/onnxruntime/capi/libonnxruntime.so.1

	# Second location, for the native engine.  A *real* file, not a symlink: the
	# engine is linked by path in do_compile against ${STAGING_LIBDIR}/qwen35/lib,
	# which only exists because populate_sysroot copies regular files out of
	# ${libdir}, and the board copy must survive anyone moving/removing the
	# python package.  Consequence: this SONAME is provided by two directories in
	# the same package, so any recipe that links against it has to declare
	# PRIVATE_LIBS (qwen35-npu does) or oe's shlib lookup fails with "Multiple
	# shlib providers for libonnxruntime.so.1".
	install -d ${D}${QWEN35_ORT_LIBDIR}
	install -m 0755 ${D}${PYTHON_DIST_PACKAGES}/onnxruntime/capi/libonnxruntime.so.1.27.0 \
		${D}${QWEN35_ORT_LIBDIR}/libonnxruntime.so.1.27.0
	ln -sf libonnxruntime.so.1.27.0 ${D}${QWEN35_ORT_LIBDIR}/libonnxruntime.so.1
	ln -sf libonnxruntime.so.1.27.0 ${D}${QWEN35_ORT_LIBDIR}/libonnxruntime.so

	# MIT + the bundled third-party notices (the wheel's own files, not the
	# generic text) so the image carries the attribution upstream requires.
	install -d ${D}${docdir}/onnxruntime
	install -m 0644 ${QWEN35_WHEEL_ROOT}/onnxruntime/LICENSE \
		${QWEN35_WHEEL_ROOT}/onnxruntime/ThirdPartyNotices.txt \
		${D}${docdir}/onnxruntime/
}

FILES:${PN} = "${PYTHON_DIST_PACKAGES}/onnxruntime \
               ${PYTHON_DIST_PACKAGES}/onnxruntime-${PV}.dist-info \
               ${QWEN35_ORT_LIBDIR} \
               ${docdir}/onnxruntime \
"

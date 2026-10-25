SUMMARY = "ONNX Runtime 1.27.0 runtime and cp313 aarch64 python bindings (prebuilt wheel)"
DESCRIPTION = "The ONNX Runtime runtime that the Qwen3.5 NPU server and its native \
decoder engine link/import: libonnxruntime.so.1.27.0 (SONAME libonnxruntime.so.1), \
libonnxruntime_providers_shared.so and, from the same wheel, the cp313 python \
extension onnxruntime_pybind11_state.cpython-313-aarch64-linux-gnu.so. \
\
The wheel is the upstream PyPI manylinux_2_27/2_28 aarch64 build, unpacked into \
/usr/lib/python3/dist-packages (the interpreter on the board is Debian's 3.13, \
so an OE python3-onnxruntime built for 3.12 would be dead weight -- see \
qwen35-wheel.inc and ../qwen35-npu/DEPENDS_NOTES.md).  The C API has no headers \
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

# 16438180 bytes; the sha256 is PyPI's, the file was fetched and re-hashed locally.
QWEN35_WHEEL_URL = "https://files.pythonhosted.org/packages/ce/88/24fc51fcbb126da6d032372314e47b55c3faad58f2aa78c0e199ccd20b9c"
QWEN35_WHEEL_NAME = "onnxruntime-${PV}-cp313-cp313-manylinux_2_27_aarch64.manylinux_2_28_aarch64.whl"
SRC_URI[wheel.sha256sum] = "48b3d87eb560ff6a772240506f3c78d6d27c63cafedd5c775672e1194f968cfd"

# numpy is deliberately NOT listed: the board rootfs already carries a cp313
# numpy in dist-packages, while OE's python3-numpy would land in the 3.12
# site-packages that this interpreter never imports (DEPENDS_NOTES.md W3).
# Same for flatbuffers/protobuf/packaging: onnxruntime's METADATA requires them,
# but nothing in capi/__init__.py imports them (checked), so installing them
# would only add a second protobuf on top of Debian's.
RDEPENDS:${PN} = "python3-core"

do_install() {
	qwen35_install_wheel_tree ${D}${PYTHON_DIST_PACKAGES}

	# Guard against a wheel bump that silently changes the ABI tag: the modules
	# must be cp313 or the board's 3.13 interpreter cannot import them.
	if ! ls ${D}${PYTHON_DIST_PACKAGES}/onnxruntime/capi/onnxruntime_pybind11_state.cpython-313-*.so >/dev/null 2>&1; then
		bbfatal "onnxruntime: no cp313 python extension in ${QWEN35_WHEEL_NAME}"
	fi

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

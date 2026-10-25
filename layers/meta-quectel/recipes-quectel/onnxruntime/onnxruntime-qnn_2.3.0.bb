SUMMARY = "ONNX Runtime QNN execution-provider plugin + Qualcomm AI Runtime 2.47 libraries (prebuilt wheel)"
DESCRIPTION = "The QNNExecutionProvider plugin for ONNX Runtime 1.27.0 \
(libonnxruntime_providers_qnn.so, package version 2.3.0) together with the QAIRT \
2.47.0 libraries it dlopen()s, and the python module onnxruntime_qnn that reports \
those paths and imports without side effects. \
\
Only the Hexagon V68 subset is installed: libonnxruntime_providers_qnn.so, \
libQnnHtp.so, libQnnSystem.so, libQnnHtpV68Stub.so, libQnnHtpV68Skel.so, plus \
libQnnHtpPrepare.so and libQnnHtpNetRunExtensions.so.  The rest of the wheel \
(V69/V73/V75/V79/V81 skel+stub, QnnCpu, QnnGpu, QnnIr, QnnSaver, Genie) is \
deleted, see QWEN35_QNN_DROP.  libQnnHtpV68Skel.so is additionally installed \
into the DSP-visible tree /usr/lib/dsp/cdsp, because the Hexagon loader does not \
look inside a python package directory. \
\
Layout is dictated by the plugin, not by us: onnxruntime_qnn/__init__.py returns \
os.path.dirname(__file__) + '/libonnxruntime_providers_qnn.so' and every library \
in that directory has RUNPATH=$ORIGIN, so module and libraries must stay \
together in /usr/lib/python3/dist-packages/onnxruntime_qnn/.  /usr/lib/qwen35/qnn \
is a symlink to it -- that is the QNN_DIR qwen35-launch.sh and runtime.json \
use."
HOMEPAGE = "https://pypi.org/project/onnxruntime-qnn/"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

require qwen35-wheel.inc

# 78730222 bytes; the sha256 is PyPI's, the file was fetched and re-hashed locally.
QWEN35_WHEEL_URL = "https://files.pythonhosted.org/packages/a0/31/ffce5b1530cb541c49da16cadc2cd94774eeb660bcf5c66be97dc77ee613"
QWEN35_WHEEL_NAME = "onnxruntime_qnn-${PV}-cp313-cp313-manylinux_2_34_aarch64.whl"
SRC_URI[wheel.sha256sum] = "382e31b7991a7a1003be636a0a97e888d3b95ffadef2162bdece74b5106f0aef"

# onnxruntime: the plugin is loaded into ORT 1.27.0 by RegisterExecutionProviderLibrary()
# and is ABI-locked to that exact release, so the core package is a hard dependency.
# fastrpc: libQnnHtpV68Stub.so has NEEDED libcdsprpc.so -- HTP sessions run on the
# CDSP (cdsprpcd.service), not the ADSP.  meta-quectel's fastrpc bbappend already
# enables cdsprpcd, so only the package dependency is missing here.
RDEPENDS:${PN} = "onnxruntime fastrpc python3-core"

# The V68 runtime this board needs.  libQnnHtpPrepare.so is kept on purpose:
# the plugin contains a dlopen("libQnnHtpPrepare.so") path
# (QnnBackendManager::LoadQnnHtpPrepareLib) that is entered when the HTP backend
# is set up in some ORT configurations, and whether it is eager cannot be
# verified without the board.  It is only used to *generate* context binaries,
# which the model package ships precompiled, so if qwen35.service is confirmed
# healthy on hardware this one file (88 MB) can be moved to QWEN35_QNN_DROP.
# libQnnHtpNetRunExtensions.so stays too, for a cheaper version of the same
# argument: nothing in the ORT path names it (a strings scan over the plugin and
# over every kept QAIRT library finds it only inside libGenie.so, which *is*
# dropped), so in theory it is dead weight -- but it is 825 KB, not 88 MB, and a
# dlopen of a library that was pruned is a failure mode that only the board
# shows.  Drop it later if the image ever needs the last megabyte.
QWEN35_QNN_KEEP = " \
    libonnxruntime_providers_qnn.so \
    libQnnHtp.so \
    libQnnSystem.so \
    libQnnHtpV68Stub.so \
    libQnnHtpV68Skel.so \
    libQnnHtpPrepare.so \
    libQnnHtpNetRunExtensions.so \
"

# ~73 MB of dead weight on a QCS6490: DSP generations this SoC does not have,
# the CPU/GPU/IR/Saver backends (the EP is configured with the HTP backend path
# only) and Genie (npu-llm ships the QAIRT 2.40 copy it was built against).
# libQnnIr/libQnnSaver are model-preparation/conversion libraries that no
# inference path loads, and the package ships precompiled context binaries.
QWEN35_QNN_DROP = " \
    libGenie.so \
    libQnnCpu.so \
    libQnnGpu.so \
    libQnnIr.so \
    libQnnSaver.so \
    libQnnHtpV69Stub.so libQnnHtpV69Skel.so \
    libQnnHtpV73Stub.so libQnnHtpV73Skel.so \
    libQnnHtpV75Stub.so libQnnHtpV75Skel.so \
    libQnnHtpV79Stub.so libQnnHtpV79Skel.so \
    libQnnHtpV81Stub.so libQnnHtpV81Skel.so \
"

do_install() {
	qwen35_install_wheel_tree ${D}${PYTHON_DIST_PACKAGES}

	for lib in ${QWEN35_QNN_KEEP}; do
		if [ ! -f ${D}${QWEN35_QNN_PYDIR}/$lib ]; then
			bbfatal "onnxruntime-qnn: $lib is missing from ${QWEN35_WHEEL_NAME}"
		fi
	done
	for lib in ${QWEN35_QNN_DROP}; do
		rm -f ${D}${QWEN35_QNN_PYDIR}/$lib
	done
	# Nothing outside QWEN35_QNN_KEEP may survive.  A silent extra library is tens
	# of megabytes of flash; a missing one is a dlopen failure only the board
	# would show, so both are checked here.
	for so in ${D}${QWEN35_QNN_PYDIR}/*.so; do
		[ -e "$so" ] || continue
		keep=0
		for lib in ${QWEN35_QNN_KEEP}; do
			if [ "$(basename "$so")" = "$lib" ]; then
				keep=1
			fi
		done
		if [ "$keep" != "1" ]; then
			bbfatal "onnxruntime-qnn: $(basename "$so") is not in QWEN35_QNN_KEEP (add it, or add it to QWEN35_QNN_DROP)"
		fi
	done

	# Contract with files/qwen35-launch.sh (QNN_DIR=$LIB_DIR/qnn, appended to
	# LD_LIBRARY_PATH), with the generated runtime.json and with
	# qwen35.service's QWEN35_QNN_DIR: one symlink, so the QNN tree has exactly
	# one copy on disk.  Relative on purpose -- "../python3/dist-packages" from
	# /usr/lib/qwen35 resolves both on the board and inside the recipe sysroot,
	# whereas an absolute /usr/lib/python3/... target would dangle in the sysroot
	# (staging root is not /) and can trip up the packaging/sysroot passes.
	# Dangling risk is nil: the target is installed by this same package, so
	# both appear or neither does.
	install -d ${D}${QWEN35_LIBDIR}
	ln -sf ../python3/dist-packages/onnxruntime_qnn ${D}${QWEN35_QNN_LINKDIR}

	# The DSP-side skeleton, in the tree the CDSP can actually see.  A real copy,
	# not a symlink into /usr/lib/python3/...: the Hexagon loader opens this path
	# from the DSP domain and nothing guarantees it follows a host symlink into
	# an unrelated subtree.  9.6 MB duplicated is cheap next to a 14001
	# ("failed to load skel") at every start.  Same bytes as the copy above, so
	# qwen35-launch.sh's backend/skel version pairing check still passes.
	install -d ${D}${QWEN35_DSP_SKEL_DIR}
	install -m 0755 ${D}${QWEN35_QNN_PYDIR}/libQnnHtpV68Skel.so \
		${D}${QWEN35_DSP_SKEL_DIR}/libQnnHtpV68Skel.so

	# Attribution: upstream LICENSE, the Qualcomm binary licence and the
	# third-party notices all come with the wheel.
	install -d ${D}${docdir}/onnxruntime-qnn
	install -m 0644 ${QWEN35_WHEEL_ROOT}/onnxruntime_qnn/LICENSE \
		${QWEN35_WHEEL_ROOT}/onnxruntime_qnn/Qualcomm_LICENSE.pdf \
		${QWEN35_WHEEL_ROOT}/onnxruntime_qnn/ThirdPartyNotices.txt \
		${QWEN35_WHEEL_ROOT}/onnxruntime_qnn/Privacy.md \
		${D}${docdir}/onnxruntime-qnn/
}

FILES:${PN} = "${QWEN35_QNN_PYDIR} \
               ${PYTHON_DIST_PACKAGES}/onnxruntime_qnn-${PV}.dist-info \
               ${QWEN35_QNN_LINKDIR} \
               ${QWEN35_DSP_SKEL_DIR}/libQnnHtpV68Skel.so \
               ${docdir}/onnxruntime-qnn \
"

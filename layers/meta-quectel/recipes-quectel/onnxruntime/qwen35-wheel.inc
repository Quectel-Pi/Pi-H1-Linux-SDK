# qwen35-wheel.inc -- install rules shared by the prebuilt manylinux wheels the
# Qwen3.5 NPU runtime (../qwen35-npu) imports.
#
#   onnxruntime_1.27.0.bb        -> onnxruntime       (ORT core + cp313 bindings)
#   onnxruntime-qnn_2.3.0.bb     -> onnxruntime-qnn   (QNN EP plugin + QAIRT 2.47)
#   tokenizers_0.22.2.bb         -> python3-tokenizers (Rust tokenizer, abi3)
#   pillow_11.3.0.bb             -> pillow            (image decoding, cp313)
#
# Why wheels and not OHOS/OE recipes is argued in ../qwen35-npu/DEPENDS_NOTES.md;
# the short version:
#
#   * the board rootfs is Debian 13 (prebuild/debian-gnome-rootfs.tar.xz).  Its
#     /usr/bin/python3 is 3.13 and its sys.path is
#         /usr/lib/python3/dist-packages  +  /usr/lib/python3.13
#     A Yocto python module is built for 3.12 and installed below
#     ${PYTHON_SITEPACKAGES_DIR} (i.e. /usr/lib/python3.12/site-packages), a
#     directory that interpreter never searches.  The destination below is
#     therefore FIXED and must NOT be "corrected" to ${PYTHON_SITEPACKAGES_DIR}.
#   * the QNN execution provider only ships as a binary built against one exact
#     ORT ABI (1.27.0), so no source recipe can produce it.
#   * the wheels are manylinux aarch64 + cp313 (tokenizers: cp39-abi3), i.e.
#     glibc 2.34 / aarch64 -- exactly this board's userspace, nothing to compile.
#   * pillow replaces meta-python's python3-pillow for the same reason as numpy:
#     that recipe targets Yocto's python, while request_frontend.py's PIL import
#     has to resolve inside the Debian 3.13 sys.path.

# Debian site-packages directory.  Literal on purpose, see above.
PYTHON_DIST_PACKAGES = "/usr/lib/python3/dist-packages"

# Program root of qwen35-npu (its QWEN35_LIBDIR) and the two paths inside it that
# the native engine and files/qwen35-launch.sh resolve at runtime:
#   LD_LIBRARY_PATH = $LIB_DIR:$QNN_DIR:$LIB_DIR/lib   (launch.sh)
#   engine DT_RUNPATH = $ORIGIN/lib:$ORIGIN            (qwen35-npu do_compile)
QWEN35_LIBDIR = "${libdir}/qwen35"
QWEN35_ORT_LIBDIR = "${QWEN35_LIBDIR}/lib"
QWEN35_QNN_LINKDIR = "${QWEN35_LIBDIR}/qnn"
QWEN35_QNN_PYDIR = "${PYTHON_DIST_PACKAGES}/onnxruntime_qnn"

# The DSP-visible firmware tree (CDSP domain).  Not an OE path variable: this is
# where the Hexagon loader looks on the *device*, so it is spelled out
# absolutely and must match files/qwen35-launch.sh SKEL_DIR.
QWEN35_DSP_SKEL_DIR = "/usr/lib/dsp/cdsp"

# ---------------------------------------------------------------- fetching --
# The recipe sets QWEN35_WHEEL_URL + QWEN35_WHEEL_NAME and the sha256 varflag.
# unpack=0 keeps bitbake from guessing at the archive; the do_unpack postfunc
# below extracts it with python's zipfile, which -- unlike unzip -- is
# guaranteed to be present and whose only defect (dropping the permission bits)
# is fixed in qwen35_extract_wheel.
QWEN35_WHEEL_NAME ??= ""
QWEN35_WHEEL_URL ??= ""
QWEN35_WHEEL_SUBDIR ??= "whl"
QWEN35_WHEEL_FILE ??= "${WORKDIR}/${QWEN35_WHEEL_SUBDIR}/${QWEN35_WHEEL_NAME}"
QWEN35_WHEEL_ROOT ??= "${WORKDIR}/wheel-root"

SRC_URI = "${QWEN35_WHEEL_URL}/${QWEN35_WHEEL_NAME};name=wheel;unpack=0;subdir=${QWEN35_WHEEL_SUBDIR}"

# Wheels are binary aarch64: the package must not be shared between machines.
PACKAGE_ARCH = "${MACHINE_ARCH}"

# Nothing is compiled; ${WORKDIR} holds the unpacked wheel and nothing else.
S = "${WORKDIR}"
do_configure[noexec] = "1"
do_compile[noexec] = "1"

inherit python3native
# python3 (native) has to be in the recipe sysroot before do_unpack runs -- the
# same pattern the other wheel recipe in this layer set uses
# (meta-tensorflow .../tensorboard_2.16.2.bb).
do_unpack[depends] += "python3-native:do_populate_sysroot"

# --------------------------------------------------------------------- qa ---
# already-stripped: shipped stripped by upstream, must not be re-stripped.
# dev-so:           libonnxruntime.so / .so.1 live next to the versioned file by
#                   design; a firmware image has no -dev package to move them to.
# file-rdeps:       these are manylinux binaries whose NEEDED set (libstdc++,
#                   libgcc_s, libcdsprpc, ...) is satisfied by the Debian rootfs
#                   and by packages listed in RDEPENDS; the OE shlib scan only
#                   sees the OE half of the image.
# libdir:           .so files under /usr/lib/python3/... instead of ${libdir};
#                   that is the layout pip produces and the layout the board's
#                   python expects, so the "relocate me" hint is wrong here.
# arch:             libQnnHtpV*Skel.so are Hexagon DSP6 images (ELF32, e_machine
#                   164, SYSV hash), not aarch64 userspace ELFs -- same reason
#                   qai-appbuilder_2.48.40.bb skips it.
# ldflags:          prebuilt binaries, no OE LDFLAGS were involved (GNU_HASH
#                   check is meaningless for the skels, see above).
INSANE_SKIP:${PN} += "already-stripped dev-so file-rdeps libdir arch ldflags"

# Prebuilt redistributables: never let the packaging classes touch the binaries.
# Stripping is the risky one -- libQnnHtpV68Skel.so is an ELF32 Hexagon image
# (e_machine 164), so ${TARGET_PREFIX}strip refuses it with "file format not
# recognized", and re-stripping the manylinux shared objects can only make them
# worse.  Debug splitting has nothing to do either: the payload ships stripped
# and carries no .debug sections.
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_SYSROOT_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"

# ---------------------------------------------------------------- helpers ---
# Extract ${QWEN35_WHEEL_FILE} into ${QWEN35_WHEEL_ROOT}.  Idempotent: called by
# the do_unpack postfunc below, and again by do_install if the tree ever went
# missing.
qwen35_extract_wheel() {
	rm -rf "${QWEN35_WHEEL_ROOT}"
	install -d "${QWEN35_WHEEL_ROOT}"
	if [ ! -f "${QWEN35_WHEEL_FILE}" ]; then
		bbfatal "qwen35-wheel: ${QWEN35_WHEEL_FILE} not found -- check QWEN35_WHEEL_NAME against the SRC_URI basename (subdir=${QWEN35_WHEEL_SUBDIR})"
	fi
	python3 -m zipfile -e "${QWEN35_WHEEL_FILE}" "${QWEN35_WHEEL_ROOT}"
	# zipfile creates every entry as 0666 & ~umask, so .so files arrive
	# non-executable.  The loader and OE's strip pass both care, so restore the
	# mode the wheels actually carry in their metadata (0755).
	find "${QWEN35_WHEEL_ROOT}" \( -name '*.so' -o -name '*.so.*' \) -exec chmod 0755 {} +
}

# Install the extracted wheel tree into ${D}<dest>.  A wheel's root IS the
# site-packages root, so this is what "pip install --target" would have left.
qwen35_install_wheel_tree() {
	if [ ! -d "${QWEN35_WHEEL_ROOT}" ]; then
		qwen35_extract_wheel
	fi
	install -d "$1"
	cp -a "${QWEN35_WHEEL_ROOT}/." "$1/"
	# Never ship byte code: it is tied to the exact interpreter patch level and
	# would be stale the first time the board's python3 is upgraded.
	find "$1" -name '__pycache__' -prune -exec rm -rf {} +
}

# NOT "do_unpack:append() { ... }": in this distro do_unpack is a *python*
# function (base.bbclass exports it as shell code from base_do_unpack), and an
# :append override would splice this tab-indented shell body straight into that
# python source, which fails to parse (TabError -> "Unable to parse <recipe>").
# A postfunc runs right after the builtin unpack instead, whatever its type.
do_unpack[postfuncs] += "qwen35_extract_wheel"

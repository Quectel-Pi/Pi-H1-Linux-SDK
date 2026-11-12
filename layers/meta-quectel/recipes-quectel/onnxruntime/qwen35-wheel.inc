# qwen35-wheel.inc -- install rules shared by the prebuilt manylinux wheels the
# Qwen3.5 NPU runtime (../qwen35-npu) imports.
#
#   onnxruntime_1.27.0.bb        -> onnxruntime       (ORT core + python bindings)
#   onnxruntime-qnn_2.3.0.bb     -> onnxruntime-qnn   (QNN EP plugin + QAIRT 2.47)
#   tokenizers_0.22.2.bb         -> python3-tokenizers (Rust tokenizer, abi3)
#   pillow_11.3.0.bb             -> pillow            (image decoding, cpXXX)
#   python3-numpy-qwen35_2.3.5.bb               -> python3-numpy-qwen35 (only where the rootfs
#                                                       does not ship numpy)
#
# Why wheels and not OHOS/OE recipes is argued in ../qwen35-npu/DEPENDS_NOTES.md;
# the short version:
#
#   * the board rootfs is a Debian/Ubuntu sample (prebuild/*-gnome-rootfs).  Its
#     /usr/bin/python3 is 3.13 on Debian 13 and 3.14 on Ubuntu 26.04, and its
#     sys.path is
#         /usr/lib/python3/dist-packages  +  /usr/lib/python3.<minor>
#     A Yocto python module is built for 3.12 and installed below
#     ${PYTHON_SITEPACKAGES_DIR} (i.e. /usr/lib/python3.12/site-packages), a
#     directory those interpreters never search.
#   * the QNN execution provider only ships as a binary built against one exact
#     ORT ABI (1.27.0), so no source recipe can produce it.
#   * the wheels are manylinux aarch64 and carry a python ABI tag, i.e. one
#     wheel per interpreter minor version -- see QWEN35_PY_ABI below.
#   * pillow replaces meta-python's python3-pillow for the same reason as numpy:
#     that recipe targets Yocto's python, while request_frontend.py's PIL import
#     has to resolve inside the Debian/Ubuntu sys.path.

# ------------------------------------------------------- python ABI -------
# The prebuilt wheels are ABI-specific: a cp313 extension module is not even
# importable by the 3.14 interpreter in the Ubuntu 26.04 rootfs (it is skipped,
# and the import fails with
#   ModuleNotFoundError: No module named 'onnxruntime.capi.onnxruntime_pybind11_state'
# even though the file is right next to it).  The OS dimension therefore decides
# which wheel each recipe fetches.
#
# QUECTEL_OS_DIM is written into local.conf by quectel_build/compile/build.sh
# (LINUX | DEBIAN | UBUNTU); the mapping itself lives in
# conf/quectel-os-dim.inc (layer-wide, because quecpi-image.bb and npu-llm need
# the same answers).  These are only the QWEN35_* alias names the wheel recipes
# below already use.
#
#   LINUX   OE-only rootfs, /usr/bin/python3 -> 3.12  (site-packages)
#   DEBIAN  prebuild/debian-gnome-rootfs  -> 3.13    (dist-packages)
#   UBUNTU  prebuild/ubuntu26-gnome-rootfs -> 3.14   (dist-packages)
#
# Adding a system dimension means adding a row in quectel-os-dim.inc -- and
# upstream PyPI has to ship a wheel for that ABI tag (checked for all three).
QWEN35_OS_DIM  ??= "${QUECTEL_OS_DIM}"
QWEN35_PY_ABI  ??= "${QUECTEL_PY_ABI}"
QWEN35_PY_MINOR ??= "${QUECTEL_PY_MINOR}"
# "cp313-cp313" -- the tag appears twice in a wheel filename.
QWEN35_PY_TAG ??= "${QWEN35_PY_ABI}-${QWEN35_PY_ABI}"
# '.cpython-313-...' -- how the tag appears inside an extension module's name.
# Guards that stat a file use this, not QWEN35_PY_ABI (cp313-cp314 differ only
# in the digits, but they differ, and a guard that looks for the wrong spelling
# simply never matches -- it fails the build loudly, or silently never removes
# the stale copy).
QWEN35_PY_EXT ??= "${QUECTEL_PY_EXT}"

# Modules the sample rootfs already provides; recipes use it to decide whether
# they have to install their own copy.  See conf/quectel-os-dim.inc for the
# evidence per rootfs (Debian ships numpy and no PIL; Ubuntu ships PIL and no
# numpy).
QWEN35_ROOTFS_PYHAS ??= "${QUECTEL_ROOTFS_PYHAS}"

# True when the sample rootfs already provides module $1.
qwen35_rootfs_provides() {
	quectel_rootfs_provides "$@"
}

# Destination of every wheel install, from conf/quectel-os-dim.inc: dist-packages
# on the Debian/Ubuntu overlays (the one path their site.py always adds), the OE
# site-packages on LINUX.
PYTHON_DIST_PACKAGES = "${QUECTEL_PY_DEST}"

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
# Each recipe sets SRC_URI itself, because the ABI has to be in the entry's name=
# tag to key its own sha256 varflag (SRC_URI[...] varflags cannot be overridden,
# so "SRC_URI[wheel.sha256sum:cp313]" does not parse).  The pattern is
#     SRC_URI = "${QWEN35_WHEEL_URL}/${QWEN35_WHEEL_NAME};name=wheel_${QWEN35_PY_ABI};unpack=0;..."
#     SRC_URI[wheel_cp313.sha256sum] = "..."
# The recipe also sets QWEN35_WHEEL_URL + QWEN35_WHEEL_NAME; what lives here is
# the shared unpack/install machinery.
#
# unpack=0 keeps bitbake from guessing at the archive; the do_unpack postfunc
# below extracts it with python's zipfile, which -- unlike unzip -- is
# guaranteed to be present and whose only defect (dropping the permission bits)
# is fixed in qwen35_extract_wheel.
QWEN35_WHEEL_NAME ??= ""
QWEN35_WHEEL_URL ??= ""
QWEN35_WHEEL_SUBDIR ??= "whl"
QWEN35_WHEEL_FILE ??= "${WORKDIR}/${QWEN35_WHEEL_SUBDIR}/${QWEN35_WHEEL_NAME}"
QWEN35_WHEEL_ROOT ??= "${WORKDIR}/wheel-root"

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

# Drop extension modules in $1 whose ABI tag is not the one this build targets.
#
# Why this is needed at all: do_install only sees ${D}, so it normally contains
# exactly one wheel's files.  But ${D}${PYTHON_DIST_PACKAGES} is also the
# directory the sample rootfs overlays, and a wheel selected for the wrong OS
# dimension leaves a whole tree of foreign-tagged extensions there -- 8 of them
# under PIL/ and 20 under numpy/ on a real board (verified: they are orphan
# files with no dpkg owner, because the overlay rsync is --inplace without
# --delete, so replacing var/lib/dpkg/status keeps the payload and loses the
# package record).  Python then silently skips them at import time: the file is
# there, `import` still succeeds through the correctly-tagged twin, and the
# leftovers cost ~4 MB while making `dpkg -S` and any later audit lie.  Removing
# them here keeps the shipped tree honest for every dimension.
#
# Deliberately matched on the '.cpython-NNN-' spelling, which is what appears in
# an extension module's name; the cpNNN wheel tag never does.  Pure-python and
# abi3 modules (tokenizers' .abi3.so, every .py) are untouched by the pattern.
qwen35_prune_foreign_abi() {
	[ -d "$1" ] || return 0
	find "$1" -name '*.cpython-*-aarch64-linux-gnu.so' \
		! -name "*.${QWEN35_PY_EXT}-aarch64-linux-gnu.so" -print -delete
}

# NOT "do_unpack:append() { ... }": in this distro do_unpack is a *python*
# function (base.bbclass exports it as shell code from base_do_unpack), and an
# :append override would splice this tab-indented shell body straight into that
# python source, which fails to parse (TabError -> "Unable to parse <recipe>").
# A postfunc runs right after the builtin unpack instead, whatever its type.
do_unpack[postfuncs] += "qwen35_extract_wheel"

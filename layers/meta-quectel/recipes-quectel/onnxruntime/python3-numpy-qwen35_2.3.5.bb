SUMMARY = "Numerical python core (prebuilt cpXXX aarch64 wheel) for the Qwen3.5 NPU runtime"
DESCRIPTION = "numpy is the one dependency of the Qwen3.5 NPU server that is neither in \
the onnxruntime wheel nor in every sample rootfs, and it is imported unguarded by \
the very first line of the server's import chain: \
  qwen_server.py:39, request_frontend.py:13, image_frontend.py:26, \
  vision_runtime.py:22, stage_worker.py:29, state_encoder.py:14, \
  state_handoff.py:8, runtime_cache.py:19, prompt_bundle.py:10, \
  native_handoff.py:10. \
Without it 'systemctl start qwen35' dies in 2 s with \
  ModuleNotFoundError: No module named 'numpy' \
and every symptom the user sees is downstream of that: npu-chat never reaches the \
qwen3.5 model (its availability gate checks the model ASSETS, not the runtime, so \
the model looks installed), and the server never listens on 127.0.0.1:8000. \
\
Where it is needed -- the two sample rootfs trees differ, and neither \
difference is obvious from the tarball name: \
  * DEBIAN 13 (prebuild/debian-gnome-rootfs) ships python3-numpy 2.2.4 for its \
    own CPython 3.13 in /usr/lib/python3/dist-packages, so this recipe is NOT \
    installed there (see QUECTEL_ROOTFS_PYHAS in conf/quectel-os-dim.inc). \
  * UBUNTU 26.04 (prebuild/ubuntu26-gnome-rootfs) ships PIL but NOT numpy, \
    because python3-numpy is not part of Ubuntu's default gnome seed; that is \
    the configuration in which qwen3.5 was dead on arrival. \
  * LINUX (pure OE rootfs, python 3.12) has poky's own python3-numpy, also not \
    this recipe. \
\
Why a wheel and not meta-oe's python3-numpy: that recipe is compiled for the \
Yocto python (3.12) and installs into /usr/lib/python3.12/site-packages, a \
directory the overlaid rootfs' 3.13/3.14 interpreter never searches \
(conf/quectel-os-dim.inc).  The manylinux wheel is also self-contained: the \
BLAS/LAPACK and gfortran runtime it needs arrive in numpy.libs/ and are resolved \
through $ORIGIN/../numpy.libs, so the image does not grow a libopenblas package \
or a gcc runtime for it. \
\
The package name is 'python3-numpy-qwen35', not 'python3-numpy': the latter is \
poky's recipe, and RPROVIDING it would put two providers of `import numpy` into \
the dependency graph -- exactly the situation that makes the image pick the 3.12 \
copy again."
HOMEPAGE = "https://numpy.org"
LICENSE = "BSD-3-Clause"
# numpy's LICENSE.txt (which carries the BSD-3-Clause text plus the OpenBLAS/
# gfortran notices) cannot be the LIC_FILES_CHKSUM source: it only exists after
# qwen35_extract_wheel has unpacked the wheel into ${QWEN35_WHEEL_ROOT}, while
# do_populate_lic needs the file at a path it can read *before* that (${S} has
# just the .whl).  So hash the canonical text and ship numpy's own copy from
# do_install into the docdir.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/BSD-3-Clause;md5=550794465ba0ec5312d6919e203a55f9"

require qwen35-wheel.inc

# One URL per python ABI.  Sizes: 14250702 (cp312) / 14248902 (cp313) /
# 14262696 (cp314).  sha256s are PyPI's, re-hashed after fetching.
#
# SRC_URI[...] varflags do NOT accept overrides -- "SRC_URI[x.sha256sum:cp312]"
# is a parse error -- so the ABI goes into the entry's name= tag and one varflag
# per ABI is declared; only the selected entry is in SRC_URI, so only its hash is
# consulted.
QWEN35_WHEEL_BASE:cp312 = "https://files.pythonhosted.org/packages/74/78/fcd41e5a0ce4f3f7b003da85825acddae6d7ecb60cf25194741b036ca7d6"
QWEN35_WHEEL_BASE:cp313 = "https://files.pythonhosted.org/packages/95/03/dc0723a013c7d7c19de5ef29e932c3081df1c14ba582b8b86b5de9db7f0f"
QWEN35_WHEEL_BASE:cp314 = "https://files.pythonhosted.org/packages/5e/a6/9ca0eecc489640615642a6cbc0ca9e10df70df38c4d43f5a928ff18d8827"

QWEN35_WHEEL_URL = "${@ d.getVar('QWEN35_WHEEL_BASE:' + (d.getVar('QWEN35_PY_ABI') or 'cp313')) }"
QWEN35_WHEEL_NAME = "numpy-${PV}-${QWEN35_PY_TAG}-manylinux_2_27_aarch64.manylinux_2_28_aarch64.whl"
SRC_URI = "${QWEN35_WHEEL_URL}/${QWEN35_WHEEL_NAME};name=wheel_${QWEN35_PY_ABI};unpack=0;subdir=${QWEN35_WHEEL_SUBDIR}"

SRC_URI[wheel_cp312.sha256sum] = "8b973c57ff8e184109db042c842423ff4f60446239bd585a5131cc47f06f789d"
SRC_URI[wheel_cp313.sha256sum] = "9c75442b2209b8470d6d5d8b1c25714270686f14c749028d2199c54e29f20b4d"
SRC_URI[wheel_cp314.sha256sum] = "727fd05b57df37dc0bcf1a27767a3d9a78cbbc92822445f32cc3436ba797337b"

# The interpreter is the only OE package this needs.  BLAS/gfortran ship inside
# the wheel (numpy.libs/), and libc comes from the rootfs.
RDEPENDS:${PN} = "python3-core"

# Doesn't provide python3-numpy on purpose (see the DESCRIPTION): poky's recipe
# is the OE-rootfs provider and RPROVIDING it here would make the choice
# ambiguous.
PROVIDES:remove = "python3-numpy"

do_install() {
	qwen35_install_wheel_tree ${D}${PYTHON_DIST_PACKAGES}

	# The core extension is ABI-tagged, so a wheel of the wrong minor installs a
	# tree that `import numpy` refuses with a confusing
	#   ModuleNotFoundError: No module named 'numpy._core._multiarray_umath'
	# (the package __init__ is fine; the extension is silently skipped).  That is
	# a runtime failure with a misleading message, so fail the build instead.
	if ! ls ${D}${PYTHON_DIST_PACKAGES}/numpy/_core/_multiarray_umath.${QWEN35_PY_EXT}-*.so >/dev/null 2>&1; then
		bbfatal "${PN}: ${QWEN35_WHEEL_NAME} has no ${QWEN35_PY_EXT} _multiarray_umath -- it will not import on the rootfs python ${QWEN35_PY_MINOR}"
	fi
	# 20 of the wheel's 20 extensions are ABI-tagged (numpy/_core, numpy/_random,
	# numpy/linalg, ... all of them).  If a foreign-tagged tree ever lands here --
	# the wrong OS dimension, or a rootfs that already shipped numpy for another
	# python -- the guard above still passes on the one file it checks while the
	# other 19 stay behind, which is how a real board ended up with a full
	# numpy-1.26.4/cp312 tree that no package owned.  Drop them all.
	qwen35_prune_foreign_abi ${D}${PYTHON_DIST_PACKAGES}/numpy
	# The BLAS/LAPACK backends live in numpy.libs and are reached through
	# $ORIGIN/../numpy.libs; a wheel whose libraries landed elsewhere imports and
	# then fails on the first dot product, which the NPU server does immediately.
	if [ ! -d ${D}${PYTHON_DIST_PACKAGES}/numpy.libs ]; then
		bbfatal "${PN}: numpy.libs/ missing -- the bundled BLAS would not be found through the RPATH"
	fi

	# Attribution: numpy's own LICENSE.txt covers numpy and the bundled
	# OpenBLAS/gfortran runtime.  cp, not install -m, is fine: the mode comes
	# with the wheel and this is documentation.
	install -d ${D}${docdir}/${PN}
	install -m 0644 ${QWEN35_WHEEL_ROOT}/numpy-${PV}.dist-info/LICENSE.txt \
		${D}${docdir}/${PN}/LICENSE.txt
	chown -R root:root ${D}${PYTHON_DIST_PACKAGES}/numpy \
		${D}${PYTHON_DIST_PACKAGES}/numpy.libs \
		${D}${PYTHON_DIST_PACKAGES}/numpy-${PV}.dist-info
}

FILES:${PN} = "${PYTHON_DIST_PACKAGES}/numpy \
               ${PYTHON_DIST_PACKAGES}/numpy.libs \
               ${PYTHON_DIST_PACKAGES}/numpy-${PV}.dist-info \
               ${docdir}/${PN} \
"

# The manylinux wheels under numpy.libs/ are unversioned SONAMEs with no -dev
# package to move them to (same story as the other qwen35 wheels).
#
# staticdev:   the wheel ships numpy/_core/lib/libnpymath.a and the .a form of its
#              BLAS stubs -- they are what `numpy.get_include()`/distutils expect
#              when something builds an extension against this numpy, so they
#              belong in the runtime package.  There is no -dev split here.
# host-user-contaminated: qwen35_install_wheel_tree copies the unpacked wheel with
#              cp -a, which keeps the wheel's recorded uid/gid (1000).  Re-owning
#              in do_install would be the alternative; skipping is what the other
#              wheel packaging in this layer does (qai-appbuilder chowns, the
#              qwen35 wheels do not) and the files are plain data.
INSANE_SKIP:${PN} += "dev-so staticdev host-user-contaminated"

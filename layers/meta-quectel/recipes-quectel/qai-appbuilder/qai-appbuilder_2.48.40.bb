SUMMARY = "QAI AppBuilder (Quick AI Application Builder) - Qualcomm NPU inference bindings for Python"
DESCRIPTION = "Prebuilt manylinux aarch64 wheel of qai_appbuilder. Loads QNN context \
binaries / DLC models on the Hexagon NPU (HTP/CPU/GPU) and exposes Python APIs for \
on-device AI apps. The wheel bundles the QAIRT runtime it needs (libQnnHtp*.so, the \
Hexagon skels in libs/, libGenie.so, libappbuilder.so), so no QAIRT SDK install is \
required at runtime."
HOMEPAGE = "https://github.com/qualcomm/qai-appbuilder"
LICENSE = "BSD-3-Clause"
# Same BSD-3-Clause text in every wheel of this release, so one checksum covers
# both the cp312 (LINUX) and cp313 (DEBIAN) variants below.
LIC_FILES_CHKSUM = "file://qai_appbuilder-${PV}.dist-info/licenses/LICENSE;md5=3d73035ac3b78bacc2fa00f27073ad9d"

# Gives PYTHON_DIR / PYTHON_SITEPACKAGES_DIR (= /usr/lib/python3.12/site-packages
# here). Pure variable definitions -- no DEPENDS, no tasks -- so it is cheaper
# than python3native/python3targetconfig, which we don't need since nothing is
# compiled or executed at build time.
inherit python3-dir

# ---------------------------------------------------------------------------
# System dimension (LINUX vs DEBIAN/UBUNTU)
#
# buildconfig writes SKIP_DEPLOY_DEBIAN_GNOME_ROOTFS = "1" into local.conf for
# LINUX and deletes the line for DEBIAN/UBUNTU (quectel_build/compile/build.sh,
# the OS_DIM branch). That is the ONLY place the system dimension reaches
# bitbake, so it is the switch here too -- do not invent a second flag.
#
#   LINUX       -> OE-only rootfs, python3 3.12.12, packages go to
#                  /usr/lib/python3.12/site-packages (${PYTHON_SITEPACKAGES_DIR})
#   DEBIAN      -> Debian trixie overlay on top, python3 -> python3.13, and
#                  Debian site.py states "/usr/lib/python3/<v>/site-packages is
#                  not used"; Debian packages/search /usr/lib/python3/dist-packages.
#
# The wheel is ABI-specific, so the two variants need different artifacts (and
# therefore different sha256sums):
#   cp312-cp312-manylinux_2_39_aarch64.whl   03d8b034e01e6fc984f76f62af1727237e96723101ad5a81c328ff51b30420ed
#   cp313-cp313-manylinux_2_39_aarch64.whl   8f057bf90b8ec0cdc3406372495192643886401f5c8215cff9714c45c6b0f9b7
#
# glibc: the manylinux_2_39 tag overstates it -- the wheels' highest requirement
# is GLIBC_2.38 (readelf -V), and poky here ships glibc 2.39, so both load on a
# pure LINUX rootfs.
#
# UBUNTU is NOT covered: buildconfig's UBUNTU variant overlays
# ubuntu26-gnome-rootfs, whose python3 is 3.14, and upstream publishes no
# cp314 aarch64 wheel. Building UBUNTU with this recipe would install a wheel
# that cannot import. Either skip it in the UBUNTU sync list or build the wheel
# from source against 3.14.
# ---------------------------------------------------------------------------
QAIB_DEBIAN = "${@bb.utils.contains('SKIP_DEPLOY_DEBIAN_GNOME_ROOTFS','1','0','1',d)}"
QAIB_ABI = "${@ 'cp313' if d.getVar('QAIB_DEBIAN') == '1' else 'cp312' }"
QAIB_PYDIR = "${@ '/usr/lib/python3/dist-packages' if d.getVar('QAIB_DEBIAN') == '1' else d.getVar('PYTHON_SITEPACKAGES_DIR') }"

# Renaming the .whl to .zip in SRC_URI lets bitbake's unpack step treat it as the
# zip it is; it also gives LIC_FILES_CHKSUM a real file to hash.
# The ABI must be in the download name: both variants are served under identical
# upstream names differing only in the python/abi tag, but they unpack to the same
# paths and carry different sha256sums. A single downloadfilename would make
# LINUX and DEBIAN share one DL_DIR entry (and one shared-protected mirror entry),
# so whichever variant fetched second would hit a checksum mismatch. Keep them
# apart: qai_appbuilder-2.48.40-cp312.zip / -cp313.zip.
WHEEL = "qai_appbuilder-${PV}-${QAIB_ABI}-${QAIB_ABI}-manylinux_2_39_aarch64.whl"
SRC_URI = "https://github.com/qualcomm/qai-appbuilder/releases/download/v${PV}/${WHEEL};downloadfilename=qai_appbuilder-${PV}-${QAIB_ABI}.zip"
SRC_URI[sha256sum] = "${@ '8f057bf90b8ec0cdc3406372495192643886401f5c8215cff9714c45c6b0f9b7' if d.getVar('QAIB_DEBIAN') == '1' else '03d8b034e01e6fc984f76f62af1727237e96723101ad5a81c328ff51b30420ed' }"

S = "${WORKDIR}"

# numpy is required by qnncontext.py (and pyyaml by onnxwrapper.py).
# LINUX: hard deps -- the OE recipes are the only provider, and they install into
#        the same 3.12 site-packages the wheel lands in.
# DEBIAN: soft deps -- this is the hybrid rootfs, where OE's python3-numpy would
#        land in 3.12/site-packages while the device python3 is 3.13; Debian's own
#        python3-numpy 2.2.4 (in dist-packages) is what actually satisfies the
#        wheel. Same reasoning as qcom-npu-test_1.0.bb.
RDEPENDS:${PN} = "python3 ${@ 'python3-numpy python3-pyyaml' if d.getVar('QAIB_DEBIAN') == '0' else '' }"
RRECOMMENDS:${PN} = "${@ 'python3-numpy python3-pyyaml' if d.getVar('QAIB_DEBIAN') == '1' else '' }"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    install -d ${D}${QAIB_PYDIR}

    # Unpacking the wheel at the site-dir root reproduces the layout pip would
    # create: qai_appbuilder/ (with libs/), qai_appbuilder.libs/ and the
    # dist-info. The ext modules resolve their siblings via RUNPATH=$ORIGIN,
    # so this exact layout matters -- do not flatten it.
    cp -a ${S}/qai_appbuilder ${S}/qai_appbuilder.libs ${S}/qai_appbuilder-${PV}.dist-info ${D}${QAIB_PYDIR}/

    # posix_spawnp launches QAIAppSvc for cross-process inference; the import
    # path restores +x if missing, but set it here so the packaged file is right.
    chmod 0755 ${D}${QAIB_PYDIR}/qai_appbuilder/QAIAppSvc

    # cp -a keeps the wheel's uid/gid (1000), which would trip the
    # host-user-contaminated QA check.
    chown -R root:root ${D}${QAIB_PYDIR}
}

FILES:${PN} = "${QAIB_PYDIR}"

# Prebuilt third-party binaries: already stripped, .so files outside libdir,
# $ORIGIN-relative rpaths, and rdeps that live in the same wheel dir.
#
# arch/ldflags: libs/libQnnHtpV{68,73,75,79}Skel.so are NOT aarch64 -- they are
# Hexagon DSP6 images (ELF32, e_machine 164, SYSV hash, no GNU_HASH) loaded by
# the HTP runtime on the NPU. arch expects e_machine == target and ldflags
# expects GNU_HASH, so both checks are meaningless for the skels (and the
# rest of the package is verified-correct prebuilt aarch64 from upstream).
INSANE_SKIP:${PN} += "already-stripped libdir dev-so file-rdeps rpath arch ldflags"

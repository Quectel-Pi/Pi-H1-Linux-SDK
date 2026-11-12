# quectel-os-dim.inc -- the OS dimension (LINUX / DEBIAN / UBUNTU) as seen by
# recipes.
#
# quectel_build/compile/build.sh writes
#     QUECTEL_OS_DIM = "LINUX|DEBIAN|UBUNTU"
# into local.conf.  Until this file existed, that dimension only reached one
# control knob: SKIP_DEPLOY_DEBIAN_GNOME_ROOTFS (1 for LINUX, 0 otherwise).  That
# is enough to choose *whether* a rootfs is overlaid, but not *which* one -- and
# DEBIAN and UBUNTU overlay two different samples with two different python
# minor versions:
#
#   LINUX    no overlay; OE rootfs,        /usr/bin/python3 -> 3.12  site-packages
#   DEBIAN   prebuild/debian-gnome-rootfs   -> 3.13   dist-packages
#   UBUNTU   prebuild/ubuntu26-gnome-rootfs -> 3.14   dist-packages
#
# Every prebuilt python artifact in this layer is ABI-tagged (onnxruntime,
# numpy, pillow, sherpa-onnx; tokenizers is cp39-abi3 and therefore portable),
# and an interpreter silently *skips* an extension whose tag it does not
# recognise: the file is right there and the import still fails with
# ModuleNotFoundError.  That is not a hypothetical -- it is how qwen3.5 (a cp313
# onnxruntime against a 3.14 interpreter) and npu-chat's voice features (a cp313
# sherpa-onnx) were broken on the Ubuntu image while looking perfectly installed.
#
# So the mapping lives here, once, and recipes derive from it instead of each
# carrying its own copy.

QUECTEL_OS_DIM ??= "LINUX"

# python ABI tag of the rootfs interpreter: cp312 | cp313 | cp314
QUECTEL_PY_ABI ??= "${@ {'LINUX':'cp312', 'DEBIAN':'cp313', 'UBUNTU':'cp314'}.get(d.getVar('QUECTEL_OS_DIM'), 'cp312') }"

# Interpreter minor version, for error messages and for guards that want to say
# "this wheel will not load on python X.Y".
QUECTEL_PY_MINOR ??= "${@ {'cp312':'3.12', 'cp313':'3.13', 'cp314':'3.14'}.get(d.getVar('QUECTEL_PY_ABI'), '3.12') }"

# The extension-module tag as it appears *inside* a wheel: the ABI tag cp313
# becomes '.cpython-313-aarch64-linux-gnu.so', not '.cp313-...'.  Guards that
# stat the installed file must use this; the cpXXX tag is only for the filename
# of the wheel itself and reads naturally in messages.
QUECTEL_PY_EXT ??= "cpython-${@ d.getVar('QUECTEL_PY_ABI').split('cp')[-1] }"

# Directory a wheel has to install into so that interpreter sees it.
#   LINUX:  ${PYTHON_SITEPACKAGES_DIR} -> /usr/lib/python3.12/site-packages
#   others: /usr/lib/python3/dist-packages, which is the one path Debian's
#           site.py adds for *every* 3.x minor (3.13 and 3.14 both read it).
#           Installing into /usr/lib/python3.1X/site-packages would be invisible
#           twice over: Debian's site.py says that directory is not used.
QUECTEL_PY_DEST ??= "${@ d.getVar('PYTHON_SITEPACKAGES_DIR') if d.getVar('QUECTEL_OS_DIM') == 'LINUX' else '/usr/lib/python3/dist-packages' }"

# python modules the overlaid sample rootfs already carries for its own
# interpreter.  Installing our own copy of one of these puts two versions of the
# same module in one directory (the overlay rsync runs with --inplace but without
# --delete, so Debian's files win and our extra ones -- numpy.libs/,
# pillow.libs/ -- survive as orphans).  Verified against the two tarballs:
#   DEBIAN 13:  dist-packages/numpy + numpy-2.2.4.dist-info  (no PIL)
#   UBUNTU 26:  dist-packages/PIL   + pillow-12.1.1.egg-info  (no numpy)
#   LINUX:      OE rootfs provides poky's python3-numpy in the 3.12 site-packages
QUECTEL_ROOTFS_PYHAS ??= "${@ {'LINUX':'numpy', 'DEBIAN':'numpy', 'UBUNTU':'pillow'}.get(d.getVar('QUECTEL_OS_DIM'), '') }"

# True when the sample rootfs already provides module $1.
quectel_rootfs_provides() {
	case " ${QUECTEL_ROOTFS_PYHAS} " in
		*" $1 "*) return 0 ;;
	esac
	return 1
}

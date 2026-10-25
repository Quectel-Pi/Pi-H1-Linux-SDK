# DEPENDS_NOTES — what `qwen35-npu` needs from onnxruntime, and how to get it

Status: **investigation + draft recipes, not merged recipes.** No `bitbake` was run
while producing this (per the task rules); every number below comes from PyPI
metadata, from the wheels themselves, or from a file on this machine, and each
claim says which.

Owner of the two `.bb` files that consume this: `qwen35-npu_1.0.bb` (engine + python
runtime) and `qwen35-npu-model_1.0.bb` (the 1.8 GB assets).

---

## 1. Summary / decision

* The engine and the python runtime need **library version 1.27.0 exactly**, not
  "some onnxruntime": the engine is compiled against the 1.27.0 C API headers
  (`Ort::ConstEpDevice`, `RegisterExecutionProviderLibrary`,
  `AppendExecutionProvider_V2`) and the QNN execution provider is a **separate
  plugin library** (`libonnxruntime_providers_qnn.so`) that is only ABI-compatible
  with the core it was built for. Core and plugin must come from the same release
  pair, which is what upstream pins: `onnxruntime==1.27.0` +
  `onnxruntime-qnn==2.3.0` (`/tmp/rdq617/requirements-device.txt`,
  `assets/runtime-lock.json`).
* **The board's python is Debian 3.13, not Yocto's python 3.12.** So a normal
  `python3-onnxruntime` Yocto recipe would install into
  `/usr/lib/python3.12/site-packages`, which the board's `/usr/bin/python3` never
  looks at. Everything python here has to land in **`/usr/lib/python3/dist-packages`**
  as a cp313 (or abi3) manylinux wheel.
* Recommended route: **two thin wheel recipes** (`onnxruntime`,
  `onnxruntime-qnn`) that unpack PyPI wheels and stage the pieces our recipe
  compiles against. Route 2 (build ORT from source) is a multi-hour build that
  still cannot produce `libonnxruntime_providers_qnn.so` (that lives in the
  separate QNN wheel). Details in §5.
* Sizes: 95 MB of wheels for those two, ~132 MB including the four other python
  wheels the runtime imports; on the board it is ~23 MB of QNN libraries + 20 MB
  of ORT (only the v68 subset of the QNN wheel is kept — the full wheel carries
  skel/stub for V69/V73/V75/V79/V81 and an 88 MB `libQnnHtpPrepare.so` that the
  prebuilt-context flow never loads).

---

## 2. What actually needs it (evidence, not assumption)

| consumer | file:line | needs |
|---|---|---|
| engine (C++) | `native/qnn_memory.cpp` — `RegisterExecutionProviderLibrary("QNNExecutionProvider", args.plugin)`, `GetEpDevices()`, `AppendExecutionProvider_V2(...)` with allocator `QnnHtpShared` | `libonnxruntime.so.1` (1.27 ABI) + `libonnxruntime_providers_qnn.so` + `libQnnHtp.so` + `libQnnSystem.so` + `libQnnHtpV68Stub.so` |
| engine (C++) | `native/qwen_text_engine.cpp` → `#include "qnn_memory.cpp"` (one TU), and `native/qnn_memory.cpp:23` → `#include "onnxruntime_cxx_api.h"` (quoted, so it resolves through the recipe's `-I`, which points at `${STAGING_INCDIR}` or, since the wheel ships no headers, at `files/native/ort_include/`) | the 5 ORT headers at exactly 1.27.0 |
| prefill worker (py) | `runtime/chunk_prefill.py:19` `import onnxruntime as ort`; `:90` `onnx.load(...)`, `:91` `onnx.checker.check_model(...)`; `:1062` `import onnxruntime_qnn as oq`; `:1069` `ort.register_execution_provider_library(oq.get_ep_name(), oq.get_library_path())` | `onnxruntime`, `onnx`, **and the `onnxruntime_qnn` python module** |
| vision frontend (py) | `runtime/vision_runtime.py:134` `import onnxruntime_qnn as oq`, `:141` `ort.register_execution_provider_library(...)` | same |
| tokenizer/vision (py) | `runtime/qwen_server.py` `from tokenizers import Tokenizer`; `runtime/image_frontend.py` `from PIL import Image, ImageOps` | `tokenizers`, `pillow` |

Two consequences that the first version of the task brief did not account for:

1. **`onnxruntime_qnn` is not optional python sugar.** Two runtime modules import
   it and call `get_library_path()` / `get_qnn_htp_path()`. That function is
   `os.path.join(os.path.dirname(os.path.abspath(__file__)), "libonnxruntime_providers_qnn.so")`
   (read from the wheel's `onnxruntime_qnn/__init__.py`, 2.3.0), i.e. **the plugin
   `.so` must sit in the same directory as the module**, or that call hands ORT a
   path that does not exist. `qwen35-mkconfig.py` already tries
   `import onnxruntime_qnn` first for exactly this reason.
2. `libonnxruntime_providers_qnn.so` has `RUNPATH=$ORIGIN`
   (`readelf -d` on the wheel's copy), so `libQnnHtp*.so`/`libQnnSystem.so` must be
   in **that same directory** too — the QNN libraries and the python module are one
   unit, they cannot be split between `/usr/lib/qwen35/qnn` and dist-packages.

That is why the draft recipes in §6 install the wheel tree verbatim into
`/usr/lib/python3/dist-packages/onnxruntime_qnn/` and create
`/usr/lib/qwen35/qnn` as a **symlink to that directory**: `files/qwen35-launch.sh`
hardcodes `QNN_DIR=$LIB_DIR/qnn` for `LD_LIBRARY_PATH`/`ADSP_LIBRARY_PATH` and
`qwen35.service` sets `QWEN35_QNN_DIR=/usr/lib/qwen35/qnn`, so the symlink keeps
both contracts without touching either file.

---

## 3. Hard constraints on the board (checked against `prebuild/debian-gnome-rootfs`)

| constraint | value | impact |
|---|---|---|
| python | **3.13** (`usr/lib/python3.13`), `sys.path` = `/usr/lib/python3/dist-packages` + `/usr/lib/python3.13` | need cp313 or abi3 wheels; never Yocto's `/usr/lib/python3.12/site-packages` |
| glibc | **2.41** (`strings` on `lib/aarch64-linux-gnu/libc.so.6` → "Debian GLIBC 2.41-12+deb13u4") | every manylinux tag up to `2_34`/`2_28` is fine; the QNN wheel's `manylinux_2_34` is fine (Debian 13 = 2.41) |
| libstdc++ | from Debian, ≥ GLIBCXX needed by 1.27 wheel | prebuilt binaries need nothing extra |
| numpy | **2.2.4** present (`dist-packages/numpy-2.2.4.dist-info`) | upstream pins 2.5.0; ORT needs `>=1.21.6`, onnx `>=1.23.2`, ml_dtypes `>=1.21` → 2.2.4 satisfies all three. Keep Debian's, do **not** add a second numpy |
| packaging | **25.0** present | ORT `Requires-Dist: packaging` satisfied |
| missing | `tokenizers`, `onnx`, `onnxruntime`, `protobuf`, `flatbuffers`, `typing_extensions`, `ml_dtypes`, `PIL` | all must come from wheels |
| already there, but wrong | `usr/lib/aarch64-linux-gnu/libonnxruntime.so.1.21{,.0}` (Debian `libonnxruntime1.21`, C++ lib only, no python) | **SONAME collision** — see §4 |
| `libcdsprpc.so` | not in the Debian rootfs | comes from the Yocto `fastrpc` package (`layers/meta-quectel/recipes-multimedia/fastrpc/fastrpc_15.0.qcom.bbappend`, already enabled `adsprpcd`/`cdsprpcd`). `RDEPENDS` on `fastrpc` belongs in the final recipe |

### The `.so.1` collision with Debian's `libonnxruntime1.21`

`readelf -d` on the 1.27.0 wheel's core:

```
SONAME = libonnxruntime.so.1
RUNPATH = $ORIGIN
```

Same SONAME as Debian's 1.21. Therefore:

* the engine **must** carry an rpath to its own copy (`-Wl,-rpath,'$ORIGIN/lib'`,
  which `qwen35-npu_1.0.bb` does) — `DT_RUNPATH` is consulted before
  `/etc/ld.so.cache`, so 1.27.0 wins even though 1.21 is in the default path;
* `/usr/lib/qwen35/lib/libonnxruntime.so.1` must exist for the loader, because the
  wheel ships **only** the versioned file (verified: no `.so.1` symlink entry in the
  wheel; only `libonnxruntime.so.1.27.0`). Upstream's `install.py` created it and
  verified the sha256 against `assets/runtime-lock.json`;
* the wheel's `onnxruntime_pybind11_state.cpython-313-*.so` has **no** `NEEDED` on
  `libonnxruntime.so.1` — it loads the versioned file by name from its own
  directory (`RUNPATH=$ORIGIN`). So the python module must keep
  `libonnxruntime.so.1.27.0` **inside** `onnxruntime/capi/`.

Copy or symlink? Upstream copies it (its `runtime-lock.json` lists
`libonnxruntime.so.1.27.0` as an installed binary with its own sha256
`9275ef52956228686ae47b0219f67476a1148505084bdc5571e7bf1bf69b11f2`, 20 001 880 B —
the exact size in the wheel). **Recommendation: copy** (20 MB) — a relative symlink
into another package's dist-packages directory is one `IMAGE_INSTALL:remove` away
from a dangling `libonnxruntime.so.1`, and the failure mode is a loader error at
service start.

---

## 4. The wheels: URL, size, sha256

All eight are `aarch64`; the two ORT ones were **downloaded here and hashed
locally** (`sha256sum` matches PyPI's `digests.sha256` byte for byte), the rest are
PyPI metadata read with `https://pypi.org/pypi/<pkg>/<ver>/json`.

### Required by this recipe (task scope)

| package | version | wheel | bytes | sha256 |
|---|---|---|---|---|
| onnxruntime | 1.27.0 | `onnxruntime-1.27.0-cp313-cp313-manylinux_2_27_aarch64.manylinux_2_28_aarch64.whl` | 16 438 180 | `48b3d87eb560ff6a772240506f3c78d6d27c63cafedd5c775672e1194f968cfd` |
| onnxruntime-qnn | 2.3.0 | `onnxruntime_qnn-2.3.0-cp313-cp313-manylinux_2_34_aarch64.whl` | 78 730 222 | `382e31b7991a7a1003be636a0a97e888d3b95ffadef2162bdece74b5106f0aef` |

```
https://files.pythonhosted.org/packages/ce/88/24fc51fcbb126da6d032372314e47b55c3faad58f2aa78c0e199ccd20b9c/onnxruntime-1.27.0-cp313-cp313-manylinux_2_27_aarch64.manylinux_2_28_aarch64.whl
https://files.pythonhosted.org/packages/a0/31/ffce5b1530cb541c49da16cadc2cd94774eeb660bcf5c66be97dc77ee613/onnxruntime_qnn-2.3.0-cp313-cp313-manylinux_2_34_aarch64.whl
```

Verified locally as well: extracting the five QNN binaries from the 2.3.0 wheel and
hashing them reproduces upstream's `assets/runtime-lock.json` **exactly**:

```
2 917 120  libonnxruntime_providers_qnn.so   ebcec5c0b52cc4bb96542accd17f4a410174bfc7a44599586f17852aa9ae78ef
4 267 144  libQnnHtp.so                       9e1a73ed4f3e7cf3ef3199a23dfb3885b12a64edea30e607b0687b25b28e94f7
5 364 936  libQnnSystem.so                    1dcc4e535f94b7d921d5c294eafc06bc0c87968077e5ee36bb14538820ef6990
  473 160  libQnnHtpV68Stub.so                68e4c5f932ea006efa311c6763a4b25081f11663ae70390e6a58e41142f82c9f
10 026 512  libQnnHtpV68Skel.so               1c4ef2cec89209ffb32d670cde10c3ff66733e44d34dd23c0b76f319afc5a6dd
```

and the five headers vendored in `files/native/ort_include/` are the 1.27.0 set
from the same lock (`c19f266a… c_api`, `3903cb4b… cxx_api`, `2251187e… cxx_inline`,
`5520793… ep_c_api`, `9b45e1b… float16` — hashed locally, all match). That is why
`qwen35-npu_1.0.bb` can compile without a headers package.

### Also needed at runtime (other recipes, same mechanism)

`onnx` and `tokenizers` ship **abi3** wheels, so they do not care about cp313:

| package | version | wheel | bytes | sha256 |
|---|---|---|---|---|
| onnx | 1.22.0 | `onnx-1.22.0-cp312-abi3-manylinux_2_26_aarch64.manylinux_2_28_aarch64.whl` | 18 889 249 | `ae5a563f281cd9d2845622cecf6c092a57e4ee1b138f66fdbbdd4200567a5e16` |
| tokenizers | 0.22.2 | `tokenizers-0.22.2-cp39-abi3-manylinux_2_17_aarch64.manylinux2014_aarch64.whl` | 3 290 736 | `2249487018adec45d6e3554c71d46eb39fa8ea67156c640f7513eb26f318cec7` |
| pillow | 12.1.0 | `pillow-12.1.0-cp313-cp313-manylinux_2_27_aarch64.manylinux_2_28_aarch64.whl` | 6 346 509 | `e115c15e3bc727b1ca3e641a909f77f8ca72a64fff150f666fcc85e57701c26c` |
| ml_dtypes | 0.5.4 | `ml_dtypes-0.5.4-cp313-cp313-manylinux_2_27_aarch64.manylinux_2_28_aarch64.whl` | 5 036 993 | `ce756d3a10d0c4067172804c9cc276ba9cc0ff47af9078ad439b075d1abdc29b` |
| protobuf | 7.35.1 | `protobuf-7.35.1-cp310-abi3-manylinux2014_aarch64.whl` | 328 847 | `11d6b0ec246892d85215b0a13ca6e0233cf5284b68f0ac02646427f4ff88a799` |
| typing_extensions | 4.15.0 | `typing_extensions-4.15.0-py3-none-any.whl` | 44 614 | `f0fa19c6845758ab08074a0cfa8b7aecb71c999ca73d62883bc25cc018c4e548` |

(URL paths: fetch them from the JSON above; the ORT two are spelled out because
they are the ones this document is about. `protobuf` also has a
`py3-none-any` variant — the abi3 upb one is 10× faster at parsing the .onnx
context files and 187 KB more.)

Not needed, despite the metadata: `flatbuffers`, `coloredlogs`, `humanfriendly`,
`sympy`/`mpmath`, `huggingface-hub`. They are `Requires-Dist` of the wheels but are
only imported by quantization/tooling paths that this image never executes
(`grep` of `files/runtime/*.py` shows no import of any of them). Install them only
if a build-time or board-side traceback asks for one; that is a 2-line change, and
adding `sympy` + `mpmath` + `coloredlogs` + `humanfriendly` preemptively means four
more recipes for nothing.

---

## 5. The three routes

### Route 1 (recommended) — thin wheel recipes in this layer

`recipes-quectel/onnxruntime/onnxruntime_1.27.0.bb` and
`recipes-quectel/onnxruntime/onnxruntime-qnn_2.3.0.bb`, ~40 lines each, drafted in
§6. They unpack the wheel into `/usr/lib/python3/dist-packages`, keep the QNN
libraries next to the python module, and additionally place
`/usr/lib/qwen35/lib/libonnxruntime.so{,.1}` so `qwen35-npu` can link against a
target library that the normal sysroot staging picks up.

* **+** reproducible (sha256 per wheel), no compiler needed, 60 s build,
  correct 1.27.0/2.3.0 pairing, no source-level risk.
* **+** works with the Debian 3.13 python, which no src recipe can (a Yocto python
  module is built against 3.12).
* **−** prebuilt binaries: no debug symbols, `already-stripped` QA needs
  `INSANE_SKIP` (drafts do it), licence files must be shipped from the wheel
  (`onnxruntime-1.27.0.dist-info/LICENSE`).
* **−** two new recipe names in the layer; ~95 MB in `DL_DIR` (sstate does not
  carry the wheel, `DL_DIR` does — a fresh builder re-downloads once).

### Route 2 — src recipes (upstream `onnxruntime` from `meta-python`, or a local one)

`meta-python` in this checkout has **no** onnxruntime recipe at all
(`grep -rl onnxruntime layers/meta-openembedded/meta-python` → nothing), and
neither does poky; `meta-tensorflow` exists but carries tensorflow-lite, not ORT.
So this route means writing an ORT-from-source recipe: cmake, ~2 000 files, 30-90
min of build per clean sysroot, plus a protobuf/flatbuffers/abseil dependency
chain, **and it still would not give us the QNN plugin** — `providers_qnn` is not
in the ORT tarball, it is shipped only as a prebuilt binary in the
`onnxruntime-qnn` wheel, built against one specific ORT. So route 2 costs a day and
ends where route 1 starts.

### Route 3 — sstate / prebuilt artifact

Push the unpacked tree into `SSTATE_MIRRORS` or a `file://` tarball in the layer.
Rejected: a 95 MB tarball in git (or in an unversioned share) is the same bytes as
the wheel but without the provenance, and the wheel URL + sha256 *is* the audit
trail. The only variant worth keeping in mind: an air-gapped builder can put the
eight wheels in a local directory and set
`SRC_URI = "file:///${QWEN35_WHEEL_DIR}/…;unpack=0"` — the draft recipes keep the
URL/sha256 in one table, so that is a two-line override
(`QWEN35_PYPI_MIRROR ??= "https://files.pythonhosted.org/packages"`).

---

## 6. Draft recipes

Not installed in the layer yet (the task said "notes, not a recipe"); they are
written so they can be dropped in as-is. Shared assumptions:

* one include holds the knobs, `recipes-quectel/onnxruntime/qwen35-wheel.inc`;
* `PYTHON_DIST_PACKAGES = "/usr/lib/python3/dist-packages"` — deliberately **not**
  `${PYTHON_SITEPACKAGES_DIR}` (that is `/usr/lib/python3.12/site-packages`, which
  the board's Debian 3.13 python does not search);
* `python3 -m zipfile -e <whl> <dir>` for extraction (same tool the old CPU recipe
  used; `pip` is not needed and must not be, it would try to resolve deps);
* `unpack=0` on the `SRC_URI` entry: bitbake would otherwise try `unzip` and lose
  the wheel's file modes / `dist-info`.

```bitbake
# recipes-quectel/onnxruntime/qwen35-wheel.inc
# Shared install rules for the prebuilt manylinux wheels that the Qwen3.5 NPU
# runtime needs.  Replaces the old inline ORT_WHEEL block in qwen35-npu_1.0.bb.
#
# The board python is Debian 3.13 (prebuild/debian-gnome-rootfs), so target
# modules go to /usr/lib/python3/dist-packages and NOT to
# ${PYTHON_SITEPACKAGES_DIR} (= /usr/lib/python3.12/site-packages, invisible to
# that interpreter).  See DEPENDS_NOTES.md §3.
PYTHON_DIST_PACKAGES = "/usr/lib/python3/dist-packages"

# Where the QNN EP + HTP libraries and the onnxruntime_qnn python module live.
# They have to share one directory: onnxruntime_qnn/__init__.py returns
# dirname(__file__) + "/libonnxruntime_providers_qnn.so", and that .so has
# RUNPATH=$ORIGIN for libQnnHtp.so.  /usr/lib/qwen35/qnn is a symlink to it so
# that files/qwen35-launch.sh (QNN_DIR=$LIB_DIR/qnn) and qwen35.service
# (QWEN35_QNN_DIR) keep working unchanged.
QWEN35_QNN_PYDIR = "${PYTHON_DIST_PACKAGES}/onnxruntime_qnn"
QWEN35_ORT_LIBDIR = "${libdir}/qwen35/lib"

# Prebuilt manylinux binaries ship stripped and the QA check is fatal by default;
# there are no sources to rebuild them from here.
INSANE_SKIP:${PN} += "already-stripped"
# /usr/lib/python3/dist-packages is not a path oe-core knows about; do not let
# the "installed-vs-shipped" / libdir checks complain about a wheel layout.
ERROR_QA:remove = "libdir"
WARN_QA:remove = "libdir"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

# Extract a wheel into a staging dir.  Wheels are zip files; RECORD/RPATH are all
# relative to the package root, so extracting at the site-packages root is what
# pip does too.  A `.data/` directory would need relocating (none of ours has
# one) — fail loudly instead of silently installing a broken tree.
qwen35_unpack_wheel() {
	local whl="$1" dest="$2"
	install -d "${dest}"
	python3 -m zipfile -e "${whl}" "${dest}"
	if [ -d "${dest}"/*.data ]; then
		bbfatal "qwen35: ${whl} uses a .data/ directory, needs relocation support"
	fi
}
```

```bitbake
# recipes-quectel/onnxruntime/onnxruntime_1.27.0.bb
SUMMARY = "ONNX Runtime 1.27.0 C/C++ core + python bindings (prebuilt aarch64 wheel)"
DESCRIPTION = "Core library and cp313 python bindings for ONNX Runtime 1.27.0. \
\
The board runs a precompiled QNN EPContext decoder, and the execution provider \
plugin (onnxruntime-qnn 2.3.0) is only ABI-compatible with the core release it \
was built against, so this is a pinned prebuilt wheel rather than a source build: \
upstream ships providers_qnn only as a binary, built against exactly 1.27.0. \
\
Installs \
  /usr/lib/python3/dist-packages/onnxruntime/  python module + \
      capi/libonnxruntime.so.1.27.0 (the module loads that file by name from its \
      own directory, RUNPATH=$ORIGIN) \
  /usr/lib/qwen35/lib/libonnxruntime.so.1.27.0 + .so/.so.1 symlinks \
      the copies qwen35-npu links against with -Wl,-rpath,$ORIGIN/lib.  The copy \
      is deliberate: Debian 13 already ships libonnxruntime.so.1.21 (SONAME \
      libonnxruntime.so.1), and the engine's DT_RUNPATH has to win over it, which \
      only works if the file is really there. See DEPENDS_NOTES.md."
HOMEPAGE = "https://github.com/microsoft/onnxruntime"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit python3native
require qwen35-wheel.inc

QWEN35_WHEEL = "onnxruntime-1.27.0-cp313-cp313-manylinux_2_27_aarch64.manylinux_2_28_aarch64.whl"
QWEN35_WHEEL_URL = "https://files.pythonhosted.org/packages/ce/88/24fc51fcbb126da6d032372314e47b55c3faad58f2aa78c0e199ccd20b9c"

SRC_URI = "${QWEN35_WHEEL_URL}/${QWEN35_WHEEL};name=ortwheel;unpack=0"
SRC_URI[ortwheel.sha256sum] = "48b3d87eb560ff6a772240506f3c78d6d27c63cafedd5c775672e1194f968cfd"

S = "${WORKDIR}"
# Only numpy/packaging are imported by the module at load time and the Debian
# rootfs already has both (2.2.4 / 25.0).
RDEPENDS:${PN} = "python3-core python3-numpy"

do_install() {
	install -d ${D}${PYTHON_DIST_PACKAGES} ${D}${QWEN35_ORT_LIBDIR}
	qwen35_unpack_wheel "${WORKDIR}/${QWEN35_WHEEL}" "${D}${PYTHON_DIST_PACKAGES}"

	# The engine links the versioned file; the loader then asks for
	# libonnxruntime.so.1 because that is the SONAME.  Both names must exist.
	install -m 0755 ${D}${PYTHON_DIST_PACKAGES}/onnxruntime/capi/libonnxruntime.so.1.27.0 \
		${D}${QWEN35_ORT_LIBDIR}/libonnxruntime.so.1.27.0
	ln -sf libonnxruntime.so.1.27.0 ${D}${QWEN35_ORT_LIBDIR}/libonnxruntime.so.1
	ln -sf libonnxruntime.so.1.27.0 ${D}${QWEN35_ORT_LIBDIR}/libonnxruntime.so

	install -m 0644 ${D}${PYTHON_DIST_PACKAGES}/onnxruntime-${PV}.dist-info/LICENSE \
		${D}${docdir}/onnxruntime/ 2>/dev/null || \
		bbwarn "onnxruntime: no LICENSE in the wheel dist-info"
}

FILES:${PN} = "${PYTHON_DIST_PACKAGES}/onnxruntime \
               ${PYTHON_DIST_PACKAGES}/onnxruntime-${PV}.dist-info \
               ${QWEN35_ORT_LIBDIR} \
               ${docdir}/onnxruntime"
```

```bitbake
# recipes-quectel/onnxruntime/onnxruntime-qnn_2.3.0.bb
SUMMARY = "ONNX Runtime QNN execution provider plugin + Qualcomm AI RT libraries (prebuilt)"
DESCRIPTION = "The QNNExecutionProvider plugin (2.3.0, Qualcomm AI Runtime 2.47.0) \
for ONNX Runtime 1.27.0, plus the python module that reports its paths. \
\
Only the Hexagon V68 subset is installed: libonnxruntime_providers_qnn.so, \
libQnnHtp.so, libQnnSystem.so, libQnnHtpV68Stub.so, libQnnHtpV68Skel.so \
(the same five files upstream's assets/runtime-lock.json pins, sha256 identical). \
The wheel also carries V69/V73/V75/V79/V81 skel+stub (11-12 MB each) and an \
88 MB libQnnHtpPrepare.so, which is only used to *generate* context binaries on \
the device -- the model package ships precompiled ones, so none of that travels. \
\
Layout is fixed by the plugin, not by us: onnxruntime_qnn/__init__.py returns \
dirname(__file__)+'/libonnxruntime_providers_qnn.so' and the plugin has \
RUNPATH=$ORIGIN, so module + libraries share \
/usr/lib/python3/dist-packages/onnxruntime_qnn/.  /usr/lib/qwen35/qnn is a \
symlink to it (launch.sh: QNN_DIR=$LIB_DIR/qnn, service: QWEN35_QNN_DIR)."
HOMEPAGE = "https://pypi.org/project/onnxruntime-qnn/"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit python3native
require qwen35-wheel.inc

QWEN35_WHEEL = "onnxruntime_qnn-2.3.0-cp313-cp313-manylinux_2_34_aarch64.whl"
QWEN35_WHEEL_URL = "https://files.pythonhosted.org/packages/a0/31/ffce5b1530cb541c49da16cadc2cd94774eeb660bcf5c66be97dc77ee613"

SRC_URI = "${QWEN35_WHEEL_URL}/${QWEN35_WHEEL};name=qnnwheel;unpack=0"
SRC_URI[qnnwheel.sha256sum] = "382e31b7991a7a1003be636a0a97e888d3b95ffadef2162bdece74b5106f0aef"

S = "${WORKDIR}"
RDEPENDS:${PN} = "onnxruntime python3-core python3-numpy"

# The v68 set only; everything else in the wheel is dead weight on this SoC.
QWEN35_QNN_KEEP = " \
    libonnxruntime_providers_qnn.so \
    libQnnHtp.so \
    libQnnSystem.so \
    libQnnHtpV68Stub.so \
    libQnnHtpV68Skel.so \
"

do_install() {
	install -d ${D}${QWEN35_QNN_PYDIR}
	qwen35_unpack_wheel "${WORKDIR}/${QWEN35_WHEEL}" "${D}${PYTHON_DIST_PACKAGES}"

	for lib in ${QWEN35_QNN_KEEP}; do
		if [ ! -f "${D}${QWEN35_QNN_PYDIR}/$lib" ]; then
			bbfatal "onnxruntime-qnn: $lib missing from ${QWEN35_WHEEL}"
		fi
	done
	# Drop everything else the wheel put there (other DSP generations, QnnCpu,
	# QnnGpu, Genie, HtpPrepare): ~200 MB the image does not need.
	find ${D}${QWEN35_QNN_PYDIR} -maxdepth 1 -name '*.so' \
		! -name 'libonnxruntime_providers_qnn.so' \
		! -name 'libQnnHtp.so' ! -name 'libQnnSystem.so' \
		! -name 'libQnnHtpV68Stub.so' ! -name 'libQnnHtpV68Skel.so' -delete

	# Contract with files/qwen35-launch.sh and qwen35.service.
	install -d ${D}${libdir}
	ln -sf ../python3/dist-packages/onnxruntime_qnn ${D}${libdir}/qwen35-qnn.tmp
	install -d ${D}${libdir}/qwen35
	ln -sf ../python3/dist-packages/onnxruntime_qnn ${D}${libdir}/qwen35/qnn
	rm -f ${D}${libdir}/qwen35-qnn.tmp
}

FILES:${PN} = "${QWEN35_QNN_PYDIR} ${PYTHON_DIST_PACKAGES}/onnxruntime_qnn-${PV}.dist-info ${libdir}/qwen35/qnn"
```

Notes on the drafts:

* `libQnnHtpV68Skel.so` is the DSP-side skeleton; it must be reachable through
  `ADSP_LIBRARY_PATH` (the launcher already appends `<qnn dir>`, and
  `qwen_server.py` re-derives it from `qnn_plugin.parent` for the decoder child —
  both end up in the `onnxruntime_qnn` directory, which is why the symlink matters).
* `libQnnHtpV68Stub.so` has `NEEDED libcdsprpc.so` — that is the Yocto `fastrpc`
  package (already in the image via `meta-quectel`'s bbappend, which enables
  `cdsprpcd.service` because HTP runs on the **CDSP**, not the ADSP). Worth adding
  `RDEPENDS:${PN} += "fastrpc"` in the final recipe and `Wants=cdsprpcd.service`
  in the unit (the current unit only wants `adsp.service`/`adsprpcd.service`).
* `ERROR_QA:remove = "libdir"` is in the include because the recipe installs under
  `/usr/lib/python3/…` rather than `${libdir}`; without it the `libdir` check wants
  to relocate the tree.
* The `.dist-info` directory must ship: `pip`-less installs still rely on it for
  `importlib.metadata` (`onnxruntime_qnn` reads `__version__` from a plain .py, but
  `onnxruntime` uses metadata for build info, and two different versions of a
  package on the image is a debugging nightmare otherwise).

---

## 7. How to verify once the recipes exist (no board needed)

```bash
# 1. recipes parse and resolve (this DOES run bitbake -- do it in a shell where
#    `source quectel_build/compile/build.sh` + buildconfig have been run):
bitbake -e onnxruntime | grep -E '^(PV|S|SRC_URI)='        # only after the leader okays it
# 2. what actually landed:
ls -l <image>/usr/lib/python3/dist-packages/onnxruntime/capi/
ls -l <image>/usr/lib/qwen35/lib/ /usr/lib/qwen35/qnn/
readelf -d <image>/usr/lib/qwen35/qwen_text_engine | grep -E 'NEEDED|RUNPATH'
#    -> NEEDED libonnxruntime.so.1, RUNPATH $ORIGIN/lib:$ORIGIN
# 3. on the board:
python3 -c 'import onnxruntime, onnxruntime_qnn, onnx, tokenizers, PIL; \
            print(onnxruntime.__version__, onnxruntime_qnn.get_library_path())'
#    -> 1.27.0 /usr/lib/python3/dist-packages/onnxruntime_qnn/libonnxruntime_providers_qnn.so
ldd /usr/lib/qwen35/qwen_text_engine | grep -E 'onnxruntime|Qnn|cdsprpc'
# 4. sha256 spot check against §4 (the two ORT wheels were hashed locally
#    already; see the table).
```

---

## 8. Open items / risks

1. **`onnx` + `protobuf` + `ml_dtypes` + `typing_extensions` are only needed for
   `runtime/chunk_prefill.py`'s `onnx.load()` + `check_model()`** (it validates the
   EPContext before use). If the runtime owner is willing to make that validation
   optional (`QWEN35_SKIP_ONNX_CHECK`), four wheels (~25 MB, 4 recipes) drop out.
   Cheaper than shipping them, but a correctness feature — the leader's call.
2. **numpy 2.2.4 vs pinned 2.5.0.** All declared requirements are satisfied, but
   the precompiled QNN contexts were validated against 2.5.0 semantics for
   `np.float16`/`ml_dtypes` casts. Nothing observed either way; if there are
   numerical oddities, install the 2.5.0 cp313 aarch64 wheel into dist-packages
   and drop Debian's (do **not** have both).
3. **Route 2 remains viable if MS ever publishes `providers_qnn` sources**, which
   would need the QNN SDK (not in this tree) — revisit only if the prebuilt plugin
   ever needs patching.
4. `flatbuffers` may still be needed by `onnxruntime`'s `session` import path in
   some releases; if `import onnxruntime` raises `ModuleNotFoundError: flatbuffers`,
   add `flatbuffers-25.12.19-py2.py3-none-any.whl` (pure python, no recipe work).
5. The 20 MB duplicate `libonnxruntime.so.1.27.0` (dist-packages + `/usr/lib/qwen35/lib`)
   is intentional, see §3. A symlink would save it but makes the engine's SONAME
   resolvable only while the python package is installed.

# README-DEPS — the four wheel recipes that unblock `qwen35-npu`

Status: **onnxruntime, onnxruntime-qnn, tokenizers, qwen35-npu and qwen35-npu-model
are built and run on the board** (`qwen35-verify.sh`: PASS 57 / FAIL 0).
`pillow_11.3.0.bb` was added afterwards (§1.1) and is not built yet.  The
original three were written without running `bitbake` at all; every size and hash
below comes from the wheel files themselves or from PyPI's JSON API. Every size and hash below
comes from the wheel files themselves or from PyPI's JSON API; the checks that
*could* be done without a build were run and are listed in §7.

Companion document: `../qwen35-npu/DEPENDS_NOTES.md` (why these wheels at all,
which file pins which sha256, and what the three draft recipes looked like before
they became real recipes). This file records **what was decided** and **what is
inside the packages**.

---

## 1. The four packages

| recipe | package | upstream wheel | wheel size | installed (`${D}`) |
|---|---|---|---|---|
| `onnxruntime_1.27.0.bb` | `onnxruntime` | `onnxruntime-1.27.0-cp313-cp313-manylinux_2_27_aarch64.manylinux_2_28_aarch64.whl` | 16 438 180 B | 65.6 MB (327 files) |
| `onnxruntime-qnn_2.3.0.bb` | `onnxruntime-qnn` | `onnxruntime_qnn-2.3.0-cp313-cp313-manylinux_2_34_aarch64.whl` | 78 730 222 B | 122.7 MB (23 files) |
| `tokenizers_0.22.2.bb` | `tokenizers`, `RPROVIDES python3-tokenizers` | `tokenizers-0.22.2-cp39-abi3-manylinux_2_17_aarch64.manylinux2014_aarch64.whl` | 3 290 736 B | 10.0 MB (30 files) |
| `pillow_11.3.0.bb` | `pillow` | `pillow-11.3.0-cp313-cp313-manylinux_2_27_aarch64.manylinux_2_28_aarch64.whl` | 5 973 503 B | ~21 MB (140 files, incl. `pillow.libs/`) — not measured yet |

The `${D}` column is measured, not estimated (dry run, §7).  `onnxruntime` is
bigger than its 44 MB payload because `libonnxruntime.so.1.27.0` (20 MB) is
installed twice — once inside the python package (where the pybind11 module's
`$ORIGIN` looks for it) and once in `${libdir}/qwen35/lib` (where the engine's
`$ORIGIN/lib` RUNPATH looks for it, and where `${libdir}` staging puts it in the
recipe sysroot).  `onnxruntime-qnn` is 122.7 MB of a 192 MB wheel: the 88 MB
`libQnnHtpPrepare.so` and the 10 MB skel (installed twice, §3.2) dominate.

`qwen35-wheel.inc` is shared by all four: destination path, `do_unpack` via
`python3 -m zipfile`, `INSANE_SKIP`, `PACKAGE_ARCH`, and the two install helpers.

### `pillow` (11.3.0) — added after the first board run

The board proved the runtime needs it: `POST /v1/chat/completions` with an
`image_url` part returned

```
generation failed: frontend: Traceback ... request_frontend.py, line 121,
in preprocess_image → from PIL import Image, ImageOps
ModuleNotFoundError: No module named 'PIL'
```

`image_frontend.py` guards that import with `try/except ModuleNotFoundError`, but
that file is only used by the standalone CLI; the **server** path goes through
`request_frontend.py::preprocess_image`, which imports Pillow unconditionally and
then decodes/EXIF-rotates/pads whatever the data URL contains to 448×448.  The
three vision EPContext pairs (152 MB + 12 MB + 1.6 MB) were already in the image
and are loaded at startup anyway (`_validate_paths()` lists `vision_*_model` as
required), so without Pillow the image ships 166 MB of unreachable capability and
every image request is a 500.

Why not `meta-python`'s `python3-pillow_10.3.0.bb`: it builds for Yocto's 3.12 and
installs into `/usr/lib/python3.12/site-packages`, which the board's 3.13
interpreter never searches (§2), and it would drag `freetype`, `libtiff`,
`openjpeg` and `lcms` build dependencies into the image.  The manylinux wheel
bundles all of those in `pillow.libs/` and reaches them through
`RPATH=$ORIGIN/../pillow.libs`; the only system library it needs is `libz.so.1`,
which the Debian rootfs has.  The package name is `pillow`, not
`python3-pillow`, so it cannot be confused with the meta-python recipe.

### `onnxruntime` (1.27.0)

* `onnxruntime/libonnxruntime*.so*` — a *symlink* to them is only provided by a
  pip install; the wheel itself ships `libonnxruntime.so.1.27.0` with SONAME
  `libonnxruntime.so.1` and no `.so.1` entry, so the recipe creates the `.so.1`
  link. Without it the loader looks for a file that never existed.
* `libonnxruntime_providers_shared.so` (the EP shared helpers the QNN plugin
  expects), the cp313 python extension
  `onnxruntime_pybind11_state.cpython-313-aarch64-linux-gnu.so`, and the python
  layer (`onnxruntime/capi/*.py`, `transformers/`, `tools/`).
* Second copy under `${libdir}/qwen35/lib` (`libonnxruntime.so.1.27.0` plus
  `.so.1` and `.so` links): that directory is the engine's `DT_RUNPATH`
  (`-Wl,-rpath,'$ORIGIN/lib'`) and it is also what `DEPENDS` stages into
  `${STAGING_LIBDIR}/qwen35/lib` so `qwen35-npu`'s `do_compile` can link.
* No headers: the wheel has none. `qwen35-npu` vendors the identical 1.27.0 C API
  headers in `files/native/ort_include/`.

### `onnxruntime-qnn` (2.3.0, QAIRT 2.47.0)

Installed into `/usr/lib/python3/dist-packages/onnxruntime_qnn/` (module + all
libraries in one directory, because `onnxruntime_qnn/__init__.py` resolves the
plugin as `dirname(__file__)/libonnxruntime_providers_qnn.so` and every library
in the wheel has `RUNPATH=$ORIGIN`):

| file | size | why it is there |
|---|---|---|
| `libonnxruntime_providers_qnn.so` | 2 917 120 | the QNN execution provider plugin ORT loads |
| `libQnnHtp.so` | 4 267 144 | HTP backend the plugin `dlopen`s (2.47.0.260601114230) |
| `libQnnSystem.so` | 5 364 936 | QNN system/device API used by the plugin |
| `libQnnHtpV68Stub.so` | 473 160 | host-side stub, `NEEDED libcdsprpc.so` → `fastrpc` |
| `libQnnHtpV68Skel.so` | 10 026 512 | DSP-side skeleton (also copied to `/usr/lib/dsp/cdsp`) |
| `libQnnHtpPrepare.so` | 88 307 664 | context-binary generation, see §3.4 |
| `libQnnHtpNetRunExtensions.so` | 825 440 | kept: 0.8 MB, and no inference path names it (§3.4) |

Also installed: `/usr/lib/qwen35/qnn` (symlink, §3.1) and
`/usr/lib/dsp/cdsp/libQnnHtpV68Skel.so` (real copy, §3.2).

Dropped from the wheel (73 MB): `libGenie.so`, `libQnnCpu.so`, `libQnnGpu.so`,
`libQnnIr.so`, `libQnnSaver.so`, and the V69/V73/V75/V79/V81 skel+stub pairs.
`do_install` fails the build if any other `.so` shows up, so a wheel bump cannot
silently re-add 70 MB.

### `tokenizers` (0.22.2)

`/usr/lib/python3/dist-packages/tokenizers/` including `tokenizers.abi3.so`
(9 768 912 B). abi3 is what allows a `cp39-abi3` wheel to be imported by the
board's 3.13 interpreter; `do_install` fails if that file name ever changes.
`PROVIDES`/`RPROVIDES` carry `python3-tokenizers`, the name `qwen35-npu` already
`RDEPENDS` on.

---

## 2. Why `${PYTHON_DIST_PACKAGES}` and not `${PYTHON_SITEPACKAGES_DIR}`

The board rootfs is Debian 13 (`prebuild/debian-gnome-rootfs.tar.xz`). Its
`/usr/bin/python3` is 3.13 and its `sys.path` is `/usr/lib/python3/dist-packages`
plus `/usr/lib/python3.13`. Every other python recipe in this layer set installs
into `${PYTHON_SITEPACKAGES_DIR}` = `/usr/lib/python3.12/site-packages`, which
that interpreter never searches — the modules would be on the image and still
unimportable.

So `qwen35-wheel.inc` pins

```
PYTHON_DIST_PACKAGES = "/usr/lib/python3/dist-packages"
```

literally, with a comment saying not to "fix" it. Consequence to keep in mind:
these three packages are Debian-rootfs-only. A pure Yocto image
(`buildconfig ... LINUX ...`, python 3.12) would need all three rebuilt as real
OE recipes or as cp312 wheels — that is what DEPENDS_NOTES.md W-limits describe.

---

## 3. Decisions

### 3.1 `/usr/lib/qwen35/qnn` — **symlink**, not a real directory

```
/usr/lib/qwen35/qnn -> ../python3/dist-packages/onnxruntime_qnn
```

`files/qwen35-launch.sh` sets `QNN_DIR=$LIB_DIR/qnn` and puts it on
`LD_LIBRARY_PATH`, `qwen35-mkconfig.py` writes `runtime.json`'s `qnn_plugin` /
`qnn_backend` as `$QNN_DIR/...`, and `qwen35.service` carries `QWEN35_QNN_DIR`.
All three only need *a path that resolves*, and the plugin itself is bound to
`dirname(__file__)` — the libraries cannot be somewhere else than the module.

* **Why a symlink**: one copy on disk. A real directory would mean either
  duplicating 112 MB or moving the module out of `dist-packages` (which is where
  python looks for `import onnxruntime_qnn`). Symlink keeps both true.
* **Relative, not absolute**: `../python3/dist-packages/onnxruntime_qnn` resolves
  to `/usr/lib/python3/dist-packages/onnxruntime_qnn` on the board, and it is also
  correct inside `${D}` and inside the recipe sysroot, where the root is not `/`.
  The absolute form is right on the device but wrong for anything that
  materialises the tree somewhere else (packaging/sysroot passes included), and
  it buys nothing: the two directories are siblings, so the relative link only
  breaks if the layout itself changes. (An earlier draft of this recipe used the
  absolute form; the dry run in §7 is what checks the link target and its
  resolution.)
* **Dangling risk**: the target is installed by *this same package*, so the link
  can never dangle while the package is installed (`do_package` checks this too).
  If `onnxruntime-qnn` is removed with a package manager that does not check
  dependents, `/usr/lib/qwen35/qnn` becomes dangling and `qwen35-launch.sh`
  fails with "missing $QNN_DIR/libQnnHtp.so" — which is the correct, loud
  behaviour, and `qwen35-npu` `RDEPENDS` on `onnxruntime-qnn` prevents it in a
  normal image. Nothing else writes into that name.

### 3.2 `libQnnHtpV68Skel.so` — installed twice, both **real files**

`${PYTHON_DIST_PACKAGES}/onnxruntime_qnn/` (needed there: `qwen35-launch.sh`'s
fallback candidate is `$QNN_DIR/libQnnHtpV68Skel.so`, and
`qwen_server.py` re-derives the DSP path from `qnn_plugin.parent`) **and**
`/usr/lib/dsp/cdsp/` (the DSP-visible tree; `SKEL_DIR` default, first entry of
the semicolon-separated `ADSP_LIBRARY_PATH`).

Same bytes ⇒ `qwen35-launch.sh`'s backend/skel QAIRT-version pairing check passes
for both candidates. Cost: 9.6 MB duplicated. Alternative rejected: a symlink
from `/usr/lib/dsp/cdsp` into `/usr/lib/python3/...`, because that path is opened
from the DSP domain by the Hexagon loader and symlink-following across an
unrelated subtree is not something to bet "QnnDevice_create 14001" on.
Note `/usr/lib/dsp/cdsp` does not exist in the Debian rootfs today; this recipe
creates it. The only other QNN skel on the image lives in `/usr/lib/npu-llm`
(QAIRT 2.40, private to npu-llm and *not* on our `ADSP_LIBRARY_PATH`), so there is
no package conflict, but do not add `/usr/lib/npu-llm` to `ADSP_LIBRARY_PATH`:
2.47 backend + 2.40 skel aborts inside `QnnDevice_create`.

### 3.3 `libQnnHtpV68Stub.so` ⇒ `RDEPENDS fastrpc`

`readelf -d` on the stub: `NEEDED libcdsprpc.so`. HTP runs on the CDSP, so the
package dependency is `fastrpc` (meta-quectel's bbappend already enables
`adsprpcd` + `cdsprpcd`). `qwen35-npu` had the same `RDEPENDS`; it is repeated in
`onnxruntime-qnn` because this is the package that carries the ELF.

### 3.4 `libQnnHtpPrepare.so` is kept — deviation from the DEPENDS_NOTES draft

The draft dropped it (88 MB, "only used to *generate* context binaries"). It is
kept here because `libonnxruntime_providers_qnn.so` contains the string
`libQnnHtpPrepare.so` (its `LoadQnnHtpPrepareLib` path, i.e. a `dlopen` at HTP
backend setup) and whether that load is eager or only entered in `prepare_only`
mode cannot be established from the stripped binary, from PyPI metadata, or
without the board (the ORT source that matches 2.3.0 is not in this tree either).
The model ships precompiled EPContexts, so this file should be unnecessary — but
"should" is not worth an unbootable service on the first integration run.
After `qwen35.service` is confirmed healthy on hardware, move
`libQnnHtpPrepare.so` to `QWEN35_QNN_DROP` (one line) and rebuild.

`libQnnHtpNetRunExtensions.so` (825 KB) is kept on the same principle, at 1/100
of the cost: a `strings` scan over `libonnxruntime_providers_qnn.so` and over every
other kept QAIRT library finds that name only inside `libGenie.so` (which *is*
dropped), so no inference path on this image names it — but pruning 0.8 MB is not
worth a board round-trip if that scan missed a runtime-constructed name.

### 3.5 Assumption dependencies are *not* installed

Not installed on purpose: `flatbuffers`, `coloredlogs`, `humanfriendly`,
`sympy`, `mpmath`, `huggingface-hub`, `protobuf`, `onnx`, `numpy`.
(`pillow` is in this list no longer — the board showed it *is* needed for image
input, see §1; it now ships as its own wheel.)

* `onnxruntime`'s `METADATA` `Requires-Dist` lists flatbuffers/protobuf/packaging,
  but nothing under `onnxruntime/` imports them (grepped `capi/*.py`,
  `__init__.py`); they are needed by the model-conversion tools, not by the
  runtime.
* `onnxruntime_qnn`'s metadata additionally lists coloredlogs/sympy — also unused
  by `__init__.py`, which only does `os.path` work.
* `tokenizers` metadata mentions `huggingface_hub`, but `tokenizers/__init__.py`
  imports only `enum`, `typing`, `.tokenizers`, `.implementations`.
* `numpy` comes from the Debian rootfs (cp313). `onnx` is only imported by
  `runtime/chunk_prefill.py`, see §4.

---

## 4. `qwen35-npu_1.0.bb` changes made together with these recipes

* `RDEPENDS`: `python3-numpy` and `python3-onnx` were already removed before
  this task (the reasoning is in the comment block in the recipe); this task
  confirms it and does not re-add them.  `python3-pillow` stays out for the same
  reason, but the *capability* is now provided by the `pillow` wheel (§1) — the
  resource, not the OE name.
* `python3-tokenizers` stays and is now actually satisfiable:
  `tokenizers_0.22.2.bb` `RPROVIDES` that name.
* `file://runtime/chunk_prefill.py` **removed from `SRC_URI`** and the file
  deleted from `files/runtime/` (decision): it is
  unreachable (`qwen35-mkconfig.py: LIB_NAMES["frontend_script"] =
  request_frontend.py`; `stage_worker.py` loads exactly that file by path; no
  `importlib`/`__import__("chunk_prefill")` anywhere), it is the only user of
  `onnx`, and shipping a module whose dependency is deliberately absent is a trap
  for whoever runs it by hand next. `install -m 0644 ${S}/runtime/*.py` is a glob,
  so nothing else in `do_install` had to change. Re-adding the file *and*
  `python3-onnx` restores upstream's full-NPU prefill path.
* `DEPENDS = "onnxruntime onnxruntime-qnn"` still works: `do_compile` looks for
  `libonnxruntime.so{,.1,.1.27.0}` in `${STAGING_LIBDIR}/qwen35/lib`, which is
  populated because `onnxruntime` installs regular files under `${libdir}`.

---

## 5. Install map (what lands where)

```
/usr/lib/python3/dist-packages/onnxruntime/                 onnxruntime
/usr/lib/python3/dist-packages/onnxruntime-1.27.0.dist-info/
/usr/lib/python3/dist-packages/onnxruntime_qnn/             onnxruntime-qnn
/usr/lib/python3/dist-packages/onnxruntime_qnn-2.3.0.dist-info/
/usr/lib/python3/dist-packages/tokenizers/                  tokenizers
/usr/lib/python3/dist-packages/tokenizers-0.22.2.dist-info/
/usr/lib/qwen35/lib/libonnxruntime.so{,.1,.1.27.0}          onnxruntime
/usr/lib/qwen35/qnn -> ../python3/dist-packages/onnxruntime_qnn
/usr/lib/dsp/cdsp/libQnnHtpV68Skel.so                       onnxruntime-qnn
/usr/share/doc/{onnxruntime,onnxruntime-qnn,tokenizers}/    licences
```

`/usr/lib/qwen35/` itself is created by `qwen35-npu` as well (empty `lib/`), so
two packages own that directory — allowed by deb, and both ship parts of it.

---

## 6. QA settings, and why

`INSANE_SKIP:${PN} += "already-stripped dev-so file-rdeps libdir arch ldflags"`
(defined once, in `qwen35-wheel.inc`; all five are in `ERROR_QA` except `libdir`):

* `already-stripped` — upstream-shipped stripped binaries; re-stripping must not
  happen and the check is fatal by default.
* `dev-so` — `libonnxruntime.so` / `.so.1` sit next to the versioned file by
  design; a firmware image has no `-dev` package to move them into.
* `file-rdeps` — the NEEDED set is satisfied by the Debian rootfs
  (`libc`, `libstdc++`, `libgcc_s`) and by `fastrpc`; OE's scan only sees the OE
  half of the image.
* `arch` — `libQnnHtpV68Skel.so` is a Hexagon DSP6 image (ELF32, `e_machine`
  164), not an aarch64 userspace ELF. Same reason `qai-appbuilder_2.48.40.bb`
  skips it.
* `ldflags` — the GNU_HASH/build-id check is meaningless for prebuilt binaries,
  doubly so for the skel.
* `libdir` — files live under `/usr/lib/python3/...`, which is what pip does and
  what the board's python expects; the "relocate to ${libdir}" hint is wrong.

`FILES:${PN}` is set explicitly in each recipe (no wildcards over `/usr/lib`), and
`do_install` in `onnxruntime-qnn` fails on any `.so` outside `QWEN35_QNN_KEEP`, so
`installed-vs-shipped` cannot pass silently either.

---

## 7. What was verified without running bitbake

0. **`do_install` dry run** — the recipe's own shell functions were extracted,
   given the values bitbake would expand to (`${D}` = a throwaway directory, the
   real wheel as `${QWEN35_WHEEL_FILE}`) and executed. All three install trees are
   produced correctly: the ORT `.so.1`/`.so` links point at the versioned file,
   the QNN package ends up with exactly the seven kept libraries (all 0755) and
   nothing else, `/usr/lib/qwen35/qnn` is a relative link that resolves to the
   python package and the plugin is reachable through it, both skel copies are
   byte-identical, no `__pycache__` survives, and every docdir file is there.
   The sizes in §1 and the file counts come from that run.

1. **Parse check** — each `.bb` and the `.inc` parsed with bitbake's own
   `bb.data`/`ConfHandler` (see the command in §8); no syntax errors, every
   `file://` in `LIC_FILES_CHKSUM` exists.
2. **Hashes** — `sha256sum` of the three wheels on this machine equals PyPI's
   `digests.sha256`; sizes equal `size` in the PyPI JSON API:
   16438180 / 78730222 / 3290736.
3. **Wheel contents** — extracted with `python3 -m zipfile -e` (the exact
   mechanism the recipe uses): every file named in `do_install` /
   `QWEN35_QNN_KEEP` exists; `onnxruntime/capi/` really has no `.so.1` entry;
   the python extension is `cpython-313`; `tokenizers/tokenizers.abi3.so` exists.
4. **ELF facts** — `readelf -d`: `libonnxruntime.so.1.27.0` SONAME
   `libonnxruntime.so.1`, `RUNPATH $ORIGIN`; the pybind11 module `RUNPATH $ORIGIN`;
   `libonnxruntime_providers_qnn.so` `RUNPATH $ORIGIN`; `libQnnHtpV68Stub.so`
   `NEEDED libcdsprpc.so`; `libQnnHtpV68Skel.so` is a Hexagon ELF.
5. **Name contract** — packages produced are exactly `onnxruntime`,
   `onnxruntime-qnn`, `tokenizers` with `RPROVIDES python3-tokenizers`, i.e. the
   three names `qwen35-npu` `RDEPENDS` on.
6. **No leftover references** — `grep -rn "onnxruntime_qnn\|tokenizers"` over
   `../qwen35-npu/files/runtime/` matches only what these packages provide.

Not verified (needs a build): `do_package`/QA on the target sysroot, deb size and
dependency resolution in `do_rootfs`, and anything that needs the board.

---

## 8. How to build and check (when bitbake is allowed)

```bash
cd <项目根目录>
source quectel_build/compile/build.sh
buildconfig QSM565DWF QSM565DWFPARL1A01_BP01.001_Linux6.6.38_V01 DEBIAN STD

# the new wheel packages, in order
bitbake onnxruntime onnxruntime-qnn tokenizers pillow
# then the consumer (qwen35-npu RDEPENDS on all four)
bitbake qwen35-npu
```

Board-side sanity checks (after flashing):

```bash
python3 -c 'import numpy, tokenizers, onnxruntime, onnxruntime_qnn, PIL; print(tokenizers.__version__, onnxruntime.__version__, onnxruntime_qnn.__file__, PIL.__version__)'
readelf -d /usr/lib/python3/dist-packages/PIL/_imaging.cpython-313-aarch64-linux-gnu.so | grep -E 'RPATH|NEEDED'
ls -d /usr/lib/python3/dist-packages/pillow.libs   # libjpeg/libtiff/libopenjp2/libfreetype/...
ls -l /usr/lib/qwen35/qnn/            # -> /usr/lib/python3/dist-packages/onnxruntime_qnn
ls -l /usr/lib/qwen35/lib/            # libonnxruntime.so, .so.1, .so.1.27.0
ls -l /usr/lib/dsp/cdsp/              # libQnnHtpV68Skel.so, 10 026 512 B
sha256sum /usr/lib/dsp/cdsp/libQnnHtpV68Skel.so \
          /usr/lib/python3/dist-packages/onnxruntime_qnn/libQnnHtpV68Skel.so   # equal
strings /usr/lib/qwen35/qnn/libQnnHtp.so | grep -m1 2\\.47   # backend/skel pairing
journalctl -u qwen35 | head -40       # QnnDevice_create must not fail
```

If the service fails at QNN setup, the first thing to rule out is the QAIRT
pairing (`libQnnHtp.so` vs the skel actually loaded via `ADSP_LIBRARY_PATH`); the
launcher already prints both versions at every start.

---

## 9. Review changes after hand-off

These were applied on top of the recipes as first written; they are the
difference between "compiles in theory" and "compiles here":

1. **`INHIBIT_PACKAGE_STRIP` / `INHIBIT_SYSROOT_STRIP` /
   `INHIBIT_PACKAGE_DEBUG_SPLIT`** added to `qwen35-wheel.inc`.
   Without them the packaging class runs `${TARGET_PREFIX}strip` over everything:
   `libQnnHtpV68Skel.so` is an **ELF32 Hexagon image** (`e_machine` 164), so the
   aarch64 strip refuses it, and re-stripping the manylinux objects can only
   damage them. `INSANE_SKIP already-stripped` alone only silences the QA, it
   does not stop the strip. `arch` is in `INSANE_SKIP` for the same reason.
2. **`libQnnHtpNetRunExtensions.so` moved from `QWEN35_QNN_DROP` to
   `QWEN35_QNN_KEEP`.** It was on the drop list; but it is 0.8 MB, the HTP
   backend `dlopen()`s it by name for HTP net-run extensions, and a failed
   dlopen is invisible until `qwen35.service` runs on the board -- a full build
   plus flash to discover. Everything else on the drop list is genuinely
   unused (CPU/GPU/IR/Saver backends, other DSP generations, Genie).
3. **`/usr/lib/qwen35/qnn` is now a relative link** (`../python3/dist-packages/
   onnxruntime_qnn`) instead of an absolute one. An absolute target dangles
   inside the recipe sysroot, where the staging root is not `/`. It resolves
   correctly on the board either way.
4. **`qwen35-mkconfig.py`: `QNN_PLUGIN_GLOBS` gained
   `libonnxruntime_providers_qnn.so`** as its first entry. The fallback search
   (only used when `--qnn-plugin` is not passed, which the recipe always does)
   looked for `libonnxruntime_qnn.so`, a name that only exists in Qualcomm's
   QNN-EP tarballs, not in this wheel. Verified against the real tree: the search
   now returns `libonnxruntime_providers_qnn.so` and `libQnnHtp.so`.
5. **`runtime/chunk_prefill.py` deleted** from `qwen35-npu/files/runtime/`
   (it was already out of `SRC_URI`, so it could never ship, and it is the only
   file that needs `onnx`). `files/README.md` and `files/PORT_NOTES.md` were
   updated accordingly; `PORT_NOTES.md` also claimed upstream's full dependency
   list (`onnx`, `flatbuffers`, `protobuf`, `sympy`, `coloredlogs`, ...) was
   needed on the target, which is not true -- see §3.

Also verified here, not in §7: the wheel really contains every file `do_install`
copies from it -- `onnxruntime/{LICENSE,Privacy.md,ThirdPartyNotices.txt}` and
`onnxruntime_qnn/{LICENSE,Privacy.md,Qualcomm_LICENSE.pdf,ThirdPartyNotices.txt}`.
A missing one of these is a hard `install:` failure, and three of the seven are
not mentioned anywhere else in the recipe set.

Joint check that the two halves of the SHA-256 plumbing agree -- `;name=wheel` is
declared in `qwen35-wheel.inc` while `SRC_URI[wheel.sha256sum]` is in each `.bb`,
so a grep of a single file shows only one half:

```bash
grep -h ';name=wheel' qwen35-wheel.inc
for b in onnxruntime_1.27.0.bb onnxruntime-qnn_2.3.0.bb tokenizers_0.22.2.bb; do
    echo "$b: $(sed -n 's/.*SRC_URI\[wheel.sha256sum\] *= *"\(.*\)"/\1/p' $b)"
done
```

`QWEN35_WHEEL_FILE` (= `${WORKDIR}/whl/${QWEN35_WHEEL_NAME}`) is where bitbake
actually puts the download: with `unpack=0` the file is copied to
`os.path.join(rootdir, subdir)/basename(file)` (`bitbake/lib/bb/fetch2/__init__.py`,
`Fetch.unpack`), i.e. `${WORKDIR}/whl/<name>`, which is exactly what
`qwen35_extract_wheel` reads.

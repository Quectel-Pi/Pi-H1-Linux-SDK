SUMMARY = "Qwen3.5-0.8B NPU model assets for the Radxa Dragon Q6A QNN EPContext runtime"
DESCRIPTION = "Pre-compiled QNN EPContext assets for Qwen3.5-0.8B on the Hexagon V68 \
NPU of the QCS6490 (Quectel PI H1).  This is not a model that gets converted on \
the build host: the graphs were compiled upstream against QAIRT and are shipped \
as EPContext pairs (xxx_ctx.onnx + xxx_ctx_qnn.bin).  The pair must stay in the \
same directory, the .bin is the serialized HTP context the .onnx refers to. \
The 2K group is what the qwen35-npu recipe drives: \\
  common/embedding_fp32.npy                   1.0 GB  token embedding table (mmap'd) \
  common/tokenizer.json                        12 MB  tokenizers.Tokenizer.from_file \
  models/2k/decode_ctx.onnx + _qnn.bin        847 MB  P5 decoder, one token per run \
  models/2k/prefill_ctx.onnx                   20 KB  64-token block prefill \
  models/2k/assembly_report.json               16 KB  full-NPU tail assembly report \
  contracts/2k/rope_ctx2048_cos_sin_fp32.bin  1 MiB  precomputed RoPE table \
  contracts/2k/tensor_contract.tsv            140 KB  decoder input/output contract \
  contracts/2k/advance_plan.tsv               170 KB  state advance plan \
The vision group (3 onnx+bin pairs, 174 MB) is on by default even though the port \
is text-only: the upstream runtime validates the three vision paths with \
is_file() at startup, so the files have to be present unless qwen_server.py is \
patched to make them optional.  QWEN35_VISION_IN_IMAGE = \"0\" drops them. \
The assets are 1.8 GB of build products, not source: they are not in this layer \
and not in git.  The recipe fetches them from a pinned GitHub Release and \
verifies every file against the sha256 in the table below.  DL_DIR keeps them, so \
a rebuild does not download them again."

HOMEPAGE = "https://github.com/super617/radxa-dragon-q6a-qwen3.5-0.8b-qcs6490-qnn-npu"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/Apache-2.0;md5=89aea4e17d99a7cacdbeed46a0096b10"

# Upstream releases the weights under Apache-2.0 together with the code; the
# graphs are derivatives of Qwen/Qwen3.5-0.8B (also Apache-2.0).  NOTICE and the
# provenance statement live in the runner recipe (qwen35-npu), which is the
# package a user actually looks at; this one is a data package.
#
# Not allarch: the HTP context is compiled for one HTP architecture (v68 here)
# and one VTCM budget, so the same bytes are only valid on the same SoC.  Keep
# the machine architecture (the oe default for a target recipe) and let a
# different board fetch a different context.

# ---------------------------------------------------------------------------
# Source: GitHub Release, pinned by tag, one SRC_URI entry per file.
# ---------------------------------------------------------------------------
# Why a GitHub Release and not the HuggingFace repo the graphs first came from:
# huggingface.co is unreachable or TLS-reset on plenty of build networks (it was
# on this one), and its mirror needed a second non-obvious URL scheme.  GitHub
# Release assets are a plain HTTPS path with no extra tooling, and the project
# already ships its Debian rootfs the same way (GITHUB_ROOTFS_DL_URL in
# quectel_build/compile/build.sh).
#
# Release assets are FLAT: every file is <base>/<basename>, the repository's
# models/2k/... directory layout does not survive the upload.  The layout is
# restored on the way into ${WORKDIR} by the ;subdir= parameter below, so
# do_install and the runtime see exactly the same paths as before.
#
# Pinning: the release tag is what a human reads, but the sha256 per file in
# QWEN35_QNN_ARTIFACTS is what actually pins the build.  A tag re-upload that
# changes one byte fails do_fetch instead of silently rebuilding an image.
#
# The assets are staged for upload in log/models/ (git-ignored) with the exact
# names GitHub needs; see log/models/README-upload.md.
QWEN35_QNN_RELEASE_REPO ??= "super617/radxa-dragon-q6a-qwen3.5-0.8b-qcs6490-qnn-npu"
QWEN35_QNN_RELEASE_TAG ??= "qwen35-0.8b-qnn-v68-v1"
QWEN35_QNN_BASE ??= "https://github.com/${QWEN35_QNN_RELEASE_REPO}/releases/download/${QWEN35_QNN_RELEASE_TAG}"

# Where the files came from originally, for the record (NOT fetched).
QWEN35_QNN_UPSTREAM_REPO ??= "PrismPhi/Qwen3.5-0.8B-Radxa-Dragon-Q6A-QCS6490-QNN-NPU"
QWEN35_QNN_UPSTREAM_TAG ??= "05de67d2f33c0cb83e375f13788895c761d30b1e"

# Optional extra origins that serve the identical absolute path as QWEN35_QNN_BASE
# (i.e. <entry>/<basename>).  Tried before the release and, on failure, the
# fetcher falls through to it.  Empty by default: GitHub is the source of record,
# and every byte is sha256-checked whichever URL wins.
# A different layout is not a mirror but a different base: override
# QWEN35_QNN_BASE instead and leave this empty.
QWEN35_QNN_MIRRORS ??= ""

# Which groups travel in the firmware.  "2k" is the text model; "vision" adds the
# 174 MB three-stage image frontend.
#
# Vision is ON by default, which contradicts the first version of the brief ("text
# only, skip vision"): runtime/2k/qwen_server.py validates
# vision_patch_model / vision_body_model / vision_merger_model with
# Path.is_file() before it serves anything, so a text-only image with the
# unmodified runtime refuses to start.  Shipping the three pairs is 174 MB and no
# code change; QWEN35_VISION_IN_IMAGE = "0" is only correct together with a patch
# that makes those three paths optional (see files/PORT_NOTES.md, open risk 1).
QWEN35_VISION_IN_IMAGE ??= "1"
QWEN35_QNN_GROUPS ??= "2k ${@ 'vision' if d.getVar('QWEN35_VISION_IN_IMAGE') == '1' else ''}"

# label:path-inside-the-repo:group:sha256
#
# The label is what the fetcher keys SRC_URI[...sha256sum] off, so it has to be
# unique and stable; the sha256 is per file and was taken from the 15 files in
# log/models/ after checking them against upstream assets/manifest.json.
QWEN35_QNN_ARTIFACTS = " \
    qwen35_embedding:common/embedding_fp32.npy:2k:6a647a0e81e9586346987a22cfe4ce06d36279fe0ab35805acc3f7cae018358a \
    qwen35_tokenizer:common/tokenizer.json:2k:5f9e4d4901a92b997e463c1f46055088b6cca5ca61a6522d1b9f64c4bb81cb42 \
    qwen35_decode:models/2k/decode_ctx.onnx:2k:caff049aceadcb024fcc2d9a0d02864200b286f2fda764db6893c2dc3a92cf18 \
    qwen35_decode_bin:models/2k/decode_ctx_qnn.bin:2k:b2b636e939c3da86898d76186af3e5aa9eba173302d9a8deb9ea0dd37194ad75 \
    qwen35_prefill:models/2k/prefill_ctx.onnx:2k:814115f9a16a07dc7253e88ae755a0464e0cedd82f9690eeb8a284af00ea1156 \
    qwen35_report:models/2k/assembly_report.json:2k:7a62e03d830b21824c0ad1a3606af5a1b9700c70ba05a5f90a80baf273217b5f \
    qwen35_rope:contracts/2k/rope_ctx2048_cos_sin_fp32.bin:2k:09aab45c00a59ec4a209cc09ca3176a80c6ec1fbe8f4f7f9f360203d000dccc4 \
    qwen35_contract:contracts/2k/tensor_contract.tsv:2k:db6fd93f9a6ec451ebd238ca36416893f21b6f8752a3c4aefd7fccec4337d0ff \
    qwen35_plan:contracts/2k/advance_plan.tsv:2k:c883de8d7c9fbe317dcda67c5c3470fb78c0450dbd32868caedebd845fb399a8 \
    qwen35_vision_patch:models/vision/patch_ctx.onnx:vision:bc54bf752fe5aeac9afc0c80d2e528d0e9d895fccab5efcfbfcc3c48702bd604 \
    qwen35_vision_patch_bin:models/vision/patch_ctx_qnn.bin:vision:93aa5bf4bf8ad77e0698b1a84d0dca98c622e6a69a1903a858680ae4e2f1aaac \
    qwen35_vision_body:models/vision/body_ctx.onnx:vision:2ecc81862a796b790780445789481dda266c9ab000c3647390d42267387ad43f \
    qwen35_vision_body_bin:models/vision/body_ctx_qnn.bin:vision:f9e713326caa577e16ba9f2dee912ccc85058e12115ee40aff7e4592fb3202d1 \
    qwen35_vision_merger:models/vision/merger_ctx.onnx:vision:07398e1c818b1178e2852044be90395cfd2d01a3eecf86409c5865bb461a34c6 \
    qwen35_vision_merger_bin:models/vision/merger_ctx_qnn.bin:vision:0bd57db97158b577bc842e88169f32c513d3a186c0137244c844a766e2025b29 \
"

SRC_URI = ""

# Expand QWEN35_QNN_GROUPS + QWEN35_QNN_ARTIFACTS into real SRC_URI entries plus
# their checksums, and leave the selected list in QWEN35_QNN_INSTALL for
# do_install.  An anonymous function (not a plain assignment) because the group
# selection has to be resolved after local.conf has had its say, and because the
# per-file checksum flags cannot be expressed in an assignment.
#
# d.setVarFlag("SRC_URI", "<label>.sha256sum", sha) and NOT
# d.setVar("SRC_URI[<label>.sha256sum]", sha): the datastore has no bracket
# syntax, so setVar() would create a variable whose *name* contains the brackets
# while bb.fetch2's getVarFlag("SRC_URI", "<label>.sha256sum") keeps returning
# nothing.  The download then completes and do_fetch dies with "Missing SRC_URI
# checksum" after 1.8 GB.
python __anonymous() {
    import os

    base = (d.getVar("QWEN35_QNN_BASE") or "").rstrip("/")
    if not base:
        bb.fatal("qwen35-npu-model: QWEN35_QNN_BASE is empty")
    groups = set((d.getVar("QWEN35_QNN_GROUPS") or "").split())
    if not groups:
        bb.fatal("qwen35-npu-model: QWEN35_QNN_GROUPS is empty, nothing to install")

    table = {}
    order = []
    for row in (d.getVar("QWEN35_QNN_ARTIFACTS") or "").split():
        try:
            label, path, group, sha = row.split(":")
        except ValueError:
            bb.fatal("qwen35-npu-model: malformed QWEN35_QNN_ARTIFACTS row: %s" % row)
        if path in table:
            bb.fatal("qwen35-npu-model: duplicate artifact path %s" % path)
        table[path] = (label, group, sha)
        order.append(path)

    uris = []
    selected = []
    for path in order:
        label, group, sha = table[path]
        if group not in groups:
            continue
        # GitHub Release assets are flat, so the URL only carries the basename.
        # ;subdir= rebuilds the repository layout inside ${WORKDIR} so that
        # do_install copies ../common/, ../models/2k/ and ../contracts/2k/
        # verbatim; ;downloadfilename= keeps the on-disk name identical to the
        # release asset, which is what the digest check and the cache key use.
        uris.append("%s/%s;name=%s;downloadfilename=%s;subdir=qwen35-assets/%s"
                    % (base, os.path.basename(path), label,
                       os.path.basename(path), os.path.dirname(path)))
        d.setVarFlag("SRC_URI", "%s.sha256sum" % label, sha)
        selected.append(path)

    if not uris:
        bb.fatal("qwen35-npu-model: QWEN35_QNN_GROUPS=%s selects no artifact"
                 % " ".join(sorted(groups)))
    d.setVar("SRC_URI", " ".join(uris))
    # What do_install installs, in repository order.
    d.setVar("QWEN35_QNN_INSTALL", " ".join(selected))

    # Turn QWEN35_QNN_MIRRORS into PREMIRRORS entries.  A PREMIRROR is tried
    # before the real URL and, on failure, the fetcher falls through to it, which
    # is the fallback order we want.  The replacement uses the PATH placeholder:
    # uri_replace() substitutes it with the original URL's path, so only
    # scheme+host+prefix change and the repository layout is preserved.
    #
    # Paired "find replace" entries, whitespace separated (mirror_from_string
    # splits on whitespace); the trailing ".*" is the per-file part of the path.
    mirror_bases = (d.getVar("QWEN35_QNN_MIRRORS") or "").split()
    entries = []
    for mirror in mirror_bases:
        mirror = mirror.rstrip("/")
        if not mirror or mirror == base:
            continue
        entries.append("%s/.* %s/PATH" % (base, mirror))
    if entries:
        d.setVar("PREMIRRORS", " ".join(entries) + " " + (d.getVar("PREMIRRORS") or ""))
}

S = "${WORKDIR}/qwen35-assets"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    install -d ${D}${datadir}/qwen35

    for path in ${QWEN35_QNN_INSTALL}; do
        src="${S}/$path"
        if [ ! -f "$src" ]; then
            bbfatal "qwen35-npu-model: $src is missing. The fetcher should have put it \
there from ${QWEN35_QNN_BASE}/$path; see QWEN35_QNN_ARTIFACTS."
        fi
        install -d "${D}${datadir}/qwen35/$(dirname "$path")"
        install -m 0644 "$src" "${D}${datadir}/qwen35/$path"
    done

    # The runtime checks the presence of the 2K set at startup
    # (qwen_server.py: _validate_paths), and the .onnx + _qnn.bin pair is loaded
    # as a unit: a partial install is a board-side crash, not a degraded model.
    for path in common/embedding_fp32.npy common/tokenizer.json \
                models/2k/decode_ctx.onnx models/2k/decode_ctx_qnn.bin \
                models/2k/prefill_ctx.onnx models/2k/assembly_report.json \
                contracts/2k/rope_ctx2048_cos_sin_fp32.bin \
                contracts/2k/tensor_contract.tsv contracts/2k/advance_plan.tsv; do
        if [ ! -f "${D}${datadir}/qwen35/$path" ]; then
            bbfatal "qwen35-npu-model: $path was not installed; the 2K group is incomplete"
        fi
    done
}

FILES:${PN} = "${datadir}/qwen35/"

# 1.8 GB of already-dense context binaries and an mmap'd embedding table:
# compressing the deb only costs build time and gains nothing.
DPKG_BUILDCMD = "dpkg-deb -Znone"

# The assets are useless without the server and the engine that drives them;
# the other direction is deliberately not a dependency, so a runner-only image
# (IMAGE_INSTALL:remove = "qwen35-npu-model") still boots and skips the unit.
RDEPENDS:${PN} = "qwen35-npu"

# Nothing in here is ELF, but insanely-large files trip nothing and the files are
# 0644 data; keep the default checks so a mistake in the layout is loud.

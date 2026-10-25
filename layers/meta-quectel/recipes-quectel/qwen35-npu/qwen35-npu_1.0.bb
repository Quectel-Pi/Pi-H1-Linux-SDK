SUMMARY = "Qwen3.5-0.8B text server on the Hexagon V68 NPU (ONNX Runtime QNN EP, EPContext graphs)"
DESCRIPTION = "Board-side resident OpenAI-compatible server for Qwen3.5-0.8B running on \
the Hexagon HTP v68 NPU of the QCS6490 (Quectel PI H1), ported from the Radxa \
Dragon Q6A release (Apache-2.0). \
The design is not \"export a graph and quantize it here\": the 2K release ships \
pre-compiled QNN EPContexts (decode_ctx.onnx + decode_ctx_qnn.bin for the P5 \
decoder, prefill_ctx.onnx for the 64-token block prefill) and this recipe wires \
them up: \
  * qwen_text_engine -- cross-compiled from native/qwen_text_engine.cpp, links \
    libonnxruntime and registers the QNN execution-provider plugin \
    (RegisterExecutionProviderLibrary + AppendExecutionProvider_V2), owns the \
    recurrent state arena and the sampler; \
  * runtime/*.py -- upstream python front end (qwen_server.py, request_frontend, \
    stage_worker, state encoders): tokenizer, HTTP API, chunked prefill, state \
    transport between the front end and the engine; \
  * configs/2k.json + runtime.json -- the server config; runtime.json is \
    generated at build time by qwen35-mkconfig.py (a port of install.py's \
    cfg['paths'] block) so the board never runs the upstream installer. \
Layout, fixed with the model package (qwen35-npu-model) and files/qwen35-launch.sh: \
  /usr/lib/qwen35/           python runtime, qwen_text_engine, runtime.json, \
                             qwen35-launch.sh; qnn/ is the QNN EP/HTP library dir \
                             from the onnxruntime-qnn package, lib/ holds the ORT \
                             SONAME alias; \
  /usr/share/qwen35/         model assets (from qwen35-npu-model); \
  /var/lib/qwen35/           writable state: scratch/ and logs/ (created at runtime). \
The service listens on 127.0.0.1:8000 and is pinned to the four big cores \
(CPUAffinity=4-7), which is what keeps schedutil at 2.4 GHz.  The upstream \
installer is not used: no aarch64 host check, no pip, no on-board g++, no \
binary sha256 self-check; the recipe pins the wheels and the assets instead."

HOMEPAGE = "https://github.com/super617/radxa-dragon-q6a-qwen3.5-0.8b-qcs6490-qnn-npu"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=a2f7eec73f8b7e1873feeb201028cf9c"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# ---------------------------------------------------------------------------
# Layout and profile
# ---------------------------------------------------------------------------
# 2k is the text model (2048 context).  4k needs the 4K asset group in
# qwen35-npu-model (QWEN35_QNN_GROUPS = "4k"), its own configs/4k.json here, and
# the -DQWEN_CONTEXT=4096 below; upstream ships it as a separate profile because
# the context binary is twice the size and the state arena grows with it.
QWEN35_PROFILE ??= "2k"

QWEN35_LIBDIR = "${libdir}/qwen35"
QWEN35_ASSETDIR = "${datadir}/qwen35"
QWEN35_STATEDIR = "${localstatedir}/lib/qwen35"
# .../lib/qwen35, not ${localstatedir}/qwen35: ${localstatedir} expands to /var, and
# systemd's StateDirectory=qwen35 in qwen35.service always resolves to
# /var/lib/qwen35.  With the wrong root, runtime.json pointed the server at
# /var/qwen35/scratch while the unit created and whitelisted (ReadWritePaths)
# /var/lib/qwen35: two directories, one of them outside the sandbox rules, and
# launch.sh logging somewhere else than the server.  Keep the three in step:
# this variable, qwen35.service StateDirectory/ReadWritePaths/QWEN35_STATE, and
# qwen35-launch.sh DEFAULT_STATE_DIR.

# The two library paths the engine and runtime.json point at.  The names are a
# contract with the onnxruntime-qnn recipe (DEPENDS_NOTES.md): the QNN EP plugin
# is the library handed to RegisterExecutionProviderLibrary(), the backend is the
# HTP implementation libQnnHtp.so that the plugin dlopen()s.
QWEN35_QNN_DIR ??= "${QWEN35_LIBDIR}/qnn"
QWEN35_QNN_PLUGIN ??= "${QWEN35_QNN_DIR}/libonnxruntime_providers_qnn.so"
QWEN35_QNN_BACKEND ??= "${QWEN35_QNN_DIR}/libQnnHtp.so"

# onnxruntime release the C API headers and the engine were taken from.
QWEN35_ORT_VERSION ??= "1.27.0"
# Directory the onnxruntime recipe publishes libonnxruntime.so* in, when it is
# not the plain ${STAGING_LIBDIR}.  Empty means "search the usual places" (see
# do_compile); the wheel-based recipe in DEPENDS_NOTES.md installs into
# ${libdir}/qwen35/lib, which is already in that list.
QWEN35_ORT_LIBDIR ??= ""

SRC_URI = " \
    file://runtime/host_memory.py \
    file://runtime/image_frontend.py \
    file://runtime/native_handoff.py \
    file://runtime/prompt_bundle.py \
    file://runtime/qwen_server.py \
    file://runtime/request_frontend.py \
    file://runtime/runtime_cache.py \
    file://runtime/stage_worker.py \
    file://runtime/state_encoder.py \
    file://runtime/state_handoff.py \
    file://runtime/vision_runtime.py \
    file://native/qwen_text_engine.cpp \
    file://native/qnn_memory.cpp \
    file://native/exact_prefix.h \
    file://native/logit_offset.h \
    file://native/resident_prefill.h \
    file://native/ort_include/onnxruntime_c_api.h \
    file://native/ort_include/onnxruntime_cxx_api.h \
    file://native/ort_include/onnxruntime_cxx_inline.h \
    file://native/ort_include/onnxruntime_ep_c_api.h \
    file://native/ort_include/onnxruntime_float16.h \
    file://native/ort_include/UPSTREAM.json \
    file://configs/${QWEN35_PROFILE}.json \
    file://qwen35-mkconfig.py \
    file://qwen35-launch.sh \
    file://qwen35-verify.sh \
    file://qwen35.service \
    file://qwen35-journald.conf \
    file://LICENSE \
    file://NOTICE \
    file://README.md \
"

S = "${WORKDIR}"

# onnxruntime/onnxruntime-qnn are wheel-based recipes (DEPENDS_NOTES.md).  The
# wheels are cp313/aarch64 and install into /usr/lib/python3/dist-packages, i.e.
# their headers are not staged and libonnxruntime is not in ${STAGING_LIBDIR};
# DEPENDS is here so the sysroot is populated before do_compile and so
# ${STAGING_LIBDIR}/qwen35/lib/libonnxruntime.so.* appears (the wheel recipe
# installs it under ${libdir}/qwen35/lib, which normal sysroot staging covers).
# The C API headers come from files/native/ort_include/ -- the same 1.27.0 set,
# verified against upstream's runtime-lock.json.  RDEPENDS keeps both packages in
# the image, which is what provides the .so.1 the engine loads at runtime.
DEPENDS = "onnxruntime onnxruntime-qnn"
RDEPENDS:${PN} = " \
    onnxruntime \
    onnxruntime-qnn \
    python3 \
    python3-core \
    python3-json \
    python3-compression \
    python3-netclient \
    python3-netserver \
    python3-tokenizers \
    pillow \
    fastrpc \
    util-linux-flock \
    util-linux-taskset \
"

# Deliberately *not* depended on:
#
#   python3-numpy  - the rootfs is Debian (prebuild/debian-gnome-rootfs.tar.xz)
#                    and already ships numpy for its CPython 3.13 in
#                    /usr/lib/python3/dist-packages/numpy.  The only OE recipe
#                    is poky's python3-numpy, which lands in
#                    /usr/lib/python3.12/site-packages - a directory the board's
#                    3.13 interpreter never puts on sys.path - so depending on it
#                    would add build time and image size and still not satisfy
#                    `import numpy`.  Do not add it back for a Debian rootfs.
#   python3-onnx   - no layer has this recipe (checked).  `import onnx` appears
#                    only in runtime/chunk_prefill.py, and nothing imports that
#                    module: stage_worker.py loads request_frontend.py,
#                    image_frontend.py and prompt_bundle.py by path and never
#                    touches chunk_prefill.  It is dead code upstream, so it is
#                    not installed either (it is not in SRC_URI): shipping a
#                    module whose only dependency is absent would just be a trap.
#                    Re-add both the file and this RDEPENDS to get upstream's
#                    full-NPU prefill path back.
#   python3-pillow - that name belongs to meta-python's python3-pillow_10.3.0.bb,
#                    which builds for 3.12 and lands in a directory the board's
#                    3.13 interpreter never searches (same problem as numpy
#                    above).  PIL is *required* on the server path --
#                    request_frontend.py:121 imports it unguarded inside
#                    preprocess_image(), and only image_frontend.py (the
#                    standalone CLI) wraps its import in try/except -- so the
#                    self-contained cp313 wheel ships as 'pillow'
#                    (recipes-quectel/onnxruntime/pillow_11.3.0.bb) and is in
#                    RDEPENDS above.  Without it an image request is a 500.

# qwen35-mkconfig.py runs on the build host in do_install, so the native
# interpreter has to be in the recipe sysroot rather than whatever python3 the
# build machine happens to have.
inherit python3native
inherit systemd

# fastrpc is not a python/QNN dependency but an ELF one: the HTP stub
# libQnnHtpV68Stub.so has NEEDED libcdsprpc.so, and HTP sessions run on the CDSP
# (cdsprpcd), not the ADSP.  meta-quectel's fastrpc bbappend already enables
# adsprpcd + cdsprpcd, so the only thing missing here is the package dependency.
# python3-tokenizers is provided by recipes-quectel/onnxruntime/tokenizers_0.22.2.bb
# (an abi3 wheel; the recipe RPROVIDES the OE-style name used above).
# fastrpc is already in the RDEPENDS list at the top; the note here is the
# reasoning, not a second dependency (a duplicate entry is harmless but makes
# the list look like it grew twice).

SYSTEMD_SERVICE:${PN} = "qwen35.service"
# DISABLED: the resident server is on-demand only, never at boot.  It pins four
# big cores, holds ~490 MB RSS and owns the whole 850 MB NPU context, so leaving
# it resident degrades every other workload on the board.  It is started either
# by hand ('systemctl start qwen35') or by npu-chat after the user selects
# qwen3.5, and npu-chat stops it again when the user switches away.
# qwen35.service keeps its [Install] section, so 'systemctl enable qwen35' still
# works for anyone who wants the old behaviour.
# NOTE: set the un-overridden variable, not SYSTEMD_AUTO_ENABLE:${PN}.
# systemd.bbclass interpolates ${SYSTEMD_AUTO_ENABLE} into the generated
# postinst (systemd_postinst runs 'systemctl enable' when it reads "enable"),
# so a :${PN} only assignment -- which get_package_var() reads for the preset
# file -- would still leave the unit enabled in the image.  There is one
# package here, so the plain assignment is unambiguous for both readers.
SYSTEMD_AUTO_ENABLE = "disable"

# Flags from upstream install.py, unchanged: the engine needs C++20 (std::span,
# designated initialisers), -O3 for the sampler/state copies, and NDEBUG because
# its asserts are on hot paths.  -O3 comes after ${CXXFLAGS} on purpose so it
# wins over the default -O2.  -DQWEN_CONTEXT is only added for 4k: the 2K source
# hard-codes 2048 (no QWEN_CONTEXT reference in native/, checked) and defining it
# would change the graph contract.
QWEN35_ENGINE_CPPFLAGS = "-std=c++20 -O3 -DNDEBUG -pthread"
QWEN35_ENGINE_CPPFLAGS:append = "${@ ' -DQWEN_CONTEXT=4096' if d.getVar('QWEN35_PROFILE') == '4k' else ''}"

do_configure[noexec] = "1"

do_compile() {
	# --- onnxruntime C API headers ------------------------------------
	# The wheel does not ship headers (checked: onnxruntime/capi/ has only the
	# .so files), so this normally uses the vendored copy; ${STAGING_INCDIR}
	# comes first anyway so that a future headers package wins and a version bump
	# fails loudly here instead of silently compiling against 1.27.0 headers.
	# Either way the header set is byte-identical to upstream's runtime-lock.json
	# (sha256 in DEPENDS_NOTES.md §4).
	ort_inc="${STAGING_INCDIR}"
	if [ ! -e "${ort_inc}/onnxruntime_cxx_api.h" ]; then
		ort_inc="${S}/native/ort_include"
	fi
	for h in onnxruntime_c_api.h onnxruntime_cxx_api.h onnxruntime_cxx_inline.h \
	         onnxruntime_ep_c_api.h onnxruntime_float16.h; do
		if [ ! -e "${ort_inc}/$h" ]; then
			bbfatal "qwen35-npu: onnxruntime header $h is not in ${STAGING_INCDIR} \
nor in ${S}/native/ort_include. DEPENDS = \"onnxruntime\" should have published \
it; see DEPENDS_NOTES.md."
		fi
	done

	# --- libonnxruntime ------------------------------------------------
	# A library recipe installs libonnxruntime.so; an unpacked manylinux wheel
	# only has the versioned file plus a symlink.  Link whichever exists, by
	# full path, so the result does not depend on -L ordering.
	ort_lib=""
	for d in ${QWEN35_ORT_LIBDIR} "${STAGING_LIBDIR}" "${STAGING_LIBDIR}/qwen35" \
	         "${STAGING_LIBDIR}/qwen35/lib" "${STAGING_LIBDIR}/onnxruntime"; do
		[ -n "$d" ] || continue
		for name in libonnxruntime.so libonnxruntime.so.1 \
		            libonnxruntime.so.${QWEN35_ORT_VERSION}; do
			if [ -e "$d/$name" ]; then
				ort_lib="$d/$name"
				break 2
			fi
		done
	done
	if [ -z "$ort_lib" ]; then
		bbfatal "qwen35-npu: libonnxruntime.so not found (tried ${STAGING_LIBDIR}, \
${STAGING_LIBDIR}/qwen35, ${STAGING_LIBDIR}/qwen35/lib; names .so, .so.1, \
.so.${QWEN35_ORT_VERSION}). DEPENDS = \"onnxruntime\" should have published it; \
set QWEN35_ORT_LIBDIR, see DEPENDS_NOTES.md."
	fi
	bbnote "qwen35-npu: linking against $ort_lib"

	# qwen_text_engine.cpp #includes qnn_memory.cpp (upstream layout), so this
	# is one translation unit, exactly like upstream install.py's g++ line.
	# -rpath $ORIGIN/lib:$ORIGIN is upstream's -Wl,-rpath,$ORIGIN/lib: the
	# engine must find the ORT copy in /usr/lib/qwen35/lib even when
	# LD_LIBRARY_PATH is not set (it is set by qwen35-launch.sh, but the engine
	# is also run by hand and by --sampler-self-test).
	${CXX} ${CXXFLAGS} ${LDFLAGS} \
		${QWEN35_ENGINE_CPPFLAGS} \
		-I"$ort_inc" \
		${S}/native/qwen_text_engine.cpp \
		"$ort_lib" -ldl \
		-Wl,-rpath,'$ORIGIN/lib' -Wl,-rpath,'$ORIGIN' \
		-o ${B}/qwen_text_engine
}

do_install() {
	install -d ${D}${QWEN35_LIBDIR} ${D}${QWEN35_LIBDIR}/configs \
	           ${D}${QWEN35_LIBDIR}/lib ${D}${QWEN35_ASSETDIR} \
	           ${D}${bindir} ${D}${systemd_system_unitdir} ${D}${docdir}/qwen35-npu

	# python front end: imported by qwen_server.py from its own directory
	# (launch.sh cd's into ${QWEN35_LIBDIR}), so 0644 is right.
	install -m 0644 ${S}/runtime/*.py ${D}${QWEN35_LIBDIR}/

	# stage_worker.py:106 derives the directory its vision warmup is allowed to
	# live in as "Path(__file__).resolve().parent / 'scratch'" and aborts with
	# "warmup must stay inside the new layered scratch directory" otherwise.
	# The worker really puts that directory under paths["scratch_root"]
	# (${QWEN35_STATEDIR}/scratch), which is the writable state root, so without
	# this link the worker exits during startup and the server never listens.
	# A symlink keeps the shipped runtime .py byte-identical to upstream
	# (PORT_NOTES.md §5) and makes the guard's assumption true; nothing is
	# written through it at build time.  rm -rf first: if a previous layout left
	# a real directory here, ln -s would silently create the link *inside* it.
	rm -rf ${D}${QWEN35_LIBDIR}/scratch
	ln -s ${QWEN35_STATEDIR}/scratch ${D}${QWEN35_LIBDIR}/scratch

	install -m 0755 ${B}/qwen_text_engine ${D}${QWEN35_LIBDIR}/qwen_text_engine
	install -m 0755 ${S}/qwen35-launch.sh ${D}${QWEN35_LIBDIR}/qwen35-launch.sh
	# Board-side acceptance check (11 sections: assets, QNN pairing, DSP skel,
	# ADSP path, runtime.json, readiness, HTTP, NPU prefill, decode, perf, log
	# health).  It lives next to what it checks and is only ever run by hand.
	install -m 0755 ${S}/qwen35-verify.sh ${D}${QWEN35_LIBDIR}/qwen35-verify.sh

	# The upstream config template plus the generated runtime.json.  Both are
	# useful on the board: the template documents the tunables, runtime.json is
	# what the server reads.
	install -m 0644 ${S}/configs/${QWEN35_PROFILE}.json \
		${D}${QWEN35_LIBDIR}/configs/${QWEN35_PROFILE}.json
	install -m 0755 ${S}/qwen35-mkconfig.py ${D}${bindir}/qwen35-mkconfig.py

	# runtime.json is generated here, not on the board: qwen35-launch.sh refuses
	# to start without it, and qwen_server.py treats paths["python"] and
	# paths["decoder_engine"] as files that must exist.  qwen35-mkconfig.py writes
	# those two shims next to its --lib-root, so lib-root is the *staging* path
	# (otherwise it would try to create /usr/lib/qwen35 on the build host), and
	# the staging prefix is stripped from the generated JSON afterwards.  Every
	# path it emits except the asset root is under ${D}; the asset root is passed
	# as the real target path, so lib_root is the only source of ${D}.
	python3 ${S}/qwen35-mkconfig.py \
		--config ${S}/configs/${QWEN35_PROFILE}.json \
		--profile ${QWEN35_PROFILE} \
		--assets-root ${QWEN35_ASSETDIR} \
		--lib-root ${D}${QWEN35_LIBDIR} \
		--state-root ${QWEN35_STATEDIR} \
		--qnn-plugin ${QWEN35_QNN_PLUGIN} \
		--qnn-backend ${QWEN35_QNN_BACKEND} \
		--out ${D}${QWEN35_LIBDIR}/runtime.json \
		--verbose
	sed -i "s|${D}||g" ${D}${QWEN35_LIBDIR}/runtime.json
	# A generated config that still mentions ${D}, or the build host, would look
	# fine here and fail on the board, so check the two hazards explicitly.
	if grep -q "${D}" ${D}${QWEN35_LIBDIR}/runtime.json; then
		bbfatal "qwen35-npu: runtime.json contains ${D}; a staging path leaked through"
	fi
	if ! grep -q "\"qnn_plugin\"" ${D}${QWEN35_LIBDIR}/runtime.json; then
		bbfatal "qwen35-npu: runtime.json has no qnn_plugin path"
	fi
	python3 -m json.tool ${D}${QWEN35_LIBDIR}/runtime.json > /dev/null || \
		bbfatal "qwen35-npu: runtime.json is not valid JSON"
	# The two shims the generator just wrote are what qwen_server.py runs; they
	# are generated rather than shipped because upstream derives them from the
	# install directory (see qwen35-mkconfig.py:SHIM_PYTHON/SHIM_ENGINE).
	for shim in qwen-python engine.sh; do
		if [ ! -x ${D}${QWEN35_LIBDIR}/$shim ]; then
			bbfatal "qwen35-npu: qwen35-mkconfig.py did not write $shim"
		fi
	done

	install -m 0644 ${S}/qwen35.service ${D}${systemd_system_unitdir}/qwen35.service
	# The unit is a separate file with its own owner; if it is still the old
	# CPU-plan unit (ExecStart into /usr/share/qwen35-npu/tools) the package
	# boots and then fails, so say so at build time instead of on the board.
	if ! grep -q "qwen35-launch.sh" ${D}${systemd_system_unitdir}/qwen35.service; then
		bbwarn "qwen35-npu: qwen35.service does not start qwen35-launch.sh; files/qwen35.service is still the old CPU unit"
	fi

	install -m 0644 ${S}/LICENSE ${S}/NOTICE ${D}${docdir}/qwen35-npu/
	# qwen35.service points Documentation= at this file.
	install -m 0644 ${S}/README.md ${D}${docdir}/qwen35-npu/README.md
	# NOT installed as a journald drop-in: making the journal persistent is a
	# whole-system decision (flash wear, log retention for every unit), not
	# something a model package should flip. Ship it as a copy-and-go example so
	# the advice in qwen35-verify.sh is actionable.
	install -m 0644 ${S}/qwen35-journald.conf \
		${D}${docdir}/qwen35-npu/journald.conf.example
}

FILES:${PN} = " \
    ${QWEN35_LIBDIR} \
    ${QWEN35_ASSETDIR} \
    ${bindir}/qwen35-mkconfig.py \
    ${systemd_system_unitdir}/qwen35.service \
    ${docdir}/qwen35-npu \
"
# oe-core's default FILES:${PN}-doc claims ${docdir}, and ${PN}-doc sits before
# ${PN} in PACKAGES, so do_package silently moved every file above into a
# qwen35-npu-doc package -- which no image installs (checked against
# quecpi-image's package.manifest: it lists qwen35-npu and qwen35-npu-model,
# never the -doc one).  That cost three things at once: the Apache-2.0 LICENSE
# and NOTICE, which have to travel with the binary that links the code; the
# unit's Documentation=file:///usr/share/doc/qwen35-npu/README.md, which became
# a dead link; and the 'cp .../journald.conf.example ...' advice printed by
# qwen35-verify.sh, which pointed at a file that was not on the board.  This
# recipe installs no other documentation, so emptying the -doc split loses
# nothing and keeps the docs with the package that needs them.
FILES:${PN}-doc = ""
# ${QWEN35_LIBDIR}/lib and qnn/ are where the onnxruntime packages put their
# shared objects; they ship in those packages (FILES above only covers what this
# recipe installs, and the directories are created empty here -- ${QWEN35_LIBDIR}
# covers lib/ and qnn/).
#
# ${QWEN35_ASSETDIR} is the one empty directory that is *not* covered by that:
# do_install creates /usr/share/qwen35 so runtime.json has a target and the
# launcher's presence checks have somewhere to look, while the files themselves
# come from qwen35-npu-model.  Without it in FILES, populate_packages reports
# "installed but not shipped" (installed-vs-shipped) and the task fails.  Both
# packages may own the directory; only files conflict in dpkg.

# /usr/lib/qwen35/lib/libonnxruntime.so.1 (this package's RUNPATH, $ORIGIN/lib)
# and .../dist-packages/onnxruntime/capi/libonnxruntime.so.1 (needed by the
# python extension) are two regular files with the same SONAME in two different
# directories, so oe's shlib scan registers two providers and the lookup becomes
# ambiguous: "Multiple shlib providers for libonnxruntime.so.1: onnxruntime,
# onnxruntime" -- a fatal QA error.  The engine deliberately loads the copy next
# to it via DT_RUNPATH, i.e. its own private library, which is what PRIVATE_LIBS
# declares.  RDEPENDS above still pulls onnxruntime into the image, so nothing on
# the board is lost; only the ambiguous directory lookup is skipped.
PRIVATE_LIBS:${PN} = "libonnxruntime.so.1"

# The ORT/QNN libraries that arrive via RDEPENDS are prebuilt manylinux binaries
# that ship stripped, and nothing here can rebuild them with symbols; the QA
# check is fatal by default.  Everything this recipe builds itself is stripped by
# the normal do_package path.
INSANE_SKIP:${PN} += "already-stripped"

# No entry in IMAGE_INSTALL is needed beyond the two names quecpi-image already
# carries (qwen35-npu + qwen35-npu-model).  Drop the model package to get a
# runner-only image; the unit is skipped by ConditionPathExists instead of
# failing in a restart loop.

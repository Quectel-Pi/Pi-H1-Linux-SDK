do_compile:append() {
    if [ -d ${TMPDIR}/bazel ]; then
        chmod +w -R ${TMPDIR}/bazel
    fi
}

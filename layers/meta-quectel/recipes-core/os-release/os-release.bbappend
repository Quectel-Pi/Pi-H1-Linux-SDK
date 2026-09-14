do_install:append() {
    osr="${D}${nonarch_libdir}/os-release"
    qrel="${D}${sysconfdir}/quectel-release"

    # Ensure /etc|/usr/lib/os-release does not contain Quectel custom fields (legacy cleanup).
    sed -i '/^QUECTEL_/d' "$osr" || true

    # Parse buildconfig generated header as a stable source of "version name"
    gen_h="${WORKSPACE}/quectel_build/compile/quectel-features-config/quectel-buildconfig-gen.h"
    project_rev="unknown"

    if [ -f "$gen_h" ]; then
        project_rev="$(sed -n 's/^[[:space:]]*#define[[:space:]]\+QUECTEL_PROJECT_REV[[:space:]]\+"\([^"]*\)".*/\1/p' "$gen_h" | head -n1 || true)"
    fi

    [ -n "$project_rev" ] || project_rev="unknown"

    # Get git commit from top repo (fallback to unknown)
    git_commit="unknown"
    if [ -d "${WORKSPACE}/.git" ]; then
        git_commit="$(git -C "${WORKSPACE}" rev-parse HEAD 2>/dev/null || true)"
    fi
    [ -n "$git_commit" ] || git_commit="unknown"

    build_date="$(date +%Y-%m-%d)"

    # Write dedicated Quectel release info file (overwrite for idempotency).
    cat > "$qrel" <<EOF
QUECTEL_VERSION="${project_rev}"
QUECTEL_BUILD_DATE="${build_date}"
QUECTEL_GIT_COMMIT="${git_commit}"
EOF
}

# Ensure the dedicated Quectel release file is packaged (os-release.bb sets FILES:${PN} explicitly).
FILES:${PN} += "${sysconfdir}/quectel-release"

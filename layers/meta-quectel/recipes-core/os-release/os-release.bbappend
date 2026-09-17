do_install:append() {
    osr="${D}${nonarch_libdir}/os-release"
    qrel="${D}${sysconfdir}/quectel-release"

    # Ensure /etc|/usr/lib/os-release does not contain Quectel custom fields (legacy cleanup).
    sed -i '/^QUECTEL_/d' "$osr" || true

    # Parse buildconfig generated header as a stable source of "version name"
    gen_h="${WORKSPACE}/quectel_build/compile/quectel-features-config/quectel-buildconfig-gen.h"

    # Read one '#define QUECTEL_X "value"' out of the generated header.
    gen_value() {
        [ -f "$gen_h" ] || return 0
        sed -n "s/^[[:space:]]*#define[[:space:]]\+$1[[:space:]]\+\"\([^\"]*\)\".*/\1/p" "$gen_h" | head -n1 || true
    }

    project_rev="$(gen_value QUECTEL_PROJECT_REV)"
    project_name="$(gen_value QUECTEL_PROJECT_NAME)"
    custom_name="$(gen_value QUECTEL_CUSTOM_NAME)"

    [ -n "$project_rev" ] || project_rev="unknown"
    [ -n "$project_name" ] || project_name="unknown"
    [ -n "$custom_name" ] || custom_name="unknown"

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
QUECTEL_PROJECT_NAME="${project_name}"
QUECTEL_CUSTOM_NAME="${custom_name}"
QUECTEL_BUILD_DATE="${build_date}"
QUECTEL_GIT_COMMIT="${git_commit}"
EOF
}

# Ensure the dedicated Quectel release file is packaged (os-release.bb sets FILES:${PN} explicitly).
FILES:${PN} += "${sysconfdir}/quectel-release"

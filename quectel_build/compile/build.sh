#!/bin/bash

# Quectel buildconfig project
TOPDIR=$(pwd); export TOPDIR
BASE_RECIPES_FILE="${TOPDIR}/quectel_build/config/base-recipes"
if [ -f "${BASE_RECIPES_FILE}" ]; then
    TARGET_IMAGE=$(cat "${BASE_RECIPES_FILE}")
else
    TARGET_IMAGE="quecpi-image"
fi
export TARGET_IMAGE

KERNEL_FILE="$TOPDIR/build-qcom-wayland/tmp-glibc/deploy/images/qcm6490-idp/esp-qcom-image-qcm6490-idp.rootfs.vfat"
DTB_FILE="$TOPDIR/build-qcom-wayland/tmp-glibc/deploy/images/qcm6490-idp/dtb-qcom-image-qcm6490-idp.rootfs.vfat"

# GitHub rootfs release (used by DEBIAN/UBUNTU builds)
GITHUB_ROOTFS_DL_URL="https://github.com/super617/pi-rootfs/releases/download/latest"
GITHUB_ROOTFS_API_URL="https://api.github.com/repos/super617/pi-rootfs/releases/latest"
GITHUB_ROOTFS_API_CACHE="${TMPDIR:-/tmp}/pi-rootfs-latest.json"

env_check()
{
	# The SHELL variable also needs to be set to /bin/bash otherwise the build will fail
	if [[ ! $SHELL =~ bash ]]
	then
		echo "### ERROR: Please Change your shell to bash. ### "
		return 1
	fi

	if [ "$(whoami)" = "root" ]; then
	    echo "ERROR: do not use the BSP as root. Exiting..."
	    return 1
	fi
}

unset_unisoc_env() {
  unset DISTRO MACHINENAME MACHINE USERDEBUG SECBOOT_ENABLE NWMODE
}

has_custom_token()
{
    local expected="$1"
    local token

    for token in "${@:4}"; do
        case "${token^^}" in
            "${expected}"|"${expected}/"*|*/"${expected}"|*/"${expected}/"*)
                return 0
                ;;
        esac
    done

    return 1
}

# --- GitHub rootfs download helpers (DEBIAN/UBUNTU builds) -------------------
# Map buildconfig token (DEBIAN/UBUNTU) to the GitHub release asset name.
rootfs_asset_name()
{
    case "${1^^}" in
        DEBIAN) echo "debian-gnome-rootfs.tar.xz" ;;
        UBUNTU) echo "ubuntu26-gnome-rootfs.tar.xz" ;;
    esac
}

sha256_of()
{
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" 2>/dev/null | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" 2>/dev/null | awk '{print $1}'
    fi
}

# Download helper: curl (resumable) preferred, wget fallback.
# Interactive terminal: curl's default meter prints a live percentage table
# (%, speed, ETA) with no progress bar; wget's default bar also shows %.
# When stderr is redirected (logs/CI) the meter is suppressed (-sS still
# surfaces errors).
# HTTP/1.1 is forced: GitHub's 1GB+ assets over HTTP/2 often die with
# "curl: (92) HTTP/2 stream was not closed cleanly"; --retry-all-errors
# (curl >= 7.71) makes transient errors like that retry automatically.
github_rootfs_download()
{
    local url="$1" out="$2" curl_extra="" wget_extra=""
    if [ ! -t 2 ]; then
        # Non-interactive: suppress the meter but keep errors visible
        curl_extra="-sS"
        wget_extra="-q"
    fi
    if command -v curl >/dev/null 2>&1; then
        if curl --help all 2>/dev/null | grep -q -- '--retry-all-errors'; then
            curl_extra="${curl_extra} --retry-all-errors"
        fi
        curl -fL --http1.1 --retry 5 --retry-delay 5 -C - --max-time 7200 ${curl_extra} -o "$out" "$url"
    elif command -v wget >/dev/null 2>&1; then
        wget -c --timeout=30 --tries=3 ${wget_extra} -O "$out" "$url"
    else
        return 1
    fi
}

# ETag of the latest release asset (content fingerprint, used when the GitHub
# API is rate-limited or unreachable). The Azure blob ETag changes whenever
# the file content changes, so it is a cheap "is it the latest?" signal.
github_rootfs_remote_etag()
{
    command -v curl >/dev/null 2>&1 || return 1
    curl -sIL --max-time 30 "${GITHUB_ROOTFS_DL_URL}/${1}" 2>/dev/null | grep -i '^etag:' | tail -1 | tr -d '\r' | sed 's/^[Ee][Tt][Aa][Gg]:[[:space:]]*//'
}

# Parse the "sha256:<hex>" digest of an asset from the GitHub release JSON
# (pretty-printed, one field per line; digest may be null for some assets).
github_rootfs_remote_sha256()
{
    awk -v asset="$1" '
        /"name":/  { gsub(/[" ,]/, "", $2); name=$2 }
        /"digest":/ && name==asset && $2!="null" {
            gsub(/[" ,]/, "", $2)
            sub(/^sha256:/, "", $2)
            print $2
            exit
        }
    ' "$2"
}

# Fetch one rootfs tarball from GitHub:
#   - local tarball missing                      -> download
#   - local sha256 != latest (API digest)        -> download
#   - API unavailable (rate limit/offline)       -> ETag fallback, else keep
#                                                   local tarball + warn
fetch_github_rootfs_asset()
{
    local asset="$1"
    [ -n "$asset" ] || return 0
    mkdir -p "${TOPDIR}/prebuild"

    local tar="${TOPDIR}/prebuild/${asset}"
    local src_dir="${TOPDIR}/prebuild/${asset%.tar.*}"
    local etag_file="${tar}.etag"
    local dl_url="${GITHUB_ROOTFS_DL_URL}/${asset}"
    local remote_sha="" json_tmp=""

    # 1) Try a fresh GitHub API call (asset digest = sha256 of the latest file)
    json_tmp=$(mktemp) || json_tmp=""
    if [ -n "$json_tmp" ] && curl -sfL --max-time 30 "${GITHUB_ROOTFS_API_URL}" -o "$json_tmp" 2>/dev/null; then
        remote_sha=$(github_rootfs_remote_sha256 "$asset" "$json_tmp")
        if [ -n "$remote_sha" ]; then
            mkdir -p "$(dirname "${GITHUB_ROOTFS_API_CACHE}")"
            mv -f "$json_tmp" "${GITHUB_ROOTFS_API_CACHE}" 2>/dev/null
            json_tmp=""
        fi
    fi
    [ -z "$json_tmp" ] || rm -f "$json_tmp"
    # 2) Fall back to the cached release JSON if the fresh call failed
    if [ -z "$remote_sha" ] && [ -f "${GITHUB_ROOTFS_API_CACHE}" ]; then
        remote_sha=$(github_rootfs_remote_sha256 "$asset" "${GITHUB_ROOTFS_API_CACHE}")
    fi

    local need_download=0 reason=""
    if [ ! -f "$tar" ]; then
        need_download=1
        reason="local tarball not found"
    elif [ -n "$remote_sha" ]; then
        local local_sha
        local_sha=$(sha256_of "$tar")
        if [ -z "$local_sha" ]; then
            need_download=1
            reason="cannot compute local sha256"
        elif [ "$local_sha" != "$remote_sha" ]; then
            need_download=1
            reason="local sha256 ${local_sha} != latest ${remote_sha}"
        else
            echo -e "\033[32;1m[INFO] ${asset}: up to date (sha256 match)\033[0m"
        fi
    else
        # API unreachable/rate-limited: compare ETags instead
        local old_etag="" new_etag
        [ -f "$etag_file" ] && old_etag=$(cat "$etag_file")
        new_etag=$(github_rootfs_remote_etag "$asset")
        if [ -n "$new_etag" ]; then
            if [ -n "$old_etag" ] && [ "$old_etag" = "$new_etag" ]; then
                echo -e "\033[32;1m[INFO] ${asset}: up to date (etag match)\033[0m"
            else
                need_download=1
                reason="etag changed (${old_etag:-none} -> ${new_etag})"
            fi
        else
            echo -e "\033[33;1m[WARN] ${asset}: cannot verify against GitHub (API/network unavailable), keep local tarball\033[0m"
        fi
    fi

    if [ "$need_download" -eq 1 ]; then
        echo -e "\033[33;1m[INFO] ${asset}: ${reason}, downloading ${dl_url}\033[0m"
        local part="${tar}.part"
        # Keep an existing .part: curl -C - resumes from it on the next run
        # (a failed download must not force a 1GB restart from zero).
        if github_rootfs_download "$dl_url" "$part"; then
            # Integrity check when the expected sha256 is known
            if [ -n "$remote_sha" ]; then
                local dl_sha
                dl_sha=$(sha256_of "$part")
                if [ -n "$dl_sha" ] && [ "$dl_sha" != "$remote_sha" ]; then
                    echo -e "\033[31;1m[ERROR] ${asset}: sha256 mismatch after download (${dl_sha} != ${remote_sha}), aborting\033[0m"
                    rm -f "$part"
                    return 1
                fi
            fi
            rm -f "$tar"
            mv -f "$part" "$tar"
            local new_etag
            new_etag=$(github_rootfs_remote_etag "$asset")
            [ -n "$new_etag" ] && echo "$new_etag" > "$etag_file"
            # Invalidate the extracted tree so bitbake re-extracts the new tarball
            if [ -d "$src_dir" ]; then
                echo -e "\033[33;1m[INFO] ${asset}: removing stale extracted tree ${src_dir}\033[0m"
                rm -rf "$src_dir"
            fi
            echo -e "\033[32;1m[INFO] ${asset}: downloaded $(stat -c %s "$tar" 2>/dev/null) bytes -> ${tar}\033[0m"
        else
            if [ -f "$part" ]; then
                echo -e "\033[31;1m[ERROR] ${asset}: download interrupted, partial file kept at ${part}\033[0m"
                echo -e "\033[31;1m       rerun buildconfig to resume from $(stat -c %s "$part" 2>/dev/null) bytes (-C -)\033[0m"
            else
                echo -e "\033[31;1m[ERROR] ${asset}: download failed: ${dl_url}\033[0m"
            fi
            return 1
        fi
    fi
    return 0
}

function buildpackage()
{ 
    TARGET_IMAGE=$TARGET_IMAGE $TOPDIR/quectel_build/a_key_generation.sh $QUECTEL_PROJECT_NAME $QUECTEL_PROJECT_REV $QUECTEL_CUSTOM_NAME
}

function flash()
{
    local flash_script="${TOPDIR}/quectel_build/tools/flash.sh"
    if [ ! -x "${flash_script}" ]; then
        echo -e "\033[31;1m[ERROR] flash script not found or not executable: ${flash_script}\033[0m"
        return 1
    fi
    "${flash_script}" "$@"
}


function buildenv()
{
    PRJECT_GEN_FILE=${TOPDIR}/quectel_build/compile/quectel-features-config/quectel-buildconfig-gen.h
    echo "PRJECT_GEN_FILE:${PRJECT_GEN_FILE}"
    if [ -f "${PRJECT_GEN_FILE}" ]
    then
        QUECTEL_PROJECT_NAME="$(sed -n '/QUECTEL_PROJECT_NAME/p' ${PRJECT_GEN_FILE} | awk -F ' ' '{print $3}' | sed 's/\"//g')"
        QUECTEL_PROJECT_REV="$(sed -n '/QUECTEL_PROJECT_REV/p' ${PRJECT_GEN_FILE} | awk -F ' ' '{print $3}' | sed 's/\"//g')"
        QUECTEL_CUSTOM_NAME="$(sed -n '/QUECTEL_CUSTOM_NAME/p' ${PRJECT_GEN_FILE} | awk -F ' ' '{print $3}' | sed 's/\"//g')"
    fi

    if [ "$QUECTEL_PROJECT_NAME" != "" ]
    then
        echo -e "\033[33;1mif you want to change next value, then run 'buildconfig' command\033[0m"
    else
        echo -e "\033[31;1mProject Name and Version information missed! Please run 'buildconfig' command\033[0m"
    fi

    echo -e "\033[32;1mCurrent QUECTEL_PROJECT_NAME = ${QUECTEL_PROJECT_NAME} \033[0m"
    echo -e "\033[32;1mCurrent QUECTEL_PROJECT_REV  = ${QUECTEL_PROJECT_REV} \033[0m"
    echo -e "\033[32;1mCurrent QUECTEL_CUSTOM_NAME  = ${QUECTEL_CUSTOM_NAME} \033[0m"
}

function buildconfig()
{
    # Two build dimensions:
    #   buildconfig <proj> <rev> <LINUX|DEBIAN|UBUNTU> <STD|DBG> [SEC]
    #     OS      : LINUX (Yocto standard) / DEBIAN / UBUNTU
    #     VERSION : STD -> performance build (DEBUG_BUILD=0, PERFORMANCE_BUILD=1, default)
    #               DBG -> debug build     (DEBUG_BUILD=1, PERFORMANCE_BUILD=0) + keep symbols
    # Legacy single-token CUST_NAME (STD / DBG / DEBIAN / ...) still works.
    # A bare trailing DEBUG token is consumed here and NOT passed to
    # config_parser.py (it is not a registered custoct token); it is the
    # same as the DBG version dimension.
    local BUILD_ARGS=()
    local DEBUG_BUILD_FLAG=0
    local OS_DIM="" VER_DIM=""
    for arg in "$@"; do
        case "${arg^^}" in
            DEBUG)
                DEBUG_BUILD_FLAG=1
                VER_DIM="DBG"
                ;;
            LINUX|DEBIAN|UBUNTU)
                OS_DIM="${arg^^}"
                BUILD_ARGS+=("$arg")
                ;;
            STD)
                VER_DIM="STD"
                BUILD_ARGS+=("$arg")
                ;;
            DBG)
                VER_DIM="DBG"
                BUILD_ARGS+=("$arg")
                ;;
            *)
                BUILD_ARGS+=("$arg")
                ;;
        esac
    done

    if [ -z "${OS_DIM}" ]; then
        echo -e "\033[33;1m[WARN] no OS dimension (LINUX|DEBIAN|UBUNTU) given, keeping legacy single-token behavior\033[0m"
    fi
    echo -e "\033[32;1mBuild dimensions: OS=${OS_DIM:-<legacy>} VERSION=${VER_DIM:-STD}\033[0m"

    # DBG token: keep debug symbols (no strip) AND force a debug build.
    if has_custom_token "DBG" "${BUILD_ARGS[@]}"; then
        echo 'INHIBIT_PACKAGE_STRIP = "1"'         >> ${BUILDDIR}/conf/local.conf
        echo 'INHIBIT_PACKAGE_DEBUG_STRIP = "1"'   >> ${BUILDDIR}/conf/local.conf
        echo 'INHIBIT_SYSROOT_STRIP = "1"'         >> ${BUILDDIR}/conf/local.conf
        DEBUG_BUILD_FLAG=1
        echo -e "\033[32;1mDBG mode: keep debug symbols (INHIBIT strip)\033[0m"
    fi

    # Apply the build type to auto.conf (DEBUG_BUILD / PERFORMANCE_BUILD).
    if [ "${DEBUG_BUILD_FLAG}" -eq 1 ]; then
        sed -i 's/^DEBUG_BUILD = .*/DEBUG_BUILD = "1"/' ${BUILDDIR}/conf/auto.conf
        sed -i 's/^PERFORMANCE_BUILD = .*/PERFORMANCE_BUILD = "0"/' ${BUILDDIR}/conf/auto.conf
        export DEBUG_BUILD=1 PERFORMANCE_BUILD=0
        echo -e "\033[32;1mDEBUG build enabled (DEBUG_BUILD=1, dump download_mode=1 on cmdline)\033[0m"
    else
        sed -i 's/^DEBUG_BUILD = .*/DEBUG_BUILD = "0"/' ${BUILDDIR}/conf/auto.conf
        sed -i 's/^PERFORMANCE_BUILD = .*/PERFORMANCE_BUILD = "1"/' ${BUILDDIR}/conf/auto.conf
        export DEBUG_BUILD=0 PERFORMANCE_BUILD=1
        echo -e "\033[32;1mPERFORMANCE build enabled (DEBUG_BUILD=0, dump off by default)\033[0m"
    fi

    # Handle SEC parameter for secure boot builds
    if has_custom_token "SEC" "${BUILD_ARGS[@]}"; then
        echo 'SECBOOT_ENABLE = "1"' >> ${BUILDDIR}/conf/auto.conf
        export SECBOOT_ENABLE=1
        # Create flag file for config_help to re-apply
        touch /tmp/.secboot_enabled
        echo -e "\033[32;1mSECBOOT mode enabled\033[0m"
    else
        export SECBOOT_ENABLE=0
        rm -f /tmp/.secboot_enabled
    fi

    # Rootfs sync lists
    UBUNTU_SYNC_FILE=${TOPDIR}/quectel_build/compile/quectel-features-config/ubuntu-sync-list
    DEBIAN_SYNC_FILE=${TOPDIR}/quectel_build/compile/quectel-features-config/debian-sync-list

    MOUNT_CONTROL_FILE=${TOPDIR}/layers/meta-qcom-hwe/recipes-core/packagegroups/packagegroup-qcom-initscripts.bb

    # Only the OS dimension selects the rootfs. It must not be keyed off the
    # version tokens: a bare "STD" used to mean the Linux system before the OS
    # dimension existed, so matching it here made "DEBIAN STD" skip the Debian
    # rootfs and silently pack the Yocto one. A legacy invocation without an OS
    # token still means the Linux system (LINUX is the default system).
    if [ "${OS_DIM:-LINUX}" = "LINUX" ]; then
        echo 'SKIP_DEPLOY_DEBIAN_GNOME_ROOTFS = "1"' >> ${BUILDDIR}/conf/local.conf
    else
        sed -i '/^SKIP_DEPLOY_DEBIAN_GNOME_ROOTFS/d' ${BUILDDIR}/conf/local.conf
    fi
    if has_custom_token "UBUNTU" "${BUILD_ARGS[@]}"; then
        cp -rf ${UBUNTU_SYNC_FILE} ${TOPDIR}/prebuild/sync-list
        fetch_github_rootfs_asset "$(rootfs_asset_name UBUNTU)"
    fi

    if has_custom_token "DEBIAN" "${BUILD_ARGS[@]}"; then
        cp -rf ${DEBIAN_SYNC_FILE} ${TOPDIR}/prebuild/sync-list
        fetch_github_rootfs_asset "$(rootfs_asset_name DEBIAN)"
    fi

    env_check
    if [ ! -f ${TOPDIR}//config/linker/versions ]
    then
        python -B ${TOPDIR}//quectel_build/compile/version_parser_auto.py "${BUILD_ARGS[@]}"
        if [ $? != 0 ];then
            break
        fi
    fi

    python -B ${TOPDIR}/quectel_build/compile/config_parser.py "${BUILD_ARGS[@]}"
    if [ $? != 0 ];then
        break
    fi

    # Update auto.conf from quectel_var.inc after config_parser generates it
    if [ -f "${TOPDIR}/quectel_build/compile/quectel-features-config/quectel_var.inc" ] && [ -n "${BUILDDIR}" ]; then
        local prj_name=$(grep "^QUECTEL_PROJECT_NAME" "${TOPDIR}/quectel_build/compile/quectel-features-config/quectel_var.inc" | cut -d= -f2 | tr -d ' ')
        local prj_rev=$(grep "^QUECTEL_PROJECT_REV" "${TOPDIR}/quectel_build/compile/quectel-features-config/quectel_var.inc" | cut -d= -f2 | tr -d ' ')
        local cust_name=$(grep "^QUECTEL_CUSTOM_NAME" "${TOPDIR}/quectel_build/compile/quectel-features-config/quectel_var.inc" | cut -d= -f2 | tr -d ' ')
        local git_commit=$(grep "^QUECTEL_GIT_COMMIT" "${TOPDIR}/quectel_build/compile/quectel-features-config/quectel_var.inc" | cut -d= -f2 | tr -d ' ')
        [ -n "$prj_name" ] && sed -i "s/^BUILDNAME = .*$/BUILDNAME = \"$prj_name\"/" ${BUILDDIR}/conf/auto.conf
        [ -n "$prj_rev" ] && sed -i "s/^QUECTEL_PROJECT_REV = .*$/QUECTEL_PROJECT_REV = \"$prj_rev\"/" ${BUILDDIR}/conf/auto.conf
        [ -n "$cust_name" ] && sed -i "s/^QUECTEL_CUSTOM_NAME = .*$/QUECTEL_CUSTOM_NAME = \"$cust_name\"/" ${BUILDDIR}/conf/auto.conf
        [ -n "$git_commit" ] && sed -i "s/^QUECTEL_GIT_COMMIT = .*$/QUECTEL_GIT_COMMIT = \"$git_commit\"/" ${BUILDDIR}/conf/auto.conf
        echo -e "\033[32;1mUpdated auto.conf from quectel_var.inc\033[0m"
    fi

    # Re-apply SECBOOT_ENABLE after config_parser.py (which regenerates auto.conf)
    if [ "${SECBOOT_ENABLE}" = "1" ] && [ -n "${BUILDDIR}" ]; then
        echo 'SECBOOT_ENABLE = "1"' >> ${BUILDDIR}/conf/auto.conf
        echo -e "\033[32;1mSECBOOT re-applied to auto.conf after config_parser\033[0m"
    fi

    buildenv

    export QUECTEL_PROJECT_NAME
    export QUECTEL_PROJECT_REV
    export QUECTEL_CUSTOM_NAME
    export QUECTEL_FEATURE_OPENLINUX
    
    cp -rf ${PRJECT_GEN_FILE} ${TOPDIR}/layers/meta-quectel/recipes-quectel/atcid/files/atci/quectel/inc/quectel-buildconfig-gen.h
}


export QUECTEL_DIR=${TOPDIR}

echo "TOPDIR:${TOPDIR}"
echo "QUECTEL_DIR:${QUECTEL_DIR}"

# git config global
git config --global core.editor vim
git config --global commit.template .gitcontent
config_help()
{
	python -B ${TOPDIR}/quectel_build/compile/config_parser.py $@
}
config_help $@

# Re-apply SECBOOT_ENABLE after config_help (which regenerates auto.conf)
if [ -f /tmp/.secboot_enabled ] && [ -n "${BUILDDIR}" ]; then
    echo 'SECBOOT_ENABLE = "1"' >> ${BUILDDIR}/conf/auto.conf
    export SECBOOT_ENABLE=1
    echo -e "\033[32;1mSECBOOT re-applied to auto.conf\033[0m"
fi

function buildall() {

    bitbake $TARGET_IMAGE -c cleanall
    bitbake $TARGET_IMAGE
}

function buildsdk() {
    bitbake $TARGET_IMAGE -c populate_sdk
    $TOPDIR/quectel_build/compile/export_sdk.sh $@
    $TOPDIR/quectel_build/do_image_package.sh
}

function do_kernel_images() {
    $TOPDIR/quectel_build/do_image_package.sh
}

function buildesdk() {
    # bitbake qcom-multimedia-crossesdk-image
    bitbake $TARGET_IMAGE -c populate_sdk
    bitbake $TARGET_IMAGE -c populate_sdk_ext
}

function buildkernel() {
    bitbake -c cleanall virtual/kernel && bitbake esp-qcom-image
    mkdir -p "${TOPDIR}/quectel_build/output"
    rm -f "${TOPDIR}/quectel_build/output/efi.bin"
    cp -L "$KERNEL_FILE" "${TOPDIR}/quectel_build/output/efi.bin"
}

function builddtb() {
    bitbake -c cleanall virtual/kernel && bitbake dtb-qcom-image
    mkdir -p "${TOPDIR}/quectel_build/output"
    rm -f "${TOPDIR}/quectel_build/output/dtb.bin"
    cp -L "$DTB_FILE"    "${TOPDIR}/quectel_build/output/dtb.bin"
}

function enter_rootfs() {
    cd "${TOPDIR}/prebuild"
    ./enter_debian_shell.sh
    cd "${TOPDIR}/"
}

export MACHINE=qcm6490-idp
export DISTRO=qcom-wayland
export FWZIP_PATH="${PWD}/quectel_build/prebuilt_bpfw"
export EXTRALAYERS="meta-qcom-qim-product-sdk"
export QCOM_SELECTED_BSP="custom"
# Build type is selected by the VERSION dimension of buildconfig:
#   buildconfig <proj> <rev> <LINUX|DEBIAN|UBUNTU> DBG   -> DEBUG_BUILD=1
#   buildconfig <proj> <rev> <LINUX|DEBIAN|UBUNTU> STD   -> PERFORMANCE_BUILD=1 (default)
# A bare trailing DEBUG token is accepted as an alias of DBG.
# These exports are used by set_bb_env.sh to write conf/auto.conf.
export DEBUG_BUILD=${DEBUG_BUILD:-0}
export PERFORMANCE_BUILD=${PERFORMANCE_BUILD:-1}

export WS_ROOT="${TOPDIR}"

# Export SECBOOT_ENABLE and WS_ROOT to bitbake environment
export BB_ENV_PASSTHROUGH_ADDITIONS="${BB_ENV_PASSTHROUGH_ADDITIONS} SECBOOT_ENABLE WS_ROOT"


. setup-environment

cat <<EOF

#############################################################
Build command:
    Buildconfig:            buildconfig [project_name] [project_rev] [LINUX|DEBIAN|UBUNTU] [STD|DBG]
    Complete Compilation:   buildall
    Export SDK:             buildsdk [packagename]
    A key generation:       buildpackage
    Flash firmware:         flash [ufs|emmc]
Secboot:
    Enable secboot:         buildconfig [project_name] [project_rev] [LINUX|DEBIAN|UBUNTU] [STD|DBG] SEC
    Secboot package output: buildpackage
#############################################################

EOF

function rebake() {
bitbake $@ -c cleansstate
bitbake $@
}

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
# "curl: (92) HTTP/2 stream was not closed cleanly". Transient errors are
# retried by the shell loop in github_rootfs_download() (NOT by curl's
# --retry, see there).
#
# QPI SDK Studio progress hook: 插件通过扫描 stdout 的 "[QPI-PROGRESS]" 标记
# 渲染下载进度条 (断点续传时 done 从已有 .part 字节数起算)。
#
# 只在下载进度表被抑制时输出 (即 -t 2 为假, 见 github_rootfs_download 的 -sS
# 判定): 插件/CI 没有 tty, 进度表被 -sS 关掉, 全靠这些行看进度; 真终端里 curl
# 自带的进度表本来就是一行原地刷新, 再叠一层每秒 printf 就是刷屏 —— 而且 \r 把
# 新行糊在进度表上, 两行互相覆盖 (实测输出是 "94 1148M ... 452k[QPI-PROGRESS]
# total=..."), 所以两者只留一个。
qpi_progress()
{
    [ -t 2 ] && return 0
    local done="${1:-0}" total="${2:-0}" pct=0
    case "$done" in ''|*[!0-9]*) done=0 ;; esac
    case "$total" in ''|*[!0-9]*) total=0 ;; esac
    [ "$total" -gt 0 ] && pct=$((done * 100 / total))
    [ "$pct" -gt 100 ] && pct=100
    printf '[QPI-PROGRESS] total=%s done=%s pct=%s\n' "$total" "$done" "$pct"
}

# Content-Length of the latest release asset (进度条总大小; 未知时返回 0)
github_rootfs_remote_size()
{
    command -v curl >/dev/null 2>&1 || { echo 0; return 1; }
    curl -sIL --max-time 30 "${GITHUB_ROOTFS_DL_URL}/${1}" 2>/dev/null \
        | awk 'BEGIN{IGNORECASE=1} /^content-length:/{v=$2} END{gsub(/\r/,"",v); print v+0}'
}

github_rootfs_download()
{
    local url="$1" out="$2" total="${3:-0}" curl_extra="" wget_extra="" pid=0 rc=0
    local have_tty="" tick=0
    case "$total" in ''|*[!0-9]*) total=0 ;; esac
    if [ -t 2 ]; then
        have_tty=1
    else
        # Non-interactive: suppress the meter but keep errors visible
        curl_extra="-sS"
        wget_extra="-q"
    fi
    if command -v curl >/dev/null 2>&1; then
        # 进度显示两种模式, 取决于 stderr 有没有终端:
        #   有终端 -> curl 跑前台, 自己画进度表 (单行原地刷新), 不输出进度标记;
        #   无终端 -> 进度表被 -sS 关掉, 后台跑 + 每秒轮询 .part 输出 [QPI-PROGRESS]
        #             标记供插件渲染进度条。
        # 两者不能同时出现: 一个 \r 原地刷新、一个每秒换行, 挤在同一个 tty 上会互相
        # 覆盖, 实测输出是 "94 1148M ... 452k[QPI-PROGRESS] total=..." 这种刷屏。
        #
        # Ctrl+C (原来的孤儿 curl 根因): 交互式 shell 会给后台作业分配独立进程组,
        # Ctrl+C 只发给前台进程组, 所以 `curl &` 收不到信号, 只能靠 trap 补刀。而
        # bash 只在等 `wait` 时才执行 trap —— 原来的轮询循环里是前台 `sleep 1`, trap
        # 根本不执行: 2026-09-22 在 pty 实测, ^C 后 bash 用默认动作退出, curl 的 PPID
        # 变 1 且 .part 继续增长 (跑了一小时的那个孤儿就是这么来的)。所以有终端时
        # 临时关掉 job control 再后台化, curl 留在本 shell 的进程组里 —— Ctrl+C 由
        # 终端直接送到它, 同时 pid 还在手上, 插件"停止"/终端被关 (TERM/HUP 只发给
        # shell) 时 trap 也能补刀。等待一律用 `wait`, 且轮询的 sleep 放后台:
        # 前台命令在跑时 bash 不会执行 trap。
        # ponytail: 顺带把 SIGHUP/TERM 也收进来, 覆盖插件"停止"按钮/关终端
        #
        # 重试放在 shell 层, 不交给 curl 的 --retry: curl 重试时只把文件截断回
        # "本次调用开始时的偏移", 本次已下到的字节全部丢 (curl 7.81 实测; 8.x 才有
        # 自动续传的判断)。从 0 起下的那次调用一旦断流, 重试 = 整包重下:
        # 2026-09-22 实测 1.2GB 下到 1100MB 断流, "Throwing away 1153433600 bytes"
        # 后又从 0 开始。每轮重新起一个 curl, -C - 会按 .part 当前长度重算偏移,
        # 断点接着下。
        local prev_sig="" interrupted="" sig=""
        local attempt=1
        prev_sig=$(trap -p INT TERM HUP)
        # pid 为空时不能裸调 kill (kill 0 = 杀整个进程组)
        trap 'sig=INT; [ -n "$pid" ] && kill "$pid" 2>/dev/null; interrupted=1' INT
        trap 'sig=TERM; [ -n "$pid" ] && kill "$pid" 2>/dev/null; interrupted=1' TERM HUP
        while :; do
            rc=0
            if [ -n "$have_tty" ]; then
                set +m
                curl -fL --http1.1 -C - --max-time 7200 ${curl_extra} -o "$out" "$url" &
                pid=$!
                set -m
                # 进度表由 curl 自己画, 没有要轮询的东西, 直接等 (bash 在 wait 里跑 trap)
                wait "$pid" || rc=$?
            else
                curl -fL --http1.1 -C - --max-time 7200 ${curl_extra} -o "$out" "$url" &
                pid=$!
                while kill -0 "$pid" 2>/dev/null; do
                    qpi_progress "$(stat -c %s "$out" 2>/dev/null || echo 0)" "$total"
                    # sleep 必须放后台: 前台 sleep 会把 Ctrl+C 吃掉, trap 不执行
                    sleep 1 & tick=$!
                    wait "$tick" 2>/dev/null || :
                done
                wait "$pid" || rc=$?
            fi
            [ "$rc" -eq 0 ] && break
            [ -n "$interrupted" ] && break
            case "$rc" in
                33|36)
                    # 服务器不认 Range / 本地件比远端大: 续传无解, 丢掉重下
                    rm -f "$out"
                    ;;
            esac
            [ "$attempt" -ge 6 ] && break
            attempt=$((attempt + 1))
            # 终端里 curl 的进度表刚在屏幕中间留了半行 (\r 未换行), 先清掉再打
            [ -n "$have_tty" ] && printf '\r\033[K'
            echo -e "\033[33;1m[WARN] download interrupted (curl rc=${rc}), retry ${attempt}/6 in 5s, resuming from $(stat -c %s "$out" 2>/dev/null || echo 0) bytes\033[0m"
            sleep 5 & tick=$!
            wait "$tick" 2>/dev/null || :
        done
        eval "${prev_sig:-trap - INT TERM HUP}"
        qpi_progress "$(stat -c %s "$out" 2>/dev/null || echo 0)" "$total"
        [ -n "$interrupted" ] && kill -s "${sig:-INT}" $$
        return "$rc"
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

# Fetch one rootfs tarball from GitHub.
# Freshness signals, in order of trust:
#   - local tarball missing                      -> download
#   - live API digest != local sha256            -> download
#   - API down/rate-limited, live ETag differs   -> download
#   - API down, ETag matches                     -> up to date, no download
#   - API down, no local ETag yet                -> compare Content-Length; equal
#     means up to date (the ETag is then backfilled, so the next run matches)
#   - neither signal reachable                   -> keep local tarball + warn
# The API response is deliberately NOT cached: a cached digest has no expiry,
# so once the API is rate-limited a stale value would outvote a live, matching
# ETag and force a needless 1GB re-download (2026-09-22: a 5-day-old cache did
# exactly that), and it would also fail the post-download sha256 check on the
# file it just fetched.
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
    fi
    [ -z "$json_tmp" ] || rm -f "$json_tmp"

    # 2) API down/rate-limited: compare the live ETag (a cheap CDN HEAD) instead
    local new_etag=""
    [ -n "$remote_sha" ] || new_etag=$(github_rootfs_remote_etag "$asset")

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
        local old_etag=""
        [ -f "$etag_file" ] && old_etag=$(cat "$etag_file")
        if [ -n "$new_etag" ]; then
            if [ -z "$old_etag" ]; then
                # 本地没有 etag 记录 != 远端内容变了 (上一次下载可能是在限流期间做完的,
                # 那时没写 .etag)。API 已经用不了, 手头只剩 Content-Length: 大小一致
                # 就当成同一份, 顺手把 etag 补上, 下次直接走 etag 匹配。否则每跑一次
                # 限流就重下 1.2GB (2026-09-22: ubuntu26 已下完整, 只因缺 .etag 被
                # 判成 "etag changed (none -> ...)" 反复整包重下)。
                local local_size remote_size
                local_size=$(stat -c %s "$tar" 2>/dev/null || echo 0)
                remote_size=$(github_rootfs_remote_size "$asset")
                if [ -n "$remote_size" ] && [ "$remote_size" != "0" ] && [ "$local_size" = "$remote_size" ]; then
                    echo -e "\033[32;1m[INFO] ${asset}: up to date (no local etag, size matches ${remote_size})\033[0m"
                    echo "$new_etag" > "$etag_file"
                else
                    need_download=1
                    reason="size differs (local ${local_size} != remote ${remote_size:-unknown})"
                fi
            elif [ "$old_etag" = "$new_etag" ]; then
                echo -e "\033[32;1m[INFO] ${asset}: up to date (etag match)\033[0m"
            else
                need_download=1
                reason="etag changed (${old_etag} -> ${new_etag})"
            fi
        else
            echo -e "\033[33;1m[WARN] ${asset}: cannot verify against GitHub (API/network unavailable), keep local tarball\033[0m"
        fi
    fi

    if [ "$need_download" -eq 1 ]; then
        echo -e "\033[33;1m[INFO] ${asset}: ${reason}, downloading ${dl_url}\033[0m"
        local part="${tar}.part"
        # Keep an existing .part: curl -C - resumes from it on the next run
        # (a failed download must not force a 1GB restart from zero). A .part
        # left by an *older* release must not be resumed though: -C - appends the
        # new file onto the old bytes, the sha256 check below then always fails,
        # and the whole download is burnt on every retry. Tag the partial with
        # the sha it is meant to become; a different value means it is stale.
        if [ -n "$remote_sha" ]; then
            if [ -f "$part" ] && [ -f "${part}.sha" ] && [ "$(cat "${part}.sha")" != "$remote_sha" ]; then
                echo -e "\033[33;1m[INFO] ${asset}: .part is from an older release, restarting download\033[0m"
                rm -f "$part"
            fi
            echo "$remote_sha" > "${part}.sha"
        fi
        if github_rootfs_download "$dl_url" "$part" "$(github_rootfs_remote_size "$asset")"; then
            # Integrity check when the expected sha256 is known
            if [ -n "$remote_sha" ]; then
                local dl_sha
                dl_sha=$(sha256_of "$part")
                if [ -n "$dl_sha" ] && [ "$dl_sha" != "$remote_sha" ]; then
                    echo -e "\033[31;1m[ERROR] ${asset}: sha256 mismatch after download (${dl_sha} != ${remote_sha}), aborting\033[0m"
                    rm -f "$part" "${part}.sha"
                    return 1
                fi
            fi
            rm -f "$tar"
            mv -f "$part" "$tar"
            rm -f "${part}.sha"
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

    # The buildconfig values (project name / rev, custom name, git commit) are
    # written into /etc/quectel-release by os-release.bbappend, which reads the
    # generated header directly. They are deliberately not mirrored into
    # conf/auto.conf: set_bb_env.sh owns that file and never emits those keys,
    # and sed 's/^KEY = .*$/' can only replace a line, never create it.

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

DESCRIPTION = "Counter-Strike game data + Xash3D FWGS engine (built from GitHub source)"
LICENSE = "CLOSED"

SRC_URI = "file://cs.tar.gz \
           file://cstrike.desktop \
           file://userconfig.cfg \
           file://lib/libopusfile.so.0 \
           file://lib/libopusfile.so.0.4.5 \
           file://menu/mainui_english.txt \
           file://menu/motd.txt \
           gitsm://github.com/FWGS/xash3d-fwgs.git;protocol=https;branch=master \
        "

SRCREV = "c7768c48a692a5b90e1a565906dff9be04c4e67b"

S = "${WORKDIR}"

# waf 构建目录（避免污染源码树）
B = "${WORKDIR}/build"

# waf 构建脚本需要 python3；引擎链接的系统库
DEPENDS = " \
    rsync-native \
    pkgconfig-native \
    libsdl2 \
    freetype \
    libopus \
    opusfile \
    libvorbis \
    libogg \
    bzip2 \
    zlib \
    virtual/libgl \
    python3-native \
"

# waf 脚本位于 ${WORKDIR}/git（gitsm 解包位置），不在 ${S} 根，
# poky 的 waf.bbclass 假定 waf 在 ${S}，故手写 configure/build/install
XASH_WAF = "cd ${WORKDIR}/git && python3 ./waf"

# -8:                       64 位引擎 (aarch64 必需)
# --disable-werror:         交叉编译告警多，避免当作错误
# -P (--enable-packaging):  尊重 --prefix，正确安装
# --sdl-use-pkgconfig:      通过 pkg-config 定位 SDL2
# 注意: 不能传 --disable-rpath! 引擎运行时 dlopen 同目录的 filesystem_stdio.so
# 等插件, 需要 $ORIGIN 路径; 且必须生成 DT_RPATH (旧式, dlopen 会搜索) 而非
# DT_RUNPATH (新式, dlopen 不搜索), 故 LDFLAGS 加 --disable-new-dtags
LDFLAGS:append = " -Wl,--disable-new-dtags"

do_configure() {
    ${XASH_WAF} configure -o ${B} -8 --disable-werror -P --sdl-use-pkgconfig \
        --prefix=${prefix} --bindir=${bindir} --libdir=${libdir}
}

do_compile() {
    ${XASH_WAF} build -o ${B} -j${@oe.utils.parallel_make_argument(d, '%d', limit=64)}
}

# --- 游戏数据安装（tar 包，排除引擎二进制，引擎由下面源码编译提供）---
do_install() {
    install -d ${D}/usr/share/games/cs
    rsync -aHAX --no-owner --no-group --inplace \
        --exclude='xash3d' \
        --exclude='libxash.so' \
        --exclude='libref_gl.so' \
        --exclude='libref_soft.so' \
        --exclude='filesystem_stdio.so' \
        --exclude='libmenu.so' \
        ${S}/Games/ ${D}/usr/share/games/cs/
    chmod 777 ${D}/usr/share/games/cs/ -R
    install -d ${D}/usr/share/applications
    install -m 777 ${S}/cstrike.desktop  ${D}/usr/share/applications
    install -d ${D}/usr/share/games/cs/cstrike
    install -m 777 ${S}/userconfig.cfg  ${D}/usr/share/games/cs/cstrike
}

# --- 引擎安装（直接从 waf build 产物拷贝，平铺到游戏目录，与旧版 tar 布局一致）---
# 不用 `waf install`: S=${WORKDIR} 而源码在 git/ 子目录, waf install 在 pseudo 下
# 访问源码树会触发 pseudo abort (path mismatch); 直接拷产物更简单可靠
do_install:append() {
    install -d ${D}/usr/share/games/cs
    install -m 755 ${B}/game_launch/xash3d ${D}/usr/share/games/cs/xash3d
    for so in ${B}/engine/libxash.so \
              ${B}/ref/gl/libref_gl.so \
              ${B}/ref/soft/libref_soft.so \
              ${B}/filesystem/filesystem_stdio.so \
              ${B}/3rdparty/mainui/libmenu.so; do
        if [ -e "$so" ]; then
            install -m 755 "$so" ${D}/usr/share/games/cs/
        fi
    done

    # libopusfile: 引擎 dlopen 依赖。装到 /usr/lib/ (Yocto 侧)。
    # Debian rootfs 的 apt 版在 /usr/lib/aarch64-linux-gnu/ (ld.so.conf 优先),
    # 两路径共存不冲突; /usr/lib/ 版本作为无 Debian 环境时的兜底。
    # 注意: 不要装 libSDL2! Yocto 的 libsdl2-2.0-0 包已提供 /usr/lib/libSDL2-2.0.so.0,
    # 重复安装会触发 dpkg "trying to overwrite" 冲突 (do_rootfs 失败)。
    install -d ${D}/usr/lib
    install -m 755 ${WORKDIR}/lib/libopusfile.so.0.4.5 ${D}/usr/lib/
    ln -sf libopusfile.so.0.4.5 ${D}/usr/lib/libopusfile.so.0

    # mainui 菜单资源: gfx/fonts(官方 Tahoma/FiraSans) + gfx/shell(菜单头图等 67 张)
    # 来自 xash-extras 子模块(gitsm 自动拉取)。数据包(cs.tar.gz)不含这些,
    # 缺失会导致菜单 UI 异常/无头图。同步到 cstrike 和 valve 两处。
    install -d ${D}/usr/share/games/cs/cstrike/gfx
    rsync -aHAX --no-owner --no-group ${WORKDIR}/git/3rdparty/extras/xash-extras/gfx/ \
        ${D}/usr/share/games/cs/cstrike/gfx/
    install -d ${D}/usr/share/games/cs/valve/gfx
    rsync -aHAX --no-owner --no-group ${WORKDIR}/git/3rdparty/extras/xash-extras/gfx/ \
        ${D}/usr/share/games/cs/valve/gfx/

    # mainui 翻译 + MOTD 修复(motd.txt 原为完整 HTML, 引擎 MOTD 渲染器不支持
    # DOCTYPE/CSS 会显示源码乱码, 替换为纯文本)
    install -m 644 ${WORKDIR}/menu/mainui_english.txt ${D}/usr/share/games/cs/cstrike/resource/
    install -m 644 ${WORKDIR}/menu/motd.txt ${D}/usr/share/games/cs/cstrike/

    # 预置 video.cfg: vid_mode 0 = 自适应全屏桌面模式(跟随屏幕分辨率)
    # 不写死 width/height, 避免 safe mode 640x480 模糊且支持任意分辨率屏幕
    cat > ${D}/usr/share/games/cs/cstrike/video.cfg <<'EOF'
// video.cfg - 自适应全屏配置
fullscreen "1"
vid_maximized "1"
vid_mode "0"
r_refdll "gl"
EOF
}

# 引擎 DT_NEEDED 依赖: libSDL2 由 Yocto libsdl2 包提供, libopusfile 由本包提供
RDEPENDS:${PN} += "libsdl2"

FILES:${PN} += "/usr/share/* /usr/lib/libopusfile*"

INSANE_SKIP:${PN} += "already-stripped arch ldflags file-rdeps dev-so"

FILESEXTRAPATHS:prepend := "${THISDIR}/qcom-pw-pal-plugin:"
FILESEXTRAPATHS:prepend := "${TOPDIR}/../prebuild/audio-profile:"

SRC_URI += " \
    file://0001-pw-pal-plugin-retry-stream-start-until-ADSP-ready.patch \
    file://0002-pw-pal-plugin-serialise-ADSP-init-between-pipewire-instances.patch \
    file://usr/share/pipewire/pipewire.conf.d/pw-pal-plugin.conf \
    file://usr/share/pipewire/pipewire.conf.d/HDMI-Audio-Path.md \
    file://zz-audio-dmaheap-perm.rules \
"

do_install:append () {
    install -d ${D}/usr/lib/aarch64-linux-gnu/pipewire-0.3/
    install -m 0755 ${D}/usr/lib/pipewire-0.3/libpipewire-module-pal.so \
        ${D}/usr/lib/aarch64-linux-gnu/pipewire-0.3/libpipewire-module-pal.so

    # 用 prebuild/audio-profile 版本覆盖原生 pw-pal-plugin.conf，并安装说明文档
    install -d ${D}/usr/share/pipewire/pipewire.conf.d/
    install -m 0644 ${WORKDIR}/usr/share/pipewire/pipewire.conf.d/pw-pal-plugin.conf \
        ${D}/usr/share/pipewire/pipewire.conf.d/pw-pal-plugin.conf
    install -m 0644 ${WORKDIR}/usr/share/pipewire/pipewire.conf.d/HDMI-Audio-Path.md \
        ${D}/usr/share/pipewire/pipewire.conf.d/HDMI-Audio-Path.md

    # pi 用户需访问 /dev/dma_heap/qcom,audio-ml 才能 AGM init(pipewire 启动),
    # 通过 udev 规则在设备创建时赋 666, 不依赖服务启动顺序。
    # 文件名 zz- 前缀保证字典序最后执行: 必须晚于 /usr/lib/udev/rules.d/audio-node.rules
    # (该规则对同一设备设 MODE=0640), 否则会被其覆盖导致权限仍是 640。
    install -d ${D}${sysconfdir}/udev/rules.d/
    install -m 0644 ${WORKDIR}/zz-audio-dmaheap-perm.rules \
        ${D}${sysconfdir}/udev/rules.d/zz-audio-dmaheap-perm.rules
}

FILES:${PN} += "/usr/lib/aarch64-linux-gnu/pipewire-0.3/libpipewire-module-pal.so"
FILES:${PN} += "/usr/share/pipewire/pipewire.conf.d/HDMI-Audio-Path.md"
FILES:${PN} += "${sysconfdir}/udev/rules.d/zz-audio-dmaheap-perm.rules"

INSANE_SKIP:${PN} += "installed-vs-shipped"
# cam-server 的 EzTune 端口 20803 绑的是 INADDR_ANY，LAN 内任何主机都能连上来
# （nmap 扫得到）。源码里端口和绑定地址都是硬编码的常量，没有配置文件 / 环境
# 变量 / 命令行开关可以改，所以只能用 unit override 在 cgroup 层禁掉这段端口的
# bind 能力，让它不再监听。详见 files/10-no-listen.conf 里的注释。
#
# 文件方式（不用 sed 改上游 unit）：额外装一个 .service.d drop-in，
# 上游 cam-server.service 原样保留（它由 meta-qcom-hwe 的 recipe 从 cmake 生成，
# 我们不碰），将来上游加字段也不会被我们的脚本盖掉。
# 上游 FILES 已经含 "/etc/systemd/system/"（见 qcom-camera-server.bb:58），
# 所以 drop-in 不用自己再声明 FILES。
#
# bbappend 跟 .bb 不在同一目录（.bb 在 meta-qcom-hwe），
# 默认 FILESPATH 找不到这边的 files/，必须显式加 FILESEXTRAPATHS。

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append = " file://10-no-listen.conf"

do_install:append() {
    install -d ${D}${sysconfdir}/systemd/system/cam-server.service.d
    install -m 0644 ${WORKDIR}/10-no-listen.conf \
        ${D}${sysconfdir}/systemd/system/cam-server.service.d/10-no-listen.conf
}

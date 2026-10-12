# lgpio 的 rgpiod 默认绑 *:8889，LAN 内任何主机都能远程操作 GPIO。
# 用纯配置的 unit override 把它改绑 127.0.0.1（ExecStart 加 -l -n 127.0.0.1），
# 不引入防火墙、不改源码。详见 files/10-local-only.conf 里的注释。
#
# 文件方式（不用 sed 改上游 unit）：额外装一个 .service.d drop-in，
# 上游 rgpiod.service 原样保留，将来上游加字段也不会被我们的脚本盖掉。
# 注意 /etc/systemd/system/ 不在上游 FILES 里，必须自己声明，否则 QA 报 unpackaged。

SRC_URI:append = " file://10-local-only.conf"

do_install:append() {
    install -d ${D}${sysconfdir}/systemd/system/rgpiod.service.d
    install -m 0644 ${WORKDIR}/10-local-only.conf \
        ${D}${sysconfdir}/systemd/system/rgpiod.service.d/10-local-only.conf
}

FILES:${PN} += "${sysconfdir}/systemd/system/rgpiod.service.d/"

FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append = " \
    file://0001-Allow-updating-files-in-the-boot-directory.patch \
    file://0002-u-boot-add-bootdir-to-the-generated-uEnv.txt.patch \
    file://0003-Add-support-for-directories-instead-of-symbolic-link.patch \
    file://0004-Add-support-for-systemd-boot-bootloader.patch \
    file://0005-ostree-decrease-default-grub.cfg-timeout-and-set-def.patch \
    file://0006-Add-support-systemd-boot-automatic-boot-assesment.patch \
    file://0007-sort-key.patch \
    file://0008-sysroot-deploy-systemd-boot-efi-to-ESP-partition.patch \
"

# Static variant of ostree-prepare-root will work only when an initrd isn't used.
# As qcom bootflow uses initrd to mount rootfs, this configuration will cause boot up issues.
PACKAGECONFIG:remove:qcom = "static"

do_install:append:qcom (){
    if [ ! -d "${D}${sysconfdir}/tmpfiles.d" ]; then
        install -d ${D}${sysconfdir}/tmpfiles.d
    fi
    if ${@bb.utils.contains('DISTRO_FEATURES', 'selinux', 'true', 'false', d)}; then
        echo "t /var/roothome - - - - security.selinux=system_u:object_r:home_root_t:s0" >> ${D}${sysconfdir}/tmpfiles.d/01ostree-tmpfiles.conf
    fi
}

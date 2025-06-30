#!/bin/bash
# set -ex

DEFAULT_CMDLINE="root=/dev/disk/by-partlabel/system rw rootwait console=ttyMSM0,115200n8 earlycon qcom_geni_serial.con_enabled=1 kernel.sched_pelt_multiplier=4 mem_sleep_default=s2idle"

mkdir -p ${TOPDIR}/quectel_build/alpha/tools/pack/image_temp
mkdir -p ${TOPDIR}/quectel_build/alpha/tools/pack/image_temp/mnt
mkdir -p ${TOPDIR}/quectel_build/alpha/output/pack
mkdir -p ${TOPDIR}/quectel_build/alpha/tools/pack/dtb_temp/dtb

# efi.bin
cp ${TOPDIR}/build-qcom-wayland/tmp-glibc/deploy/images/qcm6490-idp/qcom-multimedia-image/efi.bin ${TOPDIR}/quectel_build/alpha/tools/pack
# ukify 工具
cp ${TOPDIR}/build-qcom-wayland/tmp-glibc/sysroots-components/x86_64/systemd-boot-native/usr/bin/ukify ${TOPDIR}/quectel_build/alpha/tools/pack
cp ${TOPDIR}/build-qcom-wayland/tmp-glibc/deploy/images/qcm6490-idp/linuxaa64.efi.stub ${TOPDIR}/quectel_build/alpha/tools/pack
cp ${TOPDIR}/build-qcom-wayland/tmp-glibc/deploy/images/qcm6490-idp/initramfs-ostree-image-qcm6490-idp.cpio.gz ${TOPDIR}/quectel_build/alpha/tools/pack

cp ${TOPDIR}/build-qcom-wayland/tmp-glibc/deploy/images/qcm6490-idp/qcs6490-idp-pi.dtb ${TOPDIR}/quectel_build/alpha/tools/pack/dtb_temp/dtb
# linux Image
cp ${TOPDIR}/build-qcom-wayland/tmp-glibc/deploy/images/qcm6490-idp/Image  ${TOPDIR}/quectel_build/alpha/tools/pack/image_temp

# ukify build
python3.10 ${TOPDIR}/quectel_build/alpha/tools/pack/ukify build \
        --efi-arch=aa64 \
        --stub="${TOPDIR}/quectel_build/alpha/tools/pack/linuxaa64.efi.stub" \
        --linux="${TOPDIR}/quectel_build/alpha/tools/pack/image_temp/Image" \
        --initrd="${TOPDIR}/quectel_build/alpha/tools/pack/initramfs-ostree-image-qcm6490-idp.cpio.gz" \
        --cmdline="${DEFAULT_CMDLINE}" \
        --devicetree="${TOPDIR}/quectel_build/alpha/tools/pack/dtb_temp/dtb/qcs6490-idp-pi.dtb" \
        --output="${TOPDIR}/quectel_build/alpha/tools/pack/image_temp/uki.efi"

cp ${TOPDIR}/quectel_build/alpha/tools/pack/efi.bin ${TOPDIR}/quectel_build/alpha/tools/pack/image_temp

sudo mount -o loop -t vfat  ${TOPDIR}/quectel_build/alpha/tools/pack/image_temp/efi.bin ${TOPDIR}/quectel_build/alpha/tools/pack/image_temp/mnt --options rw
# update
sudo cp ${TOPDIR}/quectel_build/alpha/tools/pack/image_temp/uki.efi  ${TOPDIR}/quectel_build/alpha/tools/pack/image_temp/mnt/ostree/poky-*/vmlinuz-6.6.52-qli-1.3-ver.1.1
sudo umount  ${TOPDIR}/quectel_build/alpha/tools/pack/image_temp/mnt
cp ${TOPDIR}/quectel_build/alpha/tools/pack/image_temp/efi.bin ${TOPDIR}/quectel_build/alpha/output/pack

rm ${TOPDIR}/quectel_build/alpha/tools/pack/image_temp -rf


#!/bin/bash

[ -n "$TOPDIR"   ] || { echo "ERROR: TOPDIR not set, please do source build.sh first"; exit 1; }
[ -z "$1"        ] || { SDK_NAME=$1; }
[ -n "$SDK_NAME" ] || { SDK_NAME=${QUECTEL_PROJECT_REV:?"var not set! source quectel_build/compile/build.sh first please."}"_SDK"; }
[ ! -d $SDK_NAME ] || { echo "ERROR: dir $SDK_NAME exist! you need remove it!"; exit 1; }


set -ev
#[ -f ${TOPDIR}/build-qcom-wayland/tmp-glibc/deploy/sdk/qcom-wayland-x86_64-qcom-multimedia-image-armv8-2a-qcm6490-idp-toolchain-1.0.sh ] || bitbake -c populate_sdk qcom-multimedia-image
[ -f ${TOPDIR}/build-qcom-wayland/tmp-glibc/deploy/sdk/qcom-wayland-x86_64-qcom-multimedia-image-armv8-2a-qcm6490-idp-toolchain-1.3-ver.1.1.sh ] || bitbake -c populate_sdk qcom-multimedia-image
#[ -f ${TOPDIR}/build-qcom-wayland/tmp-glibc/deploy/sdk/qcom-wayland-x86_64-meta-toolchain-armv8-2a-qcm6490-idp-toolchain-1.0.sh        ] || bitbake meta-toolchain
[ -f ${TOPDIR}/build-qcom-wayland/tmp-glibc/deploy/sdk/qcom-wayland-x86_64-meta-toolchain-armv8-2a-qcm6490-idp-toolchain-1.3-ver.1.1.sh ] || bitbake meta-toolchain
mkdir -p $SDK_NAME/patch/include/asm
mkdir -p $SDK_NAME/patch/include/uapi/asm
mkdir -p $SDK_NAME/patch/include/bits
mkdir -p $SDK_NAME/patch/include/generated
mkdir -p $SDK_NAME/patch/scripts/mod
mkdir -p $SDK_NAME/patch/scripts/basic
mkdir -p $SDK_NAME/patch/certs
mkdir -p $SDK_NAME/patch/arch/arm64/tools
#cp    ${TOPDIR}/build-qcom-wayland/tmp-glibc/deploy/sdk/qcom-wayland-x86_64-qcom-multimedia-image-armv8-2a-qcm6490-idp-toolchain-1.0.sh $SDK_NAME/
cp    ${TOPDIR}/build-qcom-wayland/tmp-glibc/deploy/sdk/qcom-wayland-x86_64-qcom-multimedia-image-armv8-2a-qcm6490-idp-toolchain-1.3-ver.1.1.sh $SDK_NAME/
cp -r ${TOPDIR}/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-build-artifacts/arch/arm64/include/generated/asm/*          $SDK_NAME/patch/include/asm/
cp -r ${TOPDIR}/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-build-artifacts/arch/arm64/include/generated/uapi/asm/*     $SDK_NAME/patch/include/uapi/asm/
cp    ${TOPDIR}/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-build-artifacts/include/generated/autoconf.h                $SDK_NAME/patch/include/generated/
cp    ${TOPDIR}/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-build-artifacts/certs/signing_key.x509                      $SDK_NAME/patch/certs/
cp    ${TOPDIR}/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-build-artifacts/certs/signing_key.pem                       $SDK_NAME/patch/certs/
cp    ${TOPDIR}/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/tools/gen-sysreg.awk                      $SDK_NAME/patch/arch/arm64/tools/
cp    ${TOPDIR}/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/tools/sysreg                              $SDK_NAME/patch/arch/arm64/tools/
cp    ${TOPDIR}/quectel_build/compile/setup_sdk.sh_template $SDK_NAME/setup_sdk.sh
tar cvf ${SDK_NAME}.tar $SDK_NAME
set +v
echo "*** Export SDK ${SDK_NAME}.tar SUCCESS. ***"

inherit cmake pkgconfig python3native perlnative cpan-base

LICENSE          = "Qualcomm-Technologies-Inc.-Proprietary"
LIC_FILES_CHKSUM = "file://${QCOM_COMMON_LICENSE_DIR}/${LICENSE};md5=58d50a3d36f27f1a1e6089308a49b403"

DESCRIPTION = "Camx"

SRCPROJECT = "git://qpm-git.qualcomm.com/home2/git/revision-history/platform/vendor/qcom-proprietary/mm-camerasdk-kt.git;protocol=https"
SRCBRANCH  = "${CUST_ID}-camx.qclinux.1.0.r1-rel"
SRCREV     = "e54bcdaf3778baac04e9f1780fa2f4c5e380b7dc"

#SRC_URI  = "${SRCPROJECT};branch=${SRCBRANCH};destsuffix=vendor/qcom/proprietary/chi-cdk-kt"


#S = "${WORKDIR}/vendor/qcom/proprietary/chi-cdk-kt"

FILESPATH:prepend =  "${WORKSPACE}/sources/quectel-src:"
SRC_URI = "file://chicdk-kt"
S = "${WORKDIR}/chicdk-kt/qcom/proprietary/chi-cdk-kt"

# Toolchain to use
TOOLCHAIN = "gcc"

# Common Dependencies
DEPENDS:qcom-custom-bsp += "libxml-simple-perl-native syslog-plumber glib-2.0 property-vault perl-native camx-kt virtual/egl virtual/libgles2 adrenocl glib-2.0 qcom-fastcv-binaries"

# Extra Flags
TARGET_LDFLAGS:append   = " -Wl,--no-undefined"
CAMX_FLAGS  = "-target ${TARGET_SYS} "
CAMX_FLAGS := "-I ${STAGING_INCDIR}/c++ "
CAMX_FLAGS := "-I ${STAGING_INCDIR}/linux-msm/usr/include"
CAMX_FLAGS := "-I ${STAGING_KERNEL_BUILDDIR}/usr/include/vidc"

def config_target_sys(d):
    if d.getVar('PRODUCT', True) == 'ubuntu':
        return "aarch64-linux-gnu"
    else:
        return "${TARGET_ARCH}${TARGET_VENDOR}${@['-' + d.getVar('TARGET_OS'), ''][d.getVar('TARGET_OS') == ('' or 'custom')]}"

TARGET_SYS = "${@config_target_sys(d)}"

# Get the Machine name
def get_platform(d):
    return ""

def get_board_platform(d):
    if d.getVar('SOC_ARCH', True) == 'qcs8550':
        return "sm8550"
    if d.getVar('SOC_ARCH', True) == 'qcm6490':
        return "sm6490"
    if d.getVar('SOC_ARCH', True) == 'qcs9100':
        return "qcs9100"
    if d.getVar('SOC_ARCH', True) == 'qcs8300':
        return "qcs8300"
    else:
        return ""

#Append Extra Flags
OECMAKE_C_FLAGS:append   = " ${CAMX_FLAGS}"
OECMAKE_CXX_FLAGS:append = " ${CAMX_FLAGS}"

EXTRA_OECMAKE = "\
        -DCAMXDEBUG:STRING=True \
        -DPLATFORM:STRING=linux \
        -DCPU:STRING=64 \
        -DCMAKE_CROSSCOMPILING:BOOL=True \
        -DCMAKE_LIBRARY_PATH:PATH=${STAGING_LIBDIR} \
        -DCMAKE_INCLUDE_PATH:PATH=${STAGING_INCDIR} \
        -DKERNEL_INCDIR=${STAGING_INCDIR}/linux-msm \
        -DCAMX_PATH:PATH=${WORKDIR}/camx-kt \
        -DCMAKE_QLI_NAME:STRING=${TARGET_SYS} \
        -DTARGET_BOARD_PLATFORM:STRING=${@get_platform(d)} \
        -DBOARD_PLATFORM:STRING=${@get_board_platform(d)} \
"

PACKAGE_ARCH = "${SOC_ARCH}"

#Use native build perl/python for autogeneration
export PYTHON_USE="${PYTHON}"
export PERLCONFIGTARGET = "${@is_target(d)}"
export PERL_INC = "${STAGING_LIBDIR}${PERL_OWN_DIR}/perl/${@get_perl_version(d)}/CORE"
export PERL_LIB = "${STAGING_LIBDIR}${PERL_OWN_DIR}/perl/${@get_perl_version(d)}"
export PERL_ARCHLIB = "${STAGING_LIBDIR}${PERL_OWN_DIR}/perl/${@get_perl_version(d)}"

CHI_CDK_PATH = "${S}"

# added com.qti.chi.override.so to LD_LIBRARY_PATH to make it available search it in sub folder
EXTRA_OEMAKE += "LD_LIBRARY_PATH=/usr/lib/hw/com.qti.chi.override.so"

# Macros for Autogen Sources Scripts
CHICDKAUTOGEN = "${CHI_CDK_PATH}/core/build/linuxembedded/autogen.sh"

do_autogen() {
    # Run the autogen.sh script to generate source files
    bash ${CHICDKAUTOGEN} ${@get_board_platform(d)}
}

# Run autogen only after code unpacked
do_unpack[postfuncs] += " do_autogen"


do_configure:prepend() {
    find ${CHI_CDK_PATH} -iname *.cpp -exec sed -i 's/\/vendor\/lib64/\/usr\/lib/g' {} +
    find ${CHI_CDK_PATH} -iname *.h -exec sed -i 's/\/vendor\/lib64/\/usr\/lib/g' {} +
    find ${CHI_CDK_PATH} -iname *.c -exec sed -i 's/\/vendor\/lib64/\/usr\/lib/g' {} +
}

do_package_qa[noexec] = "1"

FILES:${PN} = "\
    /usr/lib/* \
    /usr/bin/* \
    /usr/lib/rfsa/adsp/* \
    /usr/include/* \
    /lib/firmware/* \
    /system/etc/camera/* "

RM_WORK_EXCLUDE += "${PN}"
deltask do_rm_work

FILES:${PN}-dev = ""
INSANE_SKIP = "1"
#Skips check for .so symlinks
INSANE_SKIP:${PN} = "already-stripped"

#!/bin/bash

# Quectel buildconfig project
TOPDIR=$(pwd); export TOPDIR

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

function buildpackage()
{ 
    $TOPDIR/quectel_build/a_key_generation.sh $QUECTEL_PROJECT_NAME $QUECTEL_PROJECT_REV $QUECTEL_CUSTOM_NAME
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
    env_check
    if [ ! -f ${TOPDIR}//config/linker/versions ]
    then
        python -B ${TOPDIR}//quectel_build/compile/version_parser_auto.py $*
        if [ $? != 0 ];then
            break
        fi
    fi

    python -B ${TOPDIR}/quectel_build/compile/config_parser.py $*
    if [ $? != 0 ];then
        break
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

function buildall() {
    bitbake qcom-multimedia-image
}

function buildsdk() {
    $TOPDIR/quectel_build/compile/export_sdk.sh $@
}

function do_kernel_images() {
    $TOPDIR/quectel_build/do_image_package.sh
}

if [ ! -f downloads/downloads.done ]; then
   cat downloads/downloads.tar.* | tar -xvf -
   touch downloads/downloads.done
fi

export MACHINE=qcm6490-idp
export DISTRO=qcom-wayland
export FWZIP_PATH="${PWD}/quectel_build/prebuilt_bpfw"
export EXTRALAYERS="meta-qcom-qim-product-sdk"
export QCOM_SELECTED_BSP="custom"


. setup-environment

cat <<EOF

#############################################################
Build command:
    Buildconfig:            buildconfig [project_name] [project_rev] [custom_name]
    Complete Compilation:   buildall
    Export SDK:             buildsdk [packagename]
    A key generation:       buildpackage
#############################################################

EOF

function rebake() {
bitbake $@ -c cleansstate
bitbake $@
}


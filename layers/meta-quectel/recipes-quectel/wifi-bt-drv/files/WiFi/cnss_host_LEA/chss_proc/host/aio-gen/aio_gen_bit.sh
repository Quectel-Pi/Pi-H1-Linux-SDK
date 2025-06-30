#!/bin/bash

# AIO version
AIO_GEN_VER=1.4.2
AIO_VER=$AIO_GEN_VER


#
# General settings
#
TOPDIR=`pwd`
ENGPATCHDIR=${TOPDIR}/../patch
AIO_TOP=${TOPDIR}/../AIO
ENV_MAKEFILE=${AIO_TOP}/build/env.makefile
OSS_ENGPATH_DIR=${AIO_TOP}/../../fixce_oss_patch/patch


### default value  ###

INPUT_BOARD_TYPE=
HOST_DRIVER_VERSION=
BLUETOOTHSTACK=
IF_TYPE=
FW_NEED=0   ## for non-x86 platform, it doesn't need 
ENG_PATCH=0
VERBOSE_MODE=0
DOWNLOAD_BOTH_WLAN_BT=n
RELEASE_CONFIGFILE=
RELEASE_CONFIGFILE_FLAG=

while getopts ":t:w:b:i:k:hevVr" optname
  do                                    
    case "$optname" in
      "t")
        #echo "Option $optname is specified"      
        INPUT_BOARD_TYPE=$OPTARG
        RELEASE_CONFIGFILE=${TOPDIR}/build/scripts/$INPUT_BOARD_TYPE/release.$INPUT_BOARD_TYPE
        ;;
      "r")
        #It is a release version with fixed inforamtion from configure file, then you don't need to specify host driver version
        #bluetooth stack and interface type with parameters
        #we will use these configure later
        RELEASE_CONFIGFILE_FLAG=y
        ;;
      "w")
        #echo "Option $optname has value $OPTARG" 
        HOST_DRIVER_VERSION=$OPTARG
        ;;
      "b")
        #echo "Option $optname has value $OPTARG" 
        BLUETOOTHSTACK=$OPTARG
        ;;
      "e")
        #echo "Option $optname is specified"      
        ENG_PATCH=1
        ;;
      "v")
        if [ -f "$ENV_MAKEFILE" ]
        then
            source "$ENV_MAKEFILE"
        fi
        echo "AIO version: " $AIO_VER
        exit 0
        ;;
      "V")
        VERBOSE_MODE=1
        ;;
      "i")
        IF_TYPE=$OPTARG
        ;;
      "k")
	KTAG=$OPTARG
	;;
      "h")
        echo "./aio_gen_bit.sh -t <board_type> -w <wlan_host_driver_version>  -b <bluetooth_stack> [-i <interface_type>]" 
        echo ""
        echo "or ./aio_gen_bit.sh -t <board_type> -r"
        #example for x86
        echo "Example for x86"
        echo "  #./aio_gen_bit.sh -t x86 -w 4.5.10.013 -b bluez -i USB"
        echo "  #./aio_gen_bit.sh -t cus12-9 -r"
        exit 0
        ;;
      "?")
        echo "Unknown option $OPTARG"
        ;;
      ":")
        echo "No argument value for option $OPTARG"
        ;;
      *)
      # Should not occur
        echo "Unknown error while processing options"
        ;;
    esac
    #echo "OPTIND is now $OPTIND"
  done

if [ "${INPUT_BOARD_TYPE}" == "x86" ] || [ "${INPUT_BOARD_TYPE}" == "x86-android" ]; then
    FW_NEED=1
fi

if [ "${INPUT_BOARD_TYPE}" == "x86" ]; then
    if [ -z ${IF_TYPE} ]; then
        echo "MUST specify IF_TYPE '-i [USB|SDIO|PCIE]' for x86"
        exit 1
    fi
    RELEASE_CONFIGFILE=${TOPDIR}/build/scripts/$INPUT_BOARD_TYPE/release.$INPUT_BOARD_TYPE.$IF_TYPE
fi

if [ "$RELEASE_CONFIGFILE" != "" ] && [ -f $RELEASE_CONFIGFILE ]; then
    if [ "${RELEASE_CONFIGFILE_FLAG}" == "y" ]; then
        source $RELEASE_CONFIGFILE
        echo "HOST_DRIVER_VERSION:"$HOST_DRIVER_VERSION
        echo "BLUETOOTHSTACK:"$BLUETOOTHSTACK
        echo "IF_TYPE:"$IF_TYPE
        echo "WLAN_BUILD_VER:"$WLAN_BUILD_VER
        echo "BT_BUILD_VER:"$BT_BUILD_VER
        echo "BSP_NAME:"$BSP_NAME
    else
        #Just get the WLAN_BUILD_VER and BT_BUILD_VER
        WLAN_BUILD_VER=`grep WLAN_BUILD_VER $RELEASE_CONFIGFILE  | awk -F = '{print $2}'`
        BT_BUILD_VER=`grep BT_BUILD_VER $RELEASE_CONFIGFILE  | awk -F = '{print $2}'`
        BSP_NAME=`grep BSP_NAME $RELEASE_CONFIGFILE  | awk -F = '{print $2}'`
        echo "WLAN_BUILD_VER:"$WLAN_BUILD_VER
        echo "BT_BUILD_VER:"$BT_BUILD_VER
        echo "BSP_NAME:"$BSP_NAME
    fi
elif [ "$RELEASE_CONFIGFILE" == "" ]; then
    echo "Must specify board type (-t). Please use '-h' to get usage in detail."
    exit 1
else
    echo "Please use '-h' to get usage in detail."
    echo "You must create referenced release configure file:"
    echo $RELEASE_CONFIGFILE
    exit 1
fi

if [ $VERBOSE_MODE -eq 1 ]; then
    echo "board_type: " $INPUT_BOARD_TYPE
    echo "driver_version: "  $HOST_DRIVER_VERSION
    echo "bluetooth_stack: " $BLUETOOTHSTACK
    echo "ENG_PATCH: " $ENG_PATCH
fi

# MUST input board type
if [ ! "${INPUT_BOARD_TYPE}" ]; then
	echo "Must specify board type (-t). Please use '-h' to get usage in detail."
	exit 1
fi


# Download path settings

HOST_DRIVER_PATH=" --no-check-certificate http://source.codeaurora.org/external/wlan/qcacld-2.0/snapshot"

CFG80211_VERSION="v3.12.8"
CFG80211_BASE_NAME="backports-3.12.8-1"
CFG80211_BASE_PATH="https://www.kernel.org/pub/linux/kernel/projects/backports/stable"

WPA_SUPPLICANT_NAME="wpa_supplicant_8-AU_LINUX_ANDROID_KK.04.04.04.010.154"
WPA_SUPPLICANT_CAF="https://source.codeaurora.org/quic/la/platform/external/wpa_supplicant_8/snapshot"

HOSTAP_NAME="hostap_2_4"

LIBNL_NAME="libnl-3.2.25"
LIBNL_SERVER="http://www.infradead.org/~tgr/libnl/files"

IW30_NAME="iw-3.0"
IW_SERVER="https://www.kernel.org/pub/software/network/iw"

## declare patch file string
declare -a aio_patch_arr=(
"001-cfg80211-backports-3.12.8-1.patch"
"002-wpa_supplicant-for-aio.patch"
"003-cfg80211-backports-3.16.2-1.patch"
"900-cld-kernel3.8.patch"
"904-barrier_h.patch"
"905-hex2bin.patch"
"906-is_compat_task.patch"
"907-NLA_S32.patch"
"908-rx_buffer_2k.patch"
"909-tasklet_for_usb.patch"
"910-werror.patch"
"911-backports_export_h.patch"
"912-CONFIG_WEXT_SPY.patch"
"913-compat_dma_sgtable.patch"
"914-register_inet6addr.patch"
"915-cld-kernel3.13.patch"
"916-cld-backport-3.16.2.patch"
"917-compat_dma_sgtable_backports-3.16.2-1.patch"
"918-cld-PMF-fix.patch"
"919-cld-cfgini.patch"
"920-144ch_support.patch"
"921-db.txt"
"922-Beacon-VHT-NSS-support.patch"
"923-remove-HT-always-QOS.patch"
)
AIO_PATCH_CAF="https://source.codeaurora.org/patches/external/wlan/fixce/3rdparty/release_2014_12_26"

# For specific board type patch array
BOARD_TYPE_POSTFIX="_aio_patch_arr"



#
# By default, components are NOT downloaded.
# For each board type to conditional download,
# redefine them in the file aio_gen.{$BOARD_TYPE}
#
# 1. patch files
DOWNLOAD_PATCH=n

# 2. kernel
DOWNLOAD_KERNEL_BACKPORT_3_12=n

# 3. WLAN/BT host drivers
DOWNLOAD_DRIVER_WLAN_HOST=n

# 4. APPs
DOWNLOAD_APP_WLAN_WPA_SUPPLICANT_8=n
DOWNLOAD_APP_WLAN_LIBNL_3_2_25=n
DOWNLOAD_APP_WLAN_IW_3_0=n
DOWNLOAD_APP_WLAN_HOSTAP_2_4=n


if [ "$DOWNLOAD_BOTH_WLAN_BT" = "y" ]
then
    # MUST input both: WLAN host driver version and BT stack.
    if [ ! "${HOST_DRIVER_VERSION}" ] || [ ! "${BLUETOOTHSTACK}" ]; then
        echo -e "Must specify both: \n1. WLAN host driver version (-w).\n2. Bluetooth stack (-b). \nPlease use '-h' to get usage in detail."
        exit 1
    fi
else
    # MUST input one of the two, or both: WLAN host driver version and BT stack.
    if [ ! "${HOST_DRIVER_VERSION}" ] && [ ! "${BLUETOOTHSTACK}" ]; then
        echo -e "Must specify one of the two or both: \n1. WLAN host driver version (-w).\n2. Bluetooth stack (-b). \nPlease use '-h' to get usage in detail."
        exit 1
    fi
fi


if [ $FW_NEED -eq 1 ] && [ ! -d ${TOPDIR}/../firmware/WLAN-firmware ]; then
echo -e ''
echo -e '!!! No WLAN Firmware !!!' 
echo -e 'Firmware not exist on "${TOPDIR}/../firmware/WLAN-firmware" directory ... '  
echo -e 'Please download and save in "firmware/WLAN-firmware" directory in the same level as the folder aio-gen'
echo -e 'then try again .... '
echo -e ''
exit 1
fi

if [ $FW_NEED -eq 1 ] && [ ! -d ${TOPDIR}/../firmware/BT-firmware ]; then
echo -e ''
echo -e '!!! No BT Firmware !!!' 
echo -e 'Firmware not exist on "${TOPDIR}/../firmware/BT-firmware" directory ... '  
echo -e 'Please download and save in "firmware/BT-firmware" directory in the same level as the folder aio-gen'
echo -e 'then try again .... '
echo -e ''
exit 1
fi

## for make the driver/Makefile  firmware install happy
mkdir -p ${TOPDIR}/../firmware/WLAN-firmware
mkdir -p ${TOPDIR}/../firmware/BT-firmware


#
# Create AIO package file structure
#
echo "=============Generate AIO package..."
install -d ${AIO_TOP}
install -d ${AIO_TOP}/apps
install -d ${AIO_TOP}/drivers/firmware/
install -d ${AIO_TOP}/drivers/patches

# Include board type script here
BOARD_TYPE_SCRIPT=${TOPDIR}/build/scripts/${INPUT_BOARD_TYPE}/aio_gen.${INPUT_BOARD_TYPE}
if [ -f $BOARD_TYPE_SCRIPT ]; then
    source $BOARD_TYPE_SCRIPT
fi

#
# Get the whole version
#
AIO_VER=${AIO_GEN_VER}.${BRD_TYPE_VER}.${WLAN_BUILD_VER}.${BT_BUILD_VER}

if [ "$DOWNLOAD_PATCH" == "y" ] 
then
    echo "===================================="
    echo " Downloading patch files ..."
    echo "===================================="
    if [ -d "${TOPDIR}/drivers/patches" ]
    then
        rm -rf ${TOPDIR}/drivers/patches.old
        mv "${TOPDIR}/drivers/patches" "${TOPDIR}/drivers/patches.old"
    fi

    install -d ${TOPDIR}/drivers/patches
    if [ $ENG_PATCH == 0 ]
    then
        # Board type specific patches
        if [ -n "$BOARD_TYPE_AIO_PATCH_CAF" ]
        then
            mkdir -p ${TOPDIR}/drivers/patches/${INPUT_BOARD_TYPE}
            BOARD_TYPE_PATCH_ARR=${BOARD_TYPE_PREFIX}${BOARD_TYPE_POSTFIX}
            tmp_arr=$(eval echo \${$BOARD_TYPE_PATCH_ARR[*]})
            for i in ${tmp_arr[@]}
            do
                wget ${BOARD_TYPE_AIO_PATCH_CAF}/"$i"
                patch_name=`basename $i`
                patch_dir=`dirname $i`
                mkdir -p ${TOPDIR}/drivers/patches/${INPUT_BOARD_TYPE}/${patch_dir}
                mv $patch_name ${TOPDIR}/drivers/patches/${INPUT_BOARD_TYPE}/${patch_dir}/
            done
        fi
        if [ "$INPUT_BOARD_TYPE" == "x86-android" ]
        then
            cp -fr ${ENGPATCHDIR}/android_bsp_patch/$BSP_NAME/WLAN/* ${TOPDIR}/drivers/patches/${INPUT_BOARD_TYPE}/WLAN/
            cp -fr ${ENGPATCHDIR}/android_bsp_patch/$BSP_NAME/BT/* ${TOPDIR}/drivers/patches/${INPUT_BOARD_TYPE}/BT/
        else
            # Copy NDS files to board type specific patches directory
            # Notice: Only x86-android doesn't have boardtype folder under drivers, other board type should copy NDS files to AIO/driver/patches/${INPUT_BOARD_TYPE}
            cp -fr ${ENGPATCHDIR}/${INPUT_BOARD_TYPE}/* ${TOPDIR}/drivers/patches/${INPUT_BOARD_TYPE}/
        fi
    else
        # Copy common patches and board type specific patches together
        cp -fr ${ENGPATCHDIR}/* ${TOPDIR}/drivers/patches/
	cp -fr ${OSS_ENGPATH_DIR}/* ${TOPDIR}/drivers/patches/
    fi
fi

#
# Copy files
#
echo "=============Copy files..."
cp -rf ${TOPDIR}/build ${AIO_TOP}
cp -rf ${TOPDIR}/rootfs ${AIO_TOP}
cp -rf ${TOPDIR}/drivers/patches ${AIO_TOP}/drivers/ 2>/dev/null || :
cp ${TOPDIR}/drivers/Makefile ${AIO_TOP}/drivers 
cp ${TOPDIR}/drivers/Makefile.ath6kl ${AIO_TOP}/drivers 

#
# Download host driver source codes
#
if [ "$DOWNLOAD_DRIVER_WLAN_HOST" == "y" ]
then
    echo "========================================================"
    echo " Downloading WLAN host driver ${HOST_DRIVER_VERSION} ..."
    echo "========================================================"
    wget ${HOST_DRIVER_PATH}/${HOST_DRIVER_VERSION}.tar.gz
    tar -zxf ${HOST_DRIVER_VERSION}.tar.gz
    cp -rf ${HOST_DRIVER_VERSION} ${AIO_TOP}/drivers
    mv ${AIO_TOP}/drivers/${HOST_DRIVER_VERSION} ${AIO_TOP}/drivers/qcacld-new
    rm -rf ${HOST_DRIVER_VERSION}
    rm ${HOST_DRIVER_VERSION}.tar.gz
fi

#
# Copy host driver config ini to firmware_bin
#
cp ${AIO_TOP}/drivers/patches/${INPUT_BOARD_TYPE}/wlan/WCNSS_qcom_cfg.usb.ini ${AIO_TOP}/drivers/qcacld-new/firmware_bin 2>/dev/null || :

#
#Append the WLAN_BUILD_VER to QWLAN_VERSIONSTR in CORE/MAC/inc/qwlan_version.h
#
if [ "$WLAN_BUILD_VER" != "" ] && [ -f  ${AIO_TOP}/drivers/qcacld-new/CORE/MAC/inc/qwlan_version.h ];
then
    echo "========================================================"
    echo " Append the WLAN_BUILD_VER to QWLAN_VERSIONSTR in CORE/MAC/inc/qwlan_version.h"
    echo "========================================================"
    qwlan_build_version=`grep "QWLAN_VERSIONSTR"  ${AIO_TOP}/drivers/qcacld-new/CORE/MAC/inc/qwlan_version.h| awk -v ver=$WLAN_BUILD_VER  '{printf("%s\".%s\"", $3,ver)}'`
    sed -i 's/QWLAN_VERSIONSTR\s\+\".*\"/QWLAN_VERSIONSTR             '''$qwlan_build_version'''/' ${AIO_TOP}/drivers/qcacld-new/CORE/MAC/inc/qwlan_version.h
fi

#
# Append the BT_BUILD_VER to bt_host_ver.h
#
if [ "$BT_BUILD_VER" != "" ]
then
    echo "========================================================"
    echo " Append the BT_BUILD_VER to bt_host_ver.h"
    echo "========================================================"
    echo "#define BT_HOST_VERSION \""$BT_BUILD_VER"\"" > ${AIO_TOP}/drivers/patches/bt_host_ver.h
fi

#
# Download backports for cfg80211 
#
if [ "$DOWNLOAD_KERNEL_BACKPORT_3_12" == "y" ]
then
    echo "===================================="
    echo " Downloading backports 3.12.8 ..."
    echo "===================================="
    wget ${CFG80211_BASE_PATH}/${CFG80211_VERSION}/${CFG80211_BASE_NAME}.tar.gz
    tar -zxf ${CFG80211_BASE_NAME}.tar.gz
    mv ${CFG80211_BASE_NAME} ${AIO_TOP}/drivers/backports
    if [ "${BLUETOOTHSTACK}" == "bluez" ] || [ "${BLUETOOTHSTACK}" == "BLUEZ" ]; then
        cp -f ${TOPDIR}/drivers/backports_bt_enabled.config ${AIO_TOP}/drivers/backports/
    fi
    cp -f ${TOPDIR}/drivers/.config ${AIO_TOP}/drivers/backports
    rm ${CFG80211_BASE_NAME}.tar.gz
    sed -i '/source \"drivers\/net\/ethernet\/.roa.com\/Kconfig\"/s/^/#/' ${AIO_TOP}/drivers/backports/drivers/net/ethernet/Kconfig

fi

#
# Download wpa_supplicant_8
#
if [ "$DOWNLOAD_APP_WLAN_WPA_SUPPLICANT_8" == "y" ]
then
    echo "===================================="
    echo " Downloading wpa_supplicant_8 ..."
    echo "===================================="
    wget ${WPA_SUPPLICANT_CAF}/${WPA_SUPPLICANT_NAME}.tar.gz
    tar -zxf ${WPA_SUPPLICANT_NAME}.tar.gz
    mv -f ${WPA_SUPPLICANT_NAME} ${AIO_TOP}/apps
    mv ${AIO_TOP}/apps/${WPA_SUPPLICANT_NAME} ${AIO_TOP}/apps/wpa_supplicant_8
    rm -f ${WPA_SUPPLICANT_NAME}.tar.gz
fi

#
# Download libnl-3.2.25
#
if [ "$DOWNLOAD_APP_WLAN_LIBNL_3_2_25" == "y" ]
then
    echo "===================================="
    echo " Downloading libnl.3.2.25 ..."
    echo "===================================="
    wget ${LIBNL_SERVER}/${LIBNL_NAME}.tar.gz
    tar -zxf ${LIBNL_NAME}.tar.gz
    mv ${LIBNL_NAME} ${AIO_TOP}/apps
    rm -f ${LIBNL_NAME}.tar.gz
fi

#
# Download iw-3.0
#
if [ "$DOWNLOAD_APP_WLAN_IW_3_0" == "y" ]
then
    echo "===================================="
    echo " Downloading iw-3.0 ..."
    echo "===================================="
    wget ${IW_SERVER}/${IW30_NAME}.tar.gz
    tar -zxf ${IW30_NAME}.tar.gz
    mv -f ${IW30_NAME} ${AIO_TOP}/apps
    rm -f ${IW30_NAME}.tar.gz
fi

#
# Download hostap_2_4
#
if [ "$DOWNLOAD_APP_WLAN_HOSTAP_2_4" == "y" ]
then
    echo "===================================="
    echo " Downding hostap_2_4 ..."
    echo "===================================="
    wget http://w1.fi/cgit/hostap/snapshot/${HOSTAP_NAME}.tar.gz
    tar xzf ${HOSTAP_NAME}.tar.gz
    mv -f ${HOSTAP_NAME}  ${AIO_TOP}/apps
    rm -f ${HOSTAP_NAME}.tar.gz
fi


#
# Download BlueZ
#
DOWNLOAD_BLUEZ=
if [ "$BLUETOOTHSTACK" = "bluez" ]
then
    echo "============================================"
    echo " Downloading BlueZ Patches ..."
    echo "============================================"
    get_backports_source_patch
    get_bluez_source_patch
fi


echo "===================================="
echo " Copy firmware into AIO  ..."
echo "===================================="

cp -fr ${TOPDIR}/../firmware ${AIO_TOP}/drivers/


#
# export some environment variables
#
if [ ! -e "$ENV_MAKEFILE" ]
then
    touch "$ENV_MAKEFILE"
fi
:>${ENV_MAKEFILE}
exec 6>&1
exec >${ENV_MAKEFILE}
echo "export AIO_VER=${AIO_VER}"
echo "export BOARD_TYPE=${INPUT_BOARD_TYPE}"
echo "export HOST_DRIVER_VERSION=${HOST_DRIVER_VERSION}"
echo "export BLUETOOTHSTACK=${BLUETOOTHSTACK}"
echo "export BOARD_TYPE_AIO_PATCH_CAF=${BOARD_TYPE_AIO_PATCH_CAF}"
echo "export IF_TYPE=${IF_TYPE}"
echo "export ENG_PATCH=${ENG_PATCH}"
echo "export KTAG=${KTAG}"
exec 1>&6 6>&-

echo "=============Generate AIO Done"


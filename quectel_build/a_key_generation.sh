#!/bin/bash
#author by :Zenith.zhang
#describe:A key produting version

#date=$(date +%t)
#echo "=====Build time:"$(date)"===="
#start='date'

BUILD_CONFIG_GEN_FILE="$TOPDIR/quectel_build/compile/quectel-features-config/quectel-buildconfig-gen.h"
SECBOOT_SCRIPT="$TOPDIR/sectools/build_qcm6490_firmware_secboot.sh"
SECBOOT_SIGN_SCRIPT="$TOPDIR/sectools/Unpacking_Tool/sign_files/sign_files.sh"
SECBOOT_SIGN_OUT_DIR="$TOPDIR/sectools/Unpacking_Tool/sign_out"

is_secboot_enabled()
{
    if [ "${SECBOOT_ENABLE}" = "1" ]; then
        return 0
    fi

    if [ -f "$BUILD_CONFIG_GEN_FILE" ] && \
       grep -Eq '^[[:space:]]*#define[[:space:]]+SECBOOT_ENABLE[[:space:]]+1([[:space:]]|$)' "$BUILD_CONFIG_GEN_FILE"; then
        return 0
    fi

    return 1
}

derive_target_dir_name()
{
    local project_token board_token build_token linux_token custom_token
    local bp_token modem_token release_token module_seq
    local linux_major linux_minor linux_patch linux_patch_token
    local project_suffix custom_suffix target_name token
    local -a custom_tokens

    project_token="${VERSION_NUMBER%%_*}"
    build_token="$(printf '%s' "$VERSION_NUMBER" | cut -d'_' -f2)"
    linux_token="$(printf '%s' "$VERSION_NUMBER" | grep -o 'LP[0-9][0-9A-Z]*\.[0-9][0-9A-Z]*\.[0-9][0-9A-Z]*' | head -n1)"
    custom_token=""

    IFS='/' read -r -a custom_tokens <<EOF
$CUSTOM_ID
EOF
    for token in "${custom_tokens[@]}"; do
        case "${token}" in
            ""|"SEC"|"OL")
                ;;
            "ST")
                custom_token="STD"
                break
                ;;
            *)
                custom_token="${token}"
                break
                ;;
        esac
    done

    # For current QSM565DWF projects, the project prefix in PROJECT_REV is 8 chars
    # like SG565DWF/SD565DWF. Keep the board suffix and replace the prefix with
    # QUECTEL_PROJECT_NAME.
    if [ ${#project_token} -gt 8 ]; then
        project_suffix="${project_token:8}"
    else
        project_suffix=""
    fi
    board_token="${PROJECT_NAME}${project_suffix}"

    bp_token="$(printf '%s' "$build_token" | grep -o 'BP[0-9][0-9]' | head -n1)"
    modem_token="$(printf '%s' "$build_token" | grep -o 'M[0-9][0-9]' | head -n1)"
    release_token="$(printf '%s' "$build_token" | grep -o 'V[0-9][0-9]' | tail -n1)"
    if [ -z "$release_token" ]; then
        release_token="$(printf '%s' "$VERSION_NUMBER" | tr '_' '\n' | grep -E '^V[0-9][0-9]$' | tail -n1)"
    fi

    if [ -n "$modem_token" ]; then
        printf -v module_seq "%03d" "$((10#${modem_token#M}))"
    else
        module_seq="000"
    fi

    if [ -n "$linux_token" ]; then
        linux_token="${linux_token#LP}"
        IFS='.' read -r linux_major linux_minor linux_patch_token <<EOF
$linux_token
EOF
        linux_patch="$(printf '%s' "$linux_patch_token" | sed 's/^0*//')"
        if [ -z "$linux_patch" ]; then
            linux_patch="0"
        fi
    else
        linux_major="0"
        linux_minor="0"
        linux_patch="0"
    fi

    target_name="${board_token}_${bp_token:-BP00}.${module_seq}_Linux${linux_major}.${linux_minor}.${linux_patch}_${release_token:-V00}"

    case "$custom_token" in
        ""|"STD"|"ST"|"SEC")
            ;;
        *)
            custom_suffix="$(printf '%s' "$custom_token" | tr '/' '_' )"
            target_name="${target_name}_${custom_suffix}"
            ;;
    esac

    echo "$target_name"
}

run_secboot_cmd()
{
    local label="$1"
    shift

    echo "$label"
    WS_ROOT="$TOPDIR" \
    PACKAGED_BP_DIR="$TOPDIR/quectel_build/packaged_file" \
    SIGN_OUT_DIR="$SECBOOT_SIGN_OUT_DIR" \
    AP_SIGN_OUT_DIR="$SECBOOT_SIGN_OUT_DIR/ap" \
    BP_SIGN_OUT_DIR="$SECBOOT_SIGN_OUT_DIR/bp" \
    STAGE_ROOT="$SECBOOT_STAGE_DIR" \
    "$@"
}

cleanup_non_sec_secboot_artifacts()
{
    if [ -d "${TARGET_DIR}/efuse" ]; then
        echo "Removing stale SECBOOT efuse artifacts from non-SEC package: ${TARGET_DIR}/efuse"
        rm -rf "${TARGET_DIR}/efuse"
    fi
}

# Check number of parameters
if [ "$#" -ne 3 ]; then
    echo "err: Please provide the project name and version number,custom id"
    echo "eg: ./a_key_generation.sh <QSM565DWF> <SG565DWFPARL1A01_BL01BP01K0M01_QDP_LP6.6.0XX.01.00X_V0X> <Custom ID>"
    exit 1
fi

# get parameters
PROJECT_NAME=$1
VERSION_NUMBER=$2
CUSTOM_ID=$3

# According to the different requirements of the first parameter processing
if [ "$PROJECT_NAME" = "QSM565DWF" ] ; then
    echo "Process QSM565DWF type items"
else
    echo "Error: Unknown project name.$PROJECT_NAME"
fi

# generate TARGET_DIR
TARGET_DIR_NAME="$(derive_target_dir_name)"
TARGET_DIR="$TOPDIR/quectel_build/${VERSION_NUMBER}"
SECBOOT_STAGE_DIR="$TARGET_DIR"

# Output generating TARGET_DIR
echo "TARGET_DIR_NAME: $TARGET_DIR_NAME"
echo "TARGET_DIR: $TARGET_DIR"
mkdir -p "$TARGET_DIR"

AP_VERSION_FILE="$TOPDIR/build-qcom-wayland/tmp-glibc/deploy/images/qcm6490-idp/${TARGET_IMAGE}"


if [ ! -d "$AP_VERSION_FILE" ]; then
  echo "$AP_VERSION_FILE is not exist"
  exit 1
fi

if is_secboot_enabled; then
  echo "===============SECBOOT enabled, signing BP + AP==============="
  export SECBOOT_ENABLE=1
  export QUECTEL_PROJECT_REV="${VERSION_NUMBER}"

  if [ ! -f "$SECBOOT_SCRIPT" ]; then
    echo "$SECBOOT_SCRIPT is not exist"
    exit 1
  fi

  if [ ! -f "$SECBOOT_SIGN_SCRIPT" ]; then
    echo "$SECBOOT_SIGN_SCRIPT is not exist"
    exit 1
  fi

  run_secboot_cmd "[1/4] Signing BP..." bash "$SECBOOT_SCRIPT" --qfil-full
  run_secboot_cmd "[2/4] Signing AP..." bash "$SECBOOT_SCRIPT" --ap

  echo "[3/4] Generating sec.elf..."
  WS_ROOT="$TOPDIR" \
  SIGN_OUT_DIR="$SECBOOT_SIGN_OUT_DIR" \
  AP_SIGN_OUT_DIR="$SECBOOT_SIGN_OUT_DIR/ap" \
  BP_SIGN_OUT_DIR="$SECBOOT_SIGN_OUT_DIR/bp" \
  DOWNLOAD_PATCH="$SECBOOT_SIGN_OUT_DIR/bp" \
  bash "$SECBOOT_SIGN_SCRIPT" sec.elf

  run_secboot_cmd "[4/4] Staging signed package..." bash "$SECBOOT_SCRIPT" --stage

  if [ ! -d "$SECBOOT_STAGE_DIR" ]; then
    echo "$SECBOOT_STAGE_DIR is not exist"
    exit 1
  fi

  rm -rf "${TARGET_DIR}/ap"
  if [ -f "${SECBOOT_SIGN_OUT_DIR}/bp/efuse/sec.elf" ]; then
    mkdir -p "${TARGET_DIR}/efuse"
    cp -f "${SECBOOT_SIGN_OUT_DIR}/bp/efuse/sec.elf" "${TARGET_DIR}/efuse/sec.elf"
  elif [ -f "${SECBOOT_SIGN_OUT_DIR}/bp/sec.elf" ]; then
    mkdir -p "${TARGET_DIR}/efuse"
    cp -f "${SECBOOT_SIGN_OUT_DIR}/bp/sec.elf" "${TARGET_DIR}/efuse/sec.elf"
  fi

  if [ -d "${TARGET_DIR}/efuse" ]; then
    rm -rf "${TARGET_DIR}/bp"
    rm -f "$TARGET_DIR"/prog_firehose_ddr.elf
    rm -f "$TARGET_DIR"/prog_firehose_lite.elf
  fi

  minu_time=$(($SECONDS/60))
  sec_time=$(($SECONDS%60))
  echo "===============Build version success and build_time:"$minu_time"m"$sec_time"s==============="
  exit 0
fi

cleanup_non_sec_secboot_artifacts

echo "===============copy image begin=============="
 
rm -f $AP_VERSION_FILE/prog_firehose*

rsync -av "$AP_VERSION_FILE"/* "$TARGET_DIR"
if [[ $? -eq 0 ]];
then 
   echo "copy ap version scuessful" 
else 
   echo "copy ap version fail"
   exit -1 ;
fi

cp -r $TOPDIR/quectel_build/packaged_file/bootbinaries/*  "$TARGET_DIR"

if [[ $? -eq 0 ]];
then 
   echo "copy bootbinaries to TARGET_DIR scuessful" 
else 
   echo "copy   bootbinaries to TARGET_DIR fail"
   exit -1 ;
fi

cp -r $TOPDIR/quectel_build/packaged_file/firehose/*  "$TARGET_DIR"
if [[ $? -eq 0 ]];
then 
   echo "copy prog_firehose to TARGET_DIR scuessful" 
else 
   echo "copy  prog_firehose to TARGET_DIR fail"
   exit -1 ;
fi

cp -r $TOPDIR/quectel_build/packaged_file/partition/*  "$TARGET_DIR"
if [[ $? -eq 0 ]];
then 
   echo "copy partition to TARGET_DIR scuessful" 
else 
   echo "copy  partition to TARGET_DIR fail"
   exit -1 ;
fi

#echo "=====Build time:"$(date)"===="
minu_time=$(($SECONDS/60))
sec_time=$(($SECONDS%60))

echo "===============Build version success and build_time:"$minu_time"m"$sec_time"s==============="

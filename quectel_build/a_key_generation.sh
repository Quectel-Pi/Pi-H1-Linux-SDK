#!/bin/bash
#author by :Zenith.zhang
#describe:A key produting version

#date=$(date +%t)
#echo "=====Build time:"$(date)"===="
#start='date'

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
TARGET_DIR="$TOPDIR/quectel_build/${VERSION_NUMBER}"

# Output generating TARGET_DIR
echo "TARGET_DIR: $TARGET_DIR"

AP_VERSION_FILE="$TOPDIR/build-qcom-wayland/tmp-glibc/deploy/images/qcm6490-idp/qcom-multimedia-image"

if [ ! -d "$AP_VERSION_FILE" ]; then
  echo "$AP_VERSION_FILE is not exist"
  exit 1
fi

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


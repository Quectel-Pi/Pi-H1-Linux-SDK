FW_ARTIFACTORY = "https://softwarecenter.qualcomm.com/nexus/generic/software/chip/qualcomm_linux-spf-1-0/qualcomm-linux-spf-1-0_test_device_public"
FW_BUILD_ID    = "r1.0_00114.0/qcm6490-le-1-0"
FW_BIN_PATH    = "common/build/ufs/bin"
QUECTEL_PREBUILT_BPFW_DIR := "${THISDIR}/../../../../quectel_build/prebuilt_bpfw"

QCM6490_FW_BUILD_ID ?= "r1.0_00114.0/qcm6490-le-1-0"
QCM6490_FW_BIN_PATH ?= "common/build/ufs/bin"
QCM6490_HLOSFW_SHA256SUM ?= "3b5727de98328506f959eefe5516d3a5b2dfacd7c9f9eebc8e3f2fa0b721d179"
QCM6490_DSPSO_SHA256SUM ?= "26f67d906046d7527d421bedab18be783ca6580247519c7b7fa0bf5dbcdf4088"

# Fetch a shallow mirror tarball.
BB_GIT_SHALLOW       = "1"
# Fetch only the top commit
BB_GIT_SHALLOW_DEPTH = "1"
# Generate shallow mirror tarballs
BB_GENERATE_SHALLOW_TARBALLS = "1"

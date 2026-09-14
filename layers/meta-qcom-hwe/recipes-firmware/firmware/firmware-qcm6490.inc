FW_ARTIFACTORY = "https://softwarecenter.qualcomm.com/nexus/generic/software/chip/qualcomm_linux-spf-1-0/qualcomm-linux-spf-1-0_test_device_public"
FW_BUILD_ID    = "r1.0_00114.0/qcm6490-le-1-0"
FW_BIN_PATH    = "common/build/ufs/bin"
QUECTEL_PREBUILT_BPFW_DIR := "${THISDIR}/../../../../quectel_build/prebuilt_bpfw"

# QCM6490_fw.zip（212MB HLOSFW 包）不进公开仓库：
#   内网/离线构建树里带这个包 -> 用本地包（与历史验证过的构建逐字节一致）
#   公开 clone 没有这个包    -> 从 Qualcomm 公开 artifactory 下载 QCM6490_FW_BUILD_ID 版本
QCM6490_FW_LOCAL_ZIP := "${QUECTEL_PREBUILT_BPFW_DIR}/QCM6490_fw.zip"

def qcm6490_fw_src_uri(d):
    import os
    local = d.getVar('QCM6490_FW_LOCAL_ZIP')
    if os.path.exists(local):
        return "file://%s;name=QCM6490_fw" % local
    return "%s/%s/%s/QCM6490_fw.zip;name=QCM6490_fw" % (
        d.getVar('FW_ARTIFACTORY'), d.getVar('QCM6490_FW_BUILD_ID'), d.getVar('QCM6490_FW_BIN_PATH'))

def qcm6490_hlosfw_sha256(d):
    import os
    if os.path.exists(d.getVar('QCM6490_FW_LOCAL_ZIP')):
        return "3b5727de98328506f959eefe5516d3a5b2dfacd7c9f9eebc8e3f2fa0b721d179"
    return "76c04d671335d3acc5a10c797a987ff7f6dfb43ab1cf86df12b45b49b0382c30"

QCM6490_FW_BUILD_ID ?= "r1.0_00114.0/qcm6490-le-1-0"
QCM6490_FW_BIN_PATH ?= "common/build/ufs/bin"
QCM6490_HLOSFW_SHA256SUM ?= "${@qcm6490_hlosfw_sha256(d)}"
QCM6490_DSPSO_SHA256SUM ?= "26f67d906046d7527d421bedab18be783ca6580247519c7b7fa0bf5dbcdf4088"

# Fetch a shallow mirror tarball.
BB_GIT_SHALLOW       = "1"
# Fetch only the top commit
BB_GIT_SHALLOW_DEPTH = "1"
# Generate shallow mirror tarballs
BB_GENERATE_SHALLOW_TARBALLS = "1"

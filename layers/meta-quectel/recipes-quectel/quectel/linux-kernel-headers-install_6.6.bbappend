# 改用 SDK 自带的本地内核源码，不再从 git.codelinaro.org 拉取整个内核仓库。
LIC_FILES_CHKSUM = "file://${WORKSPACE}/sources/quectel-src/kernel/qcom-6.6/COPYING;md5=6bc538ed5bd9a7fc9398086aedcd7e46"

FILESPATH =+ "${WORKSPACE}/sources/quectel-src/kernel:"
SRC_URI = "file://qcom-6.6"

S = "${WORKDIR}/qcom-6.6"

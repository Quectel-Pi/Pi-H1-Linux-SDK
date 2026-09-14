FILESEXTRAPATHS:prepend := "${THISDIR}/${BPN}:"

SRC_URI:append:qcom= " file://0001-nmcli-avoid-automatic-pager-for-colored-output.patch"


PACKAGECONFIG:qcom = "udev qrtr"

FILESEXTRAPATHS:append := "${THISDIR}/patches:"

SRC_URI:append:qcom = " file://0001-collection-basic-add-WMS-Get-routes.patch "


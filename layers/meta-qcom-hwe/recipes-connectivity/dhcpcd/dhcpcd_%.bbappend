# Disable Automatic IP assignment for rmnet_ipa,qmapmux interfaces
do_install:append:qcom () {
    echo "" >> ${D}${sysconfdir}/dhcpcd.conf
    echo "# To avoid automatic IP assignment when connected to cellular network," >> ${D}${sysconfdir}/dhcpcd.conf
    echo "# denied auto assignment to qmapmux, rmnet_ipa and wlan interfaces." >> ${D}${sysconfdir}/dhcpcd.conf
    echo "denyinterfaces rmnet_ipa* qmapmux* wlan*" >> ${D}${sysconfdir}/dhcpcd.conf
}

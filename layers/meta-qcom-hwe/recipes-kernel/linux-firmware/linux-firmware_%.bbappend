do_install:append:qcom () {

# Removing WCN6855, as it will be a symlink to QCA6698AQ
rm -rf ${D}${nonarch_base_libdir}/firmware/ath11k/WCN6855

ln -sf ${nonarch_base_libdir}/firmware/ath11k/QCA6698AQ ${D}${nonarch_base_libdir}/firmware/ath11k/WCN6855
}

FILES:${PN}-ath11k-qca6698aq += "${nonarch_base_libdir}/firmware/ath11k/WCN6855"

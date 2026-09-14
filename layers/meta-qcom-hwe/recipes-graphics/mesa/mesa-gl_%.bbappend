do_install:append:qcom-custom-bsp() {
    rm -rf ${D}${includedir}/KHR/*
}

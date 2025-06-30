SUMMARY = "Organize packages to avoid duplication across all images"

inherit packagegroup

PACKAGES = "${PN}"

RDEPENDS:${PN} = " \
    qtwayland \
    qtbase \
    qtmultimedia \
    qtmultimedia-plugins \
    qt3d \
    qtcharts \
    qtconnectivity \
    qtdatavis3d \
    qtdeclarative \
    qtgraphicaleffects \
    qtimageformats \
    qtquickcontrols \
    qtquickcontrols2 \
    qtremoteobjects \
    qtsensors \
    qtserialport \
    qtsvg \
    qttools \
    qttranslations \
    qtvirtualkeyboard \
    qtdeclarative-qmlplugins \
    qtquickcontrols-qmlplugins \
    qtfonts \
"

SUMMARY = "Organize packages to avoid duplication across all images"

inherit packagegroup

PACKAGES = "${PN}"

RDEPENDS:${PN} = " \
    qltest-openglwindow \
"

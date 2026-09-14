# TensorFlow Lite bbappend to use direct GitHub URL for OouraFFT and neon2sse

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI:append = " \
    file://tensorflow-lite/0001-Use-direct-GitHub-url-for-OouraFFT.patch \
    file://tensorflow-lite/0002-Use-direct-GitHub-url-for-neon2sse.patch \
    "

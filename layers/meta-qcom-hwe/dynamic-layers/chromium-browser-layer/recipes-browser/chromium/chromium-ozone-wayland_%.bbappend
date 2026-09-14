FILESEXTRAPATHS:prepend := "${THISDIR}/${BPN}:"
# Non-specific patches.
SRC_URI:append:qcom = "\
    file://fix-chromium-launch-crash.patch    \
    file://fix-remove-gpu-dri.patch   \
    file://fix-remove-gpu-sandbox-dri.patch \
"
GN_ARGS:remove = "symbol_level=0"
GN_ARGS:append = "symbol_level=1"

GN_ARGS += "\
        use_dri=false \
        use_gbm=false   \
        use_egl=true    \
        use_gl="egl"    \
        use_system_wayland=true \
        use_system_libwayland=true \
        use_system_libwayland_client=true \
        use_system_libwayland_cursor=true \
        use_system_libwayland_egl=true \
        use_system_libwayland_server=true \
        use_bundled_wayland=false \
        use_system_minigbm=false        \
        use_wayland_gbm=false   \
"

DEPENDS:remove = "virtual/libgl"
DEPENDS:append = " virtual/egl"

PACKAGECONFIG:remove:qcom = "dri"
# Prevent weston from shipping a launcher .desktop + icon into the image.
# Upstream weston_13.0.1.bb installs:
#   ${datadir}/applications/weston.desktop
#   ${datadir}/icons/hicolor/48x48/apps/weston.png
# whenever DISTRO_FEATURES contains "x11". On this product weston runs as
# the system compositor (not a user-launched app), so the .desktop entry just
# shows a spurious "Weston" icon in the app launcher. Strip it at the source
# (do_install) so no image recipe ever has to clean it up afterwards.
do_install:append() {
    rm -f ${D}${datadir}/applications/weston.desktop
    rm -f ${D}${datadir}/icons/hicolor/48x48/apps/weston.png
}

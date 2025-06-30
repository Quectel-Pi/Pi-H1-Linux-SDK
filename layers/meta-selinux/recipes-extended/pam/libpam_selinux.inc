inherit enable-selinux

PACKAGECONFIG[selinux] = "--enable-selinux,--disable-selinux,libselinux"
RDEPENDS:${PN}-runtime += "${@target_selinux(d, 'pam-plugin-selinux')}"

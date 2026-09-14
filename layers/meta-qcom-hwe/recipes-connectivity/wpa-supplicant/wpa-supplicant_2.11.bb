inherit autotools-brokensep linux-kernel-base pkgconfig

FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/${LICENSE};md5=550794465ba0ec5312d6919e203a55f9"

DEPENDS += "glib-2.0 dbus libnl openssl"

SRC_URI = "http://w1.fi/releases/wpa_supplicant-${PV}.tar.gz \
          "

S = "${WORKDIR}/wpa_supplicant-${PV}/wpa_supplicant"

SRC_URI += "file://defconfig.qcom \
            file://0002-UPSTREAM-WNM-Extend-workaround-for-broken-AP-operating-class-behavior.patch;patchdir=${WORKDIR}/wpa_supplicant-${PV}/ \
"

SRC_URI[sha256sum] = "912ea06f74e30a8e36fbb68064d6cdff218d8d591db0fc5d75dee6c81ac7fc0a"

LT_MAJOR = "1"
LT_MINOR = "0"
LT_PATCH = "0"

PACKAGES:prepend = "wpa-supplicant-passphrase wpa-supplicant-cli "
FILES:wpa-supplicant-passphrase = "${bindir}/wpa_passphrase"
FILES:wpa-supplicant-cli = "${bindir}/wpa_cli"
FILES:${PN} += "${datadir}/dbus-1/system-services/* ${systemd_system_unitdir}/*"

do_configure() {
        install -m 0644 ${WORKDIR}/defconfig.qcom .config

        if echo "${PACKAGECONFIG}" | grep -qw "openssl"; then
                ssl=openssl
        elif echo "${PACKAGECONFIG}" | grep -qw "gnutls"; then
                ssl=gnutls
        fi
        if [ -n "$ssl" ]; then
                sed -i "s/%ssl%/$ssl/" .config
        fi

        # For rebuild
        rm -f *.d dbus/*.d

        sed -i 's/libwpa_client.so/libwpa_client.so.${LT_MAJOR}.${LT_MINOR}.${LT_PATCH}/g' Makefile
        sed -i '/\($(Q)$(CC) $(LDFLAGS) -o $@ $(CFLAGS) -shared -fPIC $^\)/  s/\(-shared\)/\1 -Wl,-soname,libwpa_client.so.${LT_MAJOR}/' Makefile
        sed -i '/install -m 0644 -D ..\/src\/common\/wpa_ctrl.h $(DESTDIR)\/$(INCDIR)\/wpa_ctrl.h/ i\
	ln -sf libwpa_client.so.${LT_MAJOR}.${LT_MINOR}.${LT_PATCH} $(DESTDIR)/$(LIBDIR)/libwpa_client.so.${LT_MAJOR}\
	ln -sf libwpa_client.so.${LT_MAJOR}.${LT_MINOR}.${LT_PATCH} $(DESTDIR)/$(LIBDIR)/libwpa_client.so' Makefile
        sed -i '/rm -f libwpa_test1 libwpa_test2/ i\
	rm -f libwpa_client.so.${LT_MAJOR}\
	rm -f libwpa_client.so' Makefile
}

export EXTRA_CFLAGS = "${CFLAGS}"
export BINDIR = "${sbindir}"

do_install() {
        make install DESTDIR=${D} BINDIR=${sbindir} LIBDIR=${libdir} INCDIR=${includedir}

        install -d ${D}${sysconfdir}/network/if-pre-up.d/
        install -d ${D}${sysconfdir}/network/if-post-down.d/
        install -d ${D}${sysconfdir}/network/if-down.d/
        cd ${D}${sysconfdir}/network/ && \
        ln -sf ../if-pre-up.d/wpa-supplicant if-post-down.d/wpa-supplicant

        install -d ${D}/${sysconfdir}/dbus-1/system.d
        install -m 644 ${S}/dbus/dbus-wpa_supplicant.conf ${D}/${sysconfdir}/dbus-1/system.d
        install -d ${D}/${datadir}/dbus-1/system-services
        install -m 644 ${S}/dbus/*.service ${D}/${datadir}/dbus-1/system-services

        if ${@bb.utils.contains('DISTRO_FEATURES','systemd','true','false',d)}; then
                install -d ${D}/${systemd_system_unitdir}
                install -m 644 ${S}/systemd/*.service ${D}/${systemd_system_unitdir}
        fi
}

pkg_postinst:${PN} () {
     # If we're offline, we don't need to do this.
     if [ "x$D" = "x" ]; then
             killall -q -HUP dbus-daemon || true
        fi

}

SOLIBS='*.so*'
FILES_SOLIBSDEV=""
INSANE_SKIP:${PN} = "dev-so"

FILES:${PN} += "/usr/include/*"

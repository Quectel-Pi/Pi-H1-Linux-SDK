# Register Yocto-built .deb files into the dpkg database of a Debian/Ubuntu
# rootfs that was produced by rsync'ing a prebuilt distro tarball over the
# Yocto rootfs.
#
# Why this exists
# ---------------
# quecpi-image.bb builds its rootfs with Yocto (dpkg/apt fills
# /var/lib/dpkg), then deploy_debian_gnome_rootfs() rsyncs a prebuilt
# Debian/Ubuntu tarball on top of it.  That tarball ships its own
# /var/lib/dpkg, so everything the Yocto packages installed - all of
# /usr/lib/modules, the QCOM firmware, the BSP userspace - is still on disk but
# has lost its database entry: `dpkg -S` no longer knows the owner and apt can
# never upgrade those files.
#
# This class installs the Yocto-built debs AGAIN, after the rsync, into the
# target rootfs' dpkg database.  The payload is already in place, so the pass is
# pure bookkeeping - but it makes those files package-managed and therefore
# upgradeable from a Quectel apt repository later on.
#
# Scope: Debian/Ubuntu images only
# ---------------------------------
# Nothing here applies to a plain Yocto image (buildconfig LINUX).  That build
# sets SKIP_DEPLOY_DEBIAN_GNOME_ROOTFS=1, so deploy_debian_gnome_rootfs is not
# in IMAGE_PREPROCESS_COMMAND and no distro tarball is rsync'd over the rootfs.
# do_rootfs' database is then already correct - every file has an owner - so the
# class detects the missing overlay and skips itself entirely
# (DEB_TRACK_REQUIRE_OVERLAY, default 1).  Without that guard it would reinstall
# identical versions, park statoverride and run with --force-depends on an image
# that needs none of it, and it would leave a PUBLISH manifest that makes
# buildpackage publish an apt repo for a Yocto image.
#
# Usage
# -----
#   inherit deb-track
#   DEB_TRACK_PACKAGES = "file://conf/deb-track/kernel.txt"
#
# Each list is plain text, one package name per line; '#' comments and blank
# lines are ignored and globs are allowed (kernel-module-*).  To track more
# packages, add lines to a list - no class edit needed.
#
# How it works
# ------------
# * Runs in IMAGE_PREPROCESS_COMMAND immediately after
#   deploy_debian_gnome_rootfs, i.e. after the rsync overlay and before the
#   rootfs is packed into system.img.  It cannot run in
#   ROOTFS_POSTPROCESS_COMMAND: that fires during do_rootfs, before the overlay,
#   and the rsync would then wipe the database again.
# * Uses dpkg-native and the kernel recipe's crossscripts dir from the recipe
#   sysroots - the same tools do_rootfs uses.  poky patches dpkg-native so
#   --force-script-chrootless exports D=<rootfs> to maintainer scripts, meaning
#   each script takes exactly the branch it takes during a normal Yocto build:
#   the "else" (build-host) branch, with depmodwrapper/update-rc.d/systemctl
#   available on PATH.
# * Deliberately NOT run under pseudo, and not inside the fakeroot the image
#   task provides for the rsync: dpkg-deb extracts a deb's control member into
#   <admindir>/tmp and pipes it through the system tar, whose chown() is not
#   intercepted, so unpacking aborts with "Cannot change ownership to uid 0".
#   Without pseudo, --force-not-root + --force-script-chrootless installs the
#   whole set correctly.  Ownership of the payload is irrelevant here: the files
#   are already on disk from the rsync, and image.bbclass chowns the rootfs to
#   root:root afterwards.
# * /var/lib/dpkg/statoverride names Debian-only accounts (_chrony, geoclue,
#   ssl-cert, messagebus) that the build host's NSS cannot resolve, which makes
#   dpkg abort.  It is parked for the duration of the run and restored after.
# * Maintainer scripts are kept as-is, because they generate files no package
#   ships (/usr/lib/modules/<ver>/modules.dep comes from depmodwrapper).  They
#   are also what the device will run on a later `apt upgrade`, so shipping
#   modified copies would create a divergence.
# * Only one thing is rewritten in the private copy of each deb, never in
#   deploy/deb: a Version that does not start with a digit (`git-r0`), which
#   dpkg installs but apt and dpkg-query then refuse to parse, so such a package
#   could never be upgraded.
# * Dependency closure is resolved from deploy/deb only, so listing
#   kernel-module-* also pulls in the virtual kernel-<ver> meta package (owner
#   of modules.builtin/modules.order) and kernel-image-* (owner of
#   /boot/Image-<ver>).  Dependencies that are not Yocto-built - Debian runtime
#   libs, or Yocto-only names such as libgcc1 with no Debian counterpart - are
#   left unsatisfied on purpose and dpkg is told --force-depends; the real
#   libraries are already on the rootfs.

DEB_TRACK_PACKAGES ??= ""
DEB_TRACK_DEPLOY_DIR ??= "${DEPLOY_DIR_DEB}"
# 0 disables the whole pass (plain Yocto image, or A/B-ing a build).
DEB_TRACK_ENABLED ??= "1"
# 1: a failure fails the build.  0: warn and leave the rootfs unmanaged.
DEB_TRACK_FATAL ??= "1"

# Where to leave the apt-repository payload for later publishing.
#   ${TMPDIR}/deb-track/repo   flattened .debs + Packages/Release
#   ${TMPDIR}/deb-track/PUBLISH  one line per published deb + its origin
# buildpackage copies this into quectel_build/deb/, which is what an HTTP
# server (or a USB stick) would serve for `apt upgrade`.
DEB_TRACK_PUBLISH ??= "1"

# Only act on images that actually rsync a distro tarball over the Yocto rootfs.
#
# This whole class exists because deploy_debian_gnome_rootfs replaces
# /var/lib/dpkg and orphans the files Yocto installed.  A plain Yocto image
# (buildconfig LINUX sets SKIP_DEPLOY_DEBIAN_GNOME_ROOTFS=1) has no such
# overlay: do_rootfs wrote a correct database and every file already has an
# owner, so re-registering would only reinstall identical versions while
# parking statoverride and running with --force-depends on an image that needs
# none of it.  With this at "1" (default) the class stays completely out of the
# way when deploy_debian_gnome_rootfs is absent.
#
# Set it to "0" only if you deliberately want the registration on a plain Yocto
# rootfs (for example to build an apt repo from it).
DEB_TRACK_REQUIRE_OVERLAY ??= "1"

# Package lists whose names belong to the Debian/Ubuntu base system.  When
# resolving a dependency closure reaches a package whose name appears in one of
# these files, the package is skipped and the distro's own copy is kept.
#
# Only packages pulled in as dependencies are affected; anything named directly
# in DEB_TRACK_PACKAGES is always installed.  See conf/deb-track/distro-owned.txt
# for the full rationale and how to refresh the list.
#
# Set DEB_TRACK_DISTRO_OWNED_LISTS to "" to disable the rule and fall back to
# closing over everything the dependency graph reaches, minus
# DEB_TRACK_CLOSURE_DENY.
DEB_TRACK_DISTRO_OWNED_LISTS ??= "file://conf/deb-track/distro-owned.txt"

# Every file path the Debian/Ubuntu base already owns, gzipped, one path per
# line.  This is the authoritative criterion for the "keep the distro's copy"
# rule: dpkg refuses an unpack that would overwrite a path another package
# owns, and reports it as `trying to overwrite '/x', which is also in package
# y`.  The package-name list above cannot catch those cases because Yocto and
# the distro name the same library differently:
#
#   Yocto              Debian 13                  overlap
#   bluez5             bluez                      /etc/bluetooth/input.conf
#   bluez5-obex        bluez-obexd                /usr/lib/systemd/user/obex.service
#   libgtk-3.0         libgtk-3-common            /etc/gtk-3.0/im-multipress.conf
#   libgsttag-1.0-0    libgstreamer-plugins-base1.0-0  /usr/share/gst-plugins-base/...
#   python3-core       python3                    /usr/lib/python3.12/...
#   libglib-2.0-utils  libglib2.0-bin             /usr/bin/gio-querymodules
#
# Generated read-only from the prebuilt distro tarballs (Debian 13 union
# Ubuntu 26, 179103 paths).  Refresh with:
#   for t in prebuild/*-rootfs.tar.xz; do
#       tar -xJOf "$t" --wildcards './var/lib/dpkg/info/*.list' \
#           | grep '^/' ; done | sort -u | gzip -9 > distro-owned-paths.txt.gz
#
# Set to "" to disable path-based skipping.
DEB_TRACK_DISTRO_PATHS ??= "file://conf/deb-track/distro-owned-paths.txt.gz"

# Glob patterns for packages the dependency closure must NEVER pull in.
#
# This is a hard safety boundary, not a preference.  A firmware package in
# deploy/deb declares plain Yocto dependencies such as `libc6`, `libstdc++6`,
# `shadow`, `base-files`; resolving them would reinstall Yocto's versions of the
# base system over the Debian one.  Measured damage on QCS6490/Debian 13: the
# Yocto libc6 owns /usr/lib/ld-linux-aarch64.so.1 as a regular file, while
# Debian owns that path as a symlink into /usr/lib/aarch64-linux-gnu/ - letting
# the closure install it would replace the dynamic loader and can make the
# image unbootable.
#
# Anything matching these stays out of the closure.  Its dependency simply stays
# unsatisfied, which dpkg tolerates under --force-depends; the real library is
# already on the rootfs from the distro tarball.
DEB_TRACK_CLOSURE_DENY ??= " \
    libc6 libc-utils libgcc1 libgcc-s1 libstdc++6 \
    base-files base-passwd base-passwd-* \
    shadow shadow-* \
    pam-plugin-* libpam libpam-runtime \
    update-alternatives-opkg \
    useradd-qcom \
    util-linux-sulogin \
    libz1 libattr1 libblkid1 libcrypt2 libdrm2 libexpat1 libffi8 libatomic1 \
    libglib-2.0-0 libmount1 libpcre2 libxml2 \
    libx11-xcb1 libxau6 libxcb* libxdmcp6 libxshmfence1 \
    libgbm1 wayland \
    libva \
    "

# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

def _deb_track_control(deb, nativebindir, fields):
    """Read selected control fields from a deb.

    Parsed from `dpkg-deb -I` output rather than `dpkg-deb --field`: several
    Yocto packages carry `Version: git-r0`, which dpkg-deb's strict control
    parser rejects ("version number does not start with digit") even though dpkg
    itself installs them fine.
    """
    import subprocess
    out = subprocess.run([nativebindir + '/dpkg-deb', '-I', deb],
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    res = {}
    for line in out.stdout.decode('utf-8', 'replace').splitlines():
        if ':' not in line:
            continue
        k, v = line.split(':', 1)
        if k.strip() in fields:
            res[k.strip()] = v.strip()
    return res


def _deb_track_closure(initial, search_archs, deploy, nativebindir, deny=None, distro_owned=None, distro_paths=None):
    """Expand `initial` debs with the deploy/deb packages they depend on.

    `deny` is a list of fnmatch patterns that must never enter the closure; see
    DEB_TRACK_CLOSURE_DENY for why that boundary exists.

    `distro_owned` is the set of package names the Debian/Ubuntu base already
    provides and `distro_paths` the set of paths it already owns (see
    DEB_TRACK_DISTRO_OWNED_LISTS / DEB_TRACK_DISTRO_PATHS).  A package reached
    only as a dependency is skipped when either matches, so the distro's copy is
    the one that stays on the rootfs.  The path test is the one that matters:
    dpkg refuses to unpack over a path another package owns, and Yocto and the
    distro name the same library differently, so the name list alone misses
    bluez5/bluez, libgtk-3.0/libgtk-3-common, libgsttag-1.0-0/... and friends.

    Packages named in `initial` are never skipped.
    """
    import fnmatch, glob, os

    deny = deny or []
    distro_owned = distro_owned or set()
    distro_paths = distro_paths or set()
    initial_set = set(initial)
    skipped_for_distro = []

    def denied(name):
        return any(fnmatch.fnmatchcase(name, pat) for pat in deny)

    _contents_cache = {}

    def paths_of(deb):
        if deb not in _contents_cache:
            import subprocess
            out = subprocess.run([os.path.join(nativebindir, 'dpkg-deb'),
                                  '-c', deb],
                                 stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            got = set()
            for line in out.stdout.decode('utf-8', 'replace').splitlines():
                if line.startswith('d'):
                    continue
                parts = line.split()
                if len(parts) < 6:
                    continue
                p = parts[5]
                if p.startswith('./'):
                    p = '/' + p[2:]
                if p and not p.endswith('/'):
                    got.add(p)
            _contents_cache[deb] = got
        return _contents_cache[deb]

    by_name = {}
    by_provide = {}
    variants = ('-dbg_', '-dev_', '-staticdev_', '-src_', '-doc_')
    for arch in search_archs:
        for deb in glob.glob(os.path.join(deploy, arch, '*.deb')):
            f = _deb_track_control(deb, nativebindir, ['Package', 'Provides'])
            pkg = f.get('Package')
            if not pkg:
                continue
            if denied(pkg):
                continue
            if any(os.path.basename(deb).startswith(pkg + v) for v in variants):
                continue
            by_name.setdefault(pkg, deb)
            for prov in (f.get('Provides') or '').split(','):
                prov = prov.strip().split(' ')[0]
                if prov:
                    by_provide.setdefault(prov, deb)

    chosen = {}
    queue = list(initial)
    seen = set()
    while queue:
        deb = queue.pop(0)
        if deb in seen:
            continue
        seen.add(deb)
        f = _deb_track_control(deb, nativebindir,
                               ['Package', 'Depends', 'Pre-Depends'])
        pkg = f.get('Package')
        if pkg:
            chosen[pkg] = deb
        for field in ('Pre-Depends', 'Depends'):
            for dep in (f.get(field) or '').split(','):
                for option in dep.strip().split('|'):
                    name = option.strip().split(' ')[0].split(':')[0]
                    if denied(name):
                        continue
                    if name in by_name:
                        cand = by_name[name]
                    elif name in by_provide:
                        cand = by_provide[name]
                    else:
                        continue
                    # The distro already ships this: keep the distro's copy.
                    if cand not in initial_set:
                        why = None
                        if name in distro_owned:
                            why = 'name'
                        elif distro_paths and (paths_of(cand) & distro_paths):
                            why = 'path'
                        if why:
                            skipped_for_distro.append(
                                (name, os.path.basename(cand), why))
                            continue
                    queue.append(cand)
                    break

    # Reported by the caller: this function must stay importable outside
    # bitbake (deb-publish.sh --from-lists execs it from plain python, where
    # there is no `bb` module).
    _deb_track_closure.last_skipped_for_distro = skipped_for_distro
    return sorted(chosen.values())


# ---------------------------------------------------------------------------
# task bodies
# ---------------------------------------------------------------------------

python deb_track_prepare() {
    if d.getVar('DEB_TRACK_ENABLED') != '1':
        bb.note('deb-track: disabled')
        return
    # Set to 0 by the ordering hook when the image has no
    # deploy_debian_gnome_rootfs (plain Yocto build).  Checked here too so a
    # manual `bitbake -c deb_track_prepare` on such an image is also a no-op
    # instead of reinstalling packages that do_rootfs already owns.
    if d.getVar('DEB_TRACK_ACTIVE') == '0':
        bb.note('deb-track: inactive for this image, skipped')
        d.setVar('DEB_TRACK_RESOLVED', '')
        return

    import os, glob, subprocess

    rootfs = d.getVar('IMAGE_ROOTFS')
    deploy = d.getVar('DEB_TRACK_DEPLOY_DIR')
    nativebindir = os.path.join(d.getVar('RECIPE_SYSROOT_NATIVE'), 'usr', 'bin')

    lists = (d.getVar('DEB_TRACK_PACKAGES') or '').split()
    if not lists:
        bb.note('deb-track: DEB_TRACK_PACKAGES is empty, nothing to register')
        d.setVar('DEB_TRACK_RESOLVED', '')
        return
    if not os.path.isdir(rootfs):
        bb.fatal('deb-track: IMAGE_ROOTFS %s does not exist' % rootfs)
    if not os.path.exists(os.path.join(nativebindir, 'dpkg')):
        bb.fatal('deb-track: dpkg-native missing under %s' % nativebindir)
    if not os.path.isdir(deploy):
        bb.fatal('deb-track: DEPLOY_DIR_DEB %s does not exist' % deploy)

    # The distro tarball must already be merged in; otherwise we would register
    # into a dpkg database that the rsync is about to replace.
    if not os.path.exists(os.path.join(rootfs, 'var', 'lib', 'dpkg', 'status')):
        bb.fatal('deb-track: %s/var/lib/dpkg/status missing - run '
                 'deploy_debian_gnome_rootfs first' % rootfs)

    search_archs = ['all']
    for a in (d.getVar('PACKAGE_ARCHS') or '').split() + [d.getVar('MACHINE_ARCH') or '']:
        if a and a not in search_archs:
            search_archs.append(a)

    wanted = []
    for entry in lists:
        path = entry[len('file://'):] if entry.startswith('file://') else entry
        if not os.path.isabs(path):
            for base in (d.getVar('META_QUECTEL_DIR') or '',
                         d.getVar('FILE_DIRNAME') or ''):
                cand = os.path.join(base, path)
                if os.path.exists(cand):
                    path = cand
                    break
        if not os.path.exists(path):
            bb.fatal('deb-track: package list not found: %s (from %s)'
                     % (path, entry))
        with open(path) as fh:
            for line in fh:
                line = line.split('#', 1)[0].strip()
                if line:
                    wanted.append((line, path))
        bb.note('deb-track: read %s' % path)

    # Resolve names to debs.  A glob entry expands to every matching package; a
    # plain entry matches only `<name>_<version>_<arch>.deb`, so that
    # `kernel-image` cannot accidentally select kernel-image-vmlinux.
    variants = ('-dbg_', '-dev_', '-staticdev_', '-src_', '-doc_')
    resolved = []
    missing = []
    for name, src in wanted:
        hits = []
        for arch in search_archs:
            pats = [name + '_*.deb']
            if glob.has_magic(name):
                pats.append(name + '-*_*.deb')
            for pat in pats:
                hits.extend(glob.glob(os.path.join(deploy, arch, pat)))
        # The two glob patterns above overlap (`kernel-module-*_*` already
        # matches `kernel-module-foo-6.6_...`), so dedupe.
        hits = sorted(set(hits))
        hits = [h for h in hits
                if not any(os.path.basename(h).startswith(name + v) for v in variants)]
        if not hits:
            missing.append('%s (from %s)' % (name, os.path.basename(src)))
            continue
        if glob.has_magic(name):
            # Glob: every match is wanted (kernel-module-* => all modules).
            hits.sort()
            resolved.extend(hits)
            bb.note('deb-track: %-52s -> %d package(s)' % (name, len(hits)))
        else:
            hits.sort()
            resolved.append(hits[-1])
            bb.note('deb-track: %-52s -> %s' % (name, os.path.basename(hits[-1])))

    if missing:
        msg = ('deb-track: no .deb in %s for: %s'
               % (deploy, ', '.join(missing)))
        if d.getVar('DEB_TRACK_FATAL') == '1':
            bb.fatal(msg)
        bb.warn(msg)

    # Package names the Debian/Ubuntu base already ships.  The closure must not
    # install a second copy of a library the distro owns; see the comment on
    # DEB_TRACK_DISTRO_OWNED_LISTS and conf/deb-track/distro-owned.txt .
    distro_owned = set()
    for entry in (d.getVar('DEB_TRACK_DISTRO_OWNED_LISTS') or '').split():
        path = entry[len('file://'):] if entry.startswith('file://') else entry
        if not os.path.isabs(path):
            for base in (d.getVar('META_QUECTEL_DIR') or '',
                         d.getVar('FILE_DIRNAME') or ''):
                cand = os.path.join(base, path)
                if os.path.exists(cand):
                    path = cand
                    break
        if not os.path.exists(path):
            bb.fatal('deb-track: distro-owned list not found: %s (from %s)'
                     % (path, entry))
        n = 0
        with open(path) as fh:
            for line in fh:
                line = line.split('#', 1)[0].strip()
                if line:
                    distro_owned.add(line.split()[0])
                    n += 1
        bb.note('deb-track: read %s (%d distro-owned name(s))' % (path, n))

    # Paths the distro base owns.  gzip because the list is ~180k lines.
    distro_paths = set()
    for entry in (d.getVar('DEB_TRACK_DISTRO_PATHS') or '').split():
        path = entry[len('file://'):] if entry.startswith('file://') else entry
        if not os.path.isabs(path):
            for base in (d.getVar('META_QUECTEL_DIR') or '',
                         d.getVar('FILE_DIRNAME') or ''):
                cand = os.path.join(base, path)
                if os.path.exists(cand):
                    path = cand
                    break
        if not os.path.exists(path):
            bb.fatal('deb-track: distro path list not found: %s (from %s)'
                     % (path, entry))
        import gzip
        opener = gzip.open if path.endswith('.gz') else open
        n = 0
        with opener(path, 'rt', errors='replace') as fh:
            for line in fh:
                line = line.strip()
                if line.startswith('/'):
                    distro_paths.add(line)
                    n += 1
        bb.note('deb-track: read %s (%d distro path(s))' % (path, n))

    resolved = _deb_track_closure(
        resolved, search_archs, deploy, nativebindir,
        deny=(d.getVar('DEB_TRACK_CLOSURE_DENY') or '').split(),
        distro_owned=distro_owned, distro_paths=distro_paths)
    skipped = getattr(_deb_track_closure, 'last_skipped_for_distro', None) or []
    if skipped:
        by_why = {}
        for name, deb, why in skipped:
            by_why.setdefault(why, []).append(name)
        for why in sorted(by_why):
            names = sorted(set(by_why[why]))
            bb.note('deb-track: kept the distro copy of %d package(s) whose %s '
                    'matched the distro' % (len(names),
                                            'name' if why == 'name' else 'paths'))
            for name in names[:12]:
                bb.note('deb-track:   %-36s' % name)
            if len(names) > 12:
                bb.note('deb-track:   ... and %d more' % (len(names) - 12))
    d.setVar('DEB_TRACK_RESOLVED', ' '.join(resolved))
    bb.note('deb-track: %d deb(s) selected (%d listed, closure expanded)'
            % (len(resolved), len(wanted)))
}

python deb_track_install() {
    if d.getVar('DEB_TRACK_ENABLED') != '1':
        return
    if d.getVar('DEB_TRACK_ACTIVE') == '0':
        return

    import os, glob, shutil, subprocess

    resolved = (d.getVar('DEB_TRACK_RESOLVED') or '').split()
    if not resolved:
        return

    rootfs = d.getVar('IMAGE_ROOTFS')
    nativesysroot = d.getVar('RECIPE_SYSROOT_NATIVE')
    nativebindir = os.path.join(nativesysroot, 'usr', 'bin')
    dpkg = os.path.join(nativebindir, 'dpkg')
    dpkg_deb = os.path.join(nativebindir, 'dpkg-deb')

    workdir = os.path.join(d.getVar('WORKDIR'), 'deb-track')
    repackdir = os.path.join(workdir, 'debs')
    shutil.rmtree(workdir, ignore_errors=True)
    os.makedirs(repackdir)

    # Which package names will exist on the image when this is done: what the
    # distro already has plus what we are about to install.  Anything a Depends
    # points at that is not in here would make `apt-get check` fail with
    # "but it is not installable", and one failing package blocks every
    # `apt upgrade` on the system.  dpkg tolerates those under --force-depends,
    # apt does not.
    #
    # The dangling names are almost always the distro's own functionality under
    # Yocto's name: gstreamer1.0 -> libgstreamer1.0-0, wayland ->
    # libwayland-server0, libva -> libva2, python3-core -> python3,
    # shadow -> passwd, bluez5 -> bluez, libgtk-3.0 -> libgtk-3-0t64.  The
    # library is there and loaded, only the dependency label is unusable, so
    # dropping the clause is safe and is what makes apt work again.
    will_exist = set()
    status_path = os.path.join(rootfs, 'var', 'lib', 'dpkg', 'status')
    if os.path.exists(status_path):
        with open(status_path, errors='replace') as fh:
            for status_line in fh:
                if status_line.startswith('Package: '):
                    will_exist.add(status_line[9:].strip().split(':')[0])
    for deb in resolved:
        f = _deb_track_control(deb, nativebindir, ['Package'])
        if f.get('Package'):
            will_exist.add(f['Package'])

    # Private copies: normalise versions that dpkg cannot parse.  Maintainer
    # scripts and file payloads stay untouched so the image matches what a
    # repository would later deliver.
    #
    # Two places need it, and forgetting the second one is a trap:
    #   Version: git-r0                      -> 0.git-r0
    #   Depends: qcom-args (>= git)          -> (>= 0.git)
    # Fixing only Version leaves the relationship broken: dpkg-query warns
    # "reference to 'qcom-args': version 'git': version number does not start
    # with digit" on every dpkg -l, and apt cannot compare that constraint when
    # resolving an upgrade.  Hit for real on qcom-agm / qcom-pal /
    # qcom-pw-pal-plugin / qcom-vui-interface.
    import re as _re
    REL_FIELDS = ('Depends', 'Pre-Depends', 'Recommends', 'Suggests',
                  'Enhances', 'Breaks', 'Conflicts', 'Replaces', 'Provides')

    def normalise_version(text):
        """Prefix a non-numeric version with 0. so dpkg can parse it."""
        return '0.' + text if text[:1] and not text[0].isdigit() else text

    def fix_relations(line):
        """Rewrite `(<op> <non-numeric-version>)` inside a relation field."""
        return _re.sub(r'\(\s*([<>=]+)\s*([^)]+?)\s*\)',
                       lambda m: '(%s %s)' % (m.group(1),
                                              normalise_version(m.group(2))),
                       line)

    def prune_relations(line, deb_name):
        """Make a relation field something apt can actually satisfy.

        Two problems appear once the distro is older than the Yocto tree:

          a name that does not exist at all  (gstreamer1.0, python3-core,
             libva, shadow, bluez5)  -> Ubuntu/Debian ship the same library
             under a different package name.
          a name that exists but too old    (libnl-3-200 >= 3.9.0 while Debian
             has 3.7.0-2, libxml2 >= 2.12.10 while it has 2.12.7, libpciaccess0
             >= 0.18 while it has 0.17)  -> Yocto simply moved ahead.

        dpkg --force-depends ignores both, apt does not: either one makes
        `apt-get check` fail and takes every `apt upgrade` down with it.

        So: a clause whose package is missing is dropped; a clause whose package
        is present keeps the dependency but loses the version constraint.  The
        constraint is what cannot be met, not the library - the SONAME is the
        same one the binary linked against (libnl-3.so.200, libxml2.so.2,
        libpciaccess.so.0 are all present), so loading still works.

        `a | b` keeps only the alternatives that exist.  Returns None when
        nothing is left, so the caller can drop the whole field.
        """
        field, rest = line.split(':', 1)
        kept = []
        for clause in rest.split(','):
            clause = clause.strip()
            if not clause:
                continue
            options = [o.strip() for o in clause.split('|')]
            names = [o.split(' ')[0].split(':')[0] for o in options]
            present = [o for o, n in zip(options, names) if n in will_exist]
            if present:
                plain = [o.split('(')[0].strip() for o in present]
                if len(plain) != len(present) or len(plain) == 1:
                    bb.note('deb-track: %s: %s %r -> %r'
                            % (deb_name, field, clause, ' | '.join(plain)))
                kept.append(' | '.join(plain))
            else:
                bb.note('deb-track: %s: dropping dangling %s clause %r'
                        % (deb_name, field, clause))
        if not kept:
            return None
        return field + ': ' + ', '.join(kept)

    staged = []
    for deb in resolved:
        tmp = os.path.join(workdir, 'tmpdir')
        shutil.rmtree(tmp, ignore_errors=True)
        subprocess.check_call([dpkg_deb, '-R', deb, tmp], stdout=subprocess.DEVNULL)

        control = os.path.join(tmp, 'DEBIAN', 'control')
        with open(control) as fh:
            lines = fh.read().splitlines()
        out_lines = []
        for line in lines:
            field = line.split(':', 1)[0] if ':' in line else ''
            if field == 'Version':
                ver = line.split(':', 1)[1].strip()
                # A Debian version must start with a digit.  Some Yocto recipes
                # leave it as the bare SRCREV placeholder `git` => `git-r0`:
                # dpkg installs that, but apt/dpkg-query then fail to parse the
                # package, so it could never be upgraded.
                new = normalise_version(ver)
                if new != ver:
                    bb.note('deb-track: %s: Version %r -> %r'
                            % (os.path.basename(deb), ver, new))
                    line = 'Version: ' + new
            elif field in REL_FIELDS:
                fixed = fix_relations(line)
                if field in ('Depends', 'Pre-Depends', 'Recommends', 'Suggests'):
                    pruned = prune_relations(fixed, os.path.basename(deb))
                    if pruned is None:
                        continue
                    fixed = pruned
                if fixed != line:
                    bb.note('deb-track: %s: %s' % (os.path.basename(deb), fixed))
                    line = fixed
            out_lines.append(line)
        with open(control, 'w') as fh:
            fh.write('\n'.join(out_lines) + '\n')

        dst = os.path.join(repackdir, os.path.basename(deb))
        subprocess.check_call([dpkg_deb, '--root-owner-group', '-b', tmp, dst],
                              stdout=subprocess.DEVNULL)
        staged.append(dst)
    shutil.rmtree(os.path.join(workdir, 'tmpdir'), ignore_errors=True)

    admindir = os.path.join(rootfs, 'var', 'lib', 'dpkg')

    env = dict(os.environ)
    env.pop('LD_PRELOAD', None)
    env.pop('D', None)

    # Maintainer scripts must find the build-host helper shims exactly like they
    # do in do_rootfs (depmodwrapper comes from the kernel recipe's
    # crossscripts, update-rc.d/systemctl from the native sysroot).
    extra_path = []
    for cand in glob.glob(os.path.join(d.getVar('TMPDIR'), 'work', '*', '*',
                                       '*', 'recipe-sysroot', 'usr', 'bin',
                                       'crossscripts')):
        extra_path.append(cand)
    for cand in glob.glob(os.path.join(d.getVar('TMPDIR'), 'work', '*',
                                       'linux-*', '*', 'recipe-sysroot', 'usr',
                                       'bin', 'crossscripts')):
        if cand not in extra_path:
            extra_path.append(cand)
    env['PATH'] = ':'.join(extra_path + [nativebindir, env.get('PATH', '')])
    env['NATIVE_ROOT'] = nativesysroot
    env['OFFLINE_ROOT'] = rootfs

    # Do NOT run under pseudo.  Measured behaviour on this build host:
    # pseudo makes dpkg fail while unpacking the control member, because
    # dpkg-deb extracts it into <admindir>/tmp and then pipes it through the
    # system tar; tar's chown() escapes pseudo, so tar aborts with
    # "Cannot change ownership to uid 0".  Without pseudo, --force-not-root
    # plus --force-script-chrootless installs the full set correctly
    # (687 packages, ~27s).  Ownership of the payload does not matter: the
    # files are already on disk from the rsync and image.bbclass chowns the
    # whole rootfs to root:root afterwards.
    #
    # The one thing that does need the target's NSS is statoverride: it names
    # Debian-only accounts (_chrony, geoclue, ssl-cert, messagebus) that the
    # build host cannot resolve, and dpkg aborts with "unknown system group".
    # Move it aside for the run and put it back afterwards.
    statoverride = os.path.join(admindir, 'statoverride')
    statoverride_bak = os.path.join(workdir, 'statoverride.bak')
    statoverride_moved = False
    if os.path.exists(statoverride):
        shutil.copy2(statoverride, statoverride_bak)
        os.remove(statoverride)
        statoverride_moved = True
        bb.note('deb-track: statoverride parked (target-only accounts), '
                'restored after the run')

    base = [dpkg,
            '--force-not-root',
            '--force-architecture',
            '--force-depends',
            '--force-script-chrootless',
            '--root=' + rootfs,
            '--admindir=' + admindir,
            '--log=' + os.path.join(workdir, 'dpkg.log')]

    def run(arguments, what, fatal=True):
        p = subprocess.run(arguments, env=env, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT)
        text = p.stdout.decode('utf-8', 'replace')
        if p.returncode != 0:
            msg = 'deb-track: %s failed (rc=%d):\n%s' % (what, p.returncode, text)
            if fatal and d.getVar('DEB_TRACK_FATAL') == '1':
                bb.fatal(msg)
            bb.warn(msg)
            return False
        for line in text.splitlines():
            if line.strip():
                bb.note('deb-track: %s' % line)
        return True

    # --- Hard guard: drop any package that would touch a distro-owned file ----
    #
    # The closure already skips dependencies that collide, but a package named
    # explicitly in one of the list files is kept even if it collides - that is
    # deliberate, so an operator can see what was asked for.  At install time
    # the answer has to be the opposite: if a file of ours is already on disk
    # and belongs to Debian/Ubuntu, drop the whole package.  Overwriting it
    # would silently break the distro package's bookkeeping (its .list would
    # claim a file that is no longer the one it shipped), and registering the
    # path as ours would make a later `apt upgrade` of the distro package fail
    # with a file conflict.
    #
    # Sources for "belongs to the distro", both read-only:
    #   rootfs var/lib/dpkg/info/*.list, limited to package names that appear
    #       in distro-owned.txt (this excludes the packages deb-track itself
    #       registered on an earlier pass - kernel/firmware/msa-va - which are
    #       on disk but are not distro packages)
    #   distro-owned-paths.txt.gz, the snapshot taken from the distro tarballs
    #
    # Dropping is reported loudly and the package is skipped, never forced.
    import gzip

    distro_names = set()
    for entry in (d.getVar('DEB_TRACK_DISTRO_OWNED_LISTS') or '').split():
        path = entry[len('file://'):] if entry.startswith('file://') else entry
        if not os.path.isabs(path):
            for base_dir in (d.getVar('META_QUECTEL_DIR') or '',
                             d.getVar('FILE_DIRNAME') or ''):
                cand = os.path.join(base_dir, path)
                if os.path.exists(cand):
                    path = cand
                    break
        if not os.path.exists(path):
            continue
        with open(path) as fh:
            for line in fh:
                line = line.split('#', 1)[0].strip()
                if line:
                    distro_names.add(line.split()[0])

    distro_owned_paths = set()
    for entry in (d.getVar('DEB_TRACK_DISTRO_PATHS') or '').split():
        path = entry[len('file://'):] if entry.startswith('file://') else entry
        if not os.path.isabs(path):
            for base_dir in (d.getVar('META_QUECTEL_DIR') or '',
                             d.getVar('FILE_DIRNAME') or ''):
                cand = os.path.join(base_dir, path)
                if os.path.exists(cand):
                    path = cand
                    break
        if not os.path.exists(path):
            continue
        opener = gzip.open if path.endswith('.gz') else open
        with opener(path, 'rt', errors='replace') as fh:
            for line in fh:
                line = line.strip()
                if line.startswith('/'):
                    distro_owned_paths.add(line)

    # Paths actually claimed by distro packages inside this rootfs.  Anything
    # in info/ whose package name is not in distro_names is ours or Yocto's, so
    # it must not be treated as a distro claim.
    infodir = os.path.join(rootfs, 'var', 'lib', 'dpkg', 'info')
    if os.path.isdir(infodir):
        for name in os.listdir(infodir):
            if not name.endswith('.list'):
                continue
            pkg_of_list = name[:-len('.list')].split(':')[0]
            if pkg_of_list not in distro_names:
                continue
            try:
                with open(os.path.join(infodir, name), errors='replace') as fh:
                    for line in fh:
                        line = line.rstrip('\n')
                        if line.startswith('/'):
                            distro_owned_paths.add(line)
            except OSError:
                pass

    if distro_owned_paths:
        dropped = []
        kept = []
        for deb in staged:
            f = _deb_track_control(deb, nativebindir, ['Package'])
            pkg = f.get('Package') or os.path.basename(deb)
            p = subprocess.run([dpkg_deb, '--contents', deb],
                               stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            hits = []
            for line in p.stdout.decode('utf-8', 'replace').splitlines():
                if not line or line.startswith('d'):
                    continue
                parts = line.split()
                if len(parts) < 6:
                    continue
                path = parts[5]
                if path.startswith('./'):
                    path = '/' + path[2:]
                if path.endswith('/'):
                    continue
                if path in distro_owned_paths:
                    hits.append(path)
            if hits:
                dropped.append((pkg, os.path.basename(deb), hits))
            else:
                kept.append(deb)
        if dropped:
            bb.warn('deb-track: dropped %d package(s) whose files the distro '
                    'already owns - those files stay with Debian/Ubuntu:'
                    % len(dropped))
            for pkg, dname, hits in dropped:
                bb.warn('deb-track:   %-34s %-46s %d file(s), e.g. %s'
                        % (pkg, dname, len(hits), hits[0]))
            staged = kept

            # A package we keep may depend on one we just dropped.  The control
            # files were already rewritten by this point (their Depends were
            # pruned against will_exist, which still counted the dropped ones),
            # so that dependency is now dangling and apt would refuse the image.
            # Rewriting again would mean re-unpacking everything, so stop loudly
            # instead: reaching here means the list asks for a package that
            # cannot coexist with the distro, and a human has to decide.
            dropped_names = {pkg for pkg, _, _ in dropped}
            offenders = []
            for deb in staged:
                f = _deb_track_control(deb, nativebindir,
                                       ['Package', 'Depends', 'Pre-Depends'])
                holder = f.get('Package')
                for fld in ('Depends', 'Pre-Depends'):
                    for clause in (f.get(fld) or '').split(','):
                        for option in clause.split('|'):
                            name = option.strip().split(' ')[0].split(':')[0]
                            if name in dropped_names:
                                offenders.append((holder, name))
                                break
            if offenders:
                msg = ('deb-track: %d kept package(s) depend on a package that '
                       'was just dropped for touching distro files:\n  %s\n'
                       'Fix the lists: drop the dependent package too, or remove '
                       'the conflicting file from it.'
                       % (len(offenders),
                          '\n  '.join('%s -> %s' % o for o in offenders[:10])))
                if d.getVar('DEB_TRACK_FATAL') == '1':
                    bb.fatal(msg)
                bb.warn(msg)

    # Pre-flight: two packages owning the same path make dpkg unpack the second
    # one and then roll it back to not-installed, with only a terse error in the
    # log.  Detect it up front while the package list is still small enough to
    # reason about.  (Real case: qcom-video-firmware vs
    # linux-firmware-qcom-venus-5.4 both ship venus.mbn.)
    owners = {}
    clashes = {}
    for deb in staged:
        # NOTE: do not call this `base` - `base` above is the dpkg argv list and
        # shadowing it turned the later `base + [...]` into a str+list
        # TypeError (hit once in a real build).
        pkgname = os.path.basename(deb)
        p = subprocess.run([dpkg_deb, '--contents', deb],
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        for line in p.stdout.decode('utf-8', 'replace').splitlines():
            if not line or line.startswith('d'):
                continue
            parts = line.split()
            if len(parts) < 6:
                continue
            path = parts[5]
            if path.startswith('./'):
                path = '/' + path[2:]
            if path.endswith('/'):
                continue
            if '/' not in path.lstrip('/'):
                continue
            prev = owners.get(path)
            if prev and prev != pkgname:
                clashes.setdefault(path, set()).update((prev, pkgname))
            else:
                owners[path] = pkgname
    if clashes:
        lines = ['  %s\n      %s' % (p, '  vs  '.join(sorted(v)))
                 for p, v in sorted(clashes.items())]
        msg = ('deb-track: %d path(s) shipped by more than one selected '
               'package; dpkg will roll back the later one:\n%s'
               % (len(clashes), '\n'.join(lines[:20])))
        if d.getVar('DEB_TRACK_FATAL') == '1':
            bb.fatal(msg + '\nFix the package list (drop one of each pair).')
        bb.warn(msg)

    try:
        # Never let this run reach a package Debian owns.
        #
        # Two rules make that hold:
        #   --no-triggers   dpkg records pending triggers but does not execute
        #                   them, so distro packages' postinst/triggers are not
        #                   re-run by us.  Without it, unpacking a package that
        #                   ships desktop files or mime data fires
        #                   `Processing triggers for gnome-menus` and
        #                   `desktop-file-utils`, whose scripts fail under
        #                   --force-script-chrootless (gnome-menus-blacklist and
        #                   update-desktop-database are not on PATH) and leave
        #                   those two *distro* packages half-configured.
        #   --configure <names>  not --configure -a: only the packages we just
        #                   unpacked get configured.  -a would also try to
        #                   configure whatever the distro left pending.
        #
        # dpkg may still exit non-zero if a package of ours has a failing
        # postinst; that is tolerated so the per-package check below can report
        # precisely which ones stayed unconfigured.
        if not run(base + ['--unpack', '--no-triggers'] + staged,
                   'dpkg --unpack', fatal=False):
            bb.warn('deb-track: dpkg --unpack returned non-zero; continuing to '
                    'configure. If the failure was a real file conflict the '
                    'per-package check below will report it.')
        our_packages = []
        for deb in staged:
            f = _deb_track_control(deb, nativebindir, ['Package'])
            if f.get('Package'):
                our_packages.append(f['Package'])
        if not run(base + ['--configure', '--no-triggers'] + our_packages,
                   'dpkg --configure', fatal=False):
            bb.warn('deb-track: dpkg --configure returned non-zero; the '
                    'per-package check below names what stayed unconfigured')
    finally:
        if statoverride_moved:
            shutil.copy2(statoverride_bak, statoverride)

    # Sanity check: every package we asked dpkg for must end up 'ii'
    # (installed and configured).  This is what catches a --configure that did
    # not finish: a package left at 'iU' (unpacked) has its files on disk but is
    # treated as uninstalled by apt, so a later `apt upgrade` would fight it.
    # Kernel packages get the same treatment plus a name match, because a broken
    # kernel is a boot failure rather than a cosmetic one.
    q = subprocess.run([dpkg, '--root=' + rootfs, '--admindir=' + admindir,
                        '--force-not-root', '-l'],
                       env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    resolved_names = set()
    for deb in staged:
        f = _deb_track_control(deb, nativebindir, ['Package'])
        if f.get('Package'):
            resolved_names.add(f['Package'])
    broken = []
    unconfigured = []
    for line in q.stdout.decode('utf-8', 'replace').splitlines():
        parts = line.split()
        if len(parts) < 2 or not parts[0][:1].isalpha():
            continue
        name = parts[1].split(':')[0]
        if parts[0] != 'ii':
            if 'kernel' in name:
                broken.append(line.strip())
            elif name in resolved_names:
                unconfigured.append(line.strip())
    if broken:
        msg = ('deb-track: %d kernel package(s) not fully installed:\n  %s'
               % (len(broken), '\n  '.join(broken[:20])))
        if d.getVar('DEB_TRACK_FATAL') == '1':
            bb.fatal(msg)
        bb.warn(msg)
    if unconfigured:
        msg = ('deb-track: %d selected package(s) left unconfigured (files are '
               'on disk but apt treats them as not installed):\n  %s'
               % (len(unconfigured), '\n  '.join(unconfigured[:20])))
        if d.getVar('DEB_TRACK_FATAL') == '1':
            bb.fatal(msg)
        bb.warn(msg)
    if not broken and not unconfigured:
        bb.note('deb-track: %d package(s) registered' % len(staged))

    # Leave a publishing payload behind for buildpackage (see deb_track_publish).
    if d.getVar('DEB_TRACK_PUBLISH') == '1':
        publish = os.path.join(workdir, 'PUBLISH')
        with open(publish, 'w') as fh:
            for deb in staged:
                # staged = rewritten copy, resolved = matching Yocto artifact
                fh.write('%s\t%s\n' % (deb, os.path.basename(deb)))
        bb.note('deb-track: publishing payload listed in %s' % publish)
}

# Ordering: run AFTER deploy_debian_gnome_rootfs.
#
# `:append` is concatenated in parse order, and this class is inherited near the
# top of quecpi-image.bb while the recipe appends the overlay at the bottom - so
# a plain `IMAGE_PREPROCESS_COMMAND:append` here would land BEFORE the overlay and
# the rsync would wipe the dpkg entries we just wrote.
#
# The fix uses an anonymous python function.  bitbake collects anonymous
# functions from every file/append and runs them in ast.finalize() -
# `runAnonFuncs(d)` - which happens after the whole recipe (including all
# appends) is parsed but BEFORE `bb.parse.siggen.finalise()` computes the
# basehashes.  So the value we set here is both correctly ordered and part of the
# hash from the very first calculation.
#
# Do NOT do this from a `do_image[prefuncs]` hook: a prefunc runs at task
# execution time, and IMAGE_PREPROCESS_COMMAND is registered as its own vardep by
# image.bbclass (`d.setVarFlag(var, 'vardeps', d.getVar(var))`).  Mutating it
# there makes the basehash differ between the initial parse and any later
# reparse, which bitbake reports as:
#   "When reparsing ...:do_image, the basehash value changed ... The metadata is
#    not deterministic and this needs to be fixed."
python () {
    if d.getVar('DEB_TRACK_ENABLED') != '1':
        return
    if not (d.getVar('DEB_TRACK_PACKAGES') or '').strip():
        return
    cmds = (d.getVar('IMAGE_PREPROCESS_COMMAND') or '').split()
    if 'deb_track_prepare' in cmds:
        return

    # IMAGE_PREPROCESS_COMMAND entries are shell commands and may carry a
    # trailing ';': quecpi-image.bb appends ' deploy_debian_gnome_rootfs;', so a
    # plain `overlay in cmds` test never matches and everything used to fall
    # through to the tail - which merely looked right because the overlay
    # happened to be the last entry, and silently enabled the class on plain
    # Yocto builds too.  Compare with the ';' stripped.
    overlay = 'deploy_debian_gnome_rootfs'
    overlay_idx = next((i for i, c in enumerate(cmds) if c.rstrip(';') == overlay), None)

    if overlay_idx is None:
        # No overlay in this image.  That is the plain Yocto build: buildconfig
        # LINUX sets SKIP_DEPLOY_DEBIAN_GNOME_ROOTFS=1, so quecpi-image.bb does
        # not append deploy_debian_gnome_rootfs and nothing rsyncs over the
        # rootfs here.
        #
        # In that case do_rootfs's dpkg database is already correct: every file
        # on the rootfs has an owner, so there is nothing to re-register.  All
        # deb-track would do is reinstall the same versions (wasted work) while
        # taking real risks on an image that is otherwise fine: parking
        # statoverride, running with --force-depends, and leaving a PUBLISH
        # manifest that would make buildpackage offer an apt repo a Yocto image
        # does not need.  So stay out of the way by default.
        #
        # Set DEB_TRACK_REQUIRE_OVERLAY=0 to force it on such an image anyway
        # (e.g. to build a repo from a plain Yocto rootfs).
        d.setVar('DEB_TRACK_ACTIVE', '0')
        if d.getVar('DEB_TRACK_REQUIRE_OVERLAY') == '1':
            bb.note('deb-track: this image has no deploy_debian_gnome_rootfs '
                    '(plain Yocto build), nothing to register - skipped')
            return
        cmds += ['deb_track_prepare', 'deb_track_install']
    else:
        cmds[overlay_idx + 1:overlay_idx + 1] = ['deb_track_prepare', 'deb_track_install']
    # setVar (not appendVar) so the placement right after the overlay is kept
    # even though we are not at the end of the list.
    d.setVar('IMAGE_PREPROCESS_COMMAND', ' '.join(cmds))
    # Keep the vardeps flag in sync so a change to the command list still
    # invalidates the task signature (image.bbclass sets this flag from the
    # pre-anonymous value).
    d.setVarFlag('IMAGE_PREPROCESS_COMMAND', 'vardeps', ' '.join(cmds))
}

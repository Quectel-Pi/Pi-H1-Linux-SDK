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


def _deb_track_closure(initial, search_archs, deploy, nativebindir, deny=None):
    """Expand `initial` debs with the deploy/deb packages they depend on.

    `deny` is a list of fnmatch patterns that must never enter the closure; see
    DEB_TRACK_CLOSURE_DENY for why that boundary exists.
    """
    import fnmatch, glob, os

    deny = deny or []

    def denied(name):
        return any(fnmatch.fnmatchcase(name, pat) for pat in deny)

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
                        queue.append(by_name[name])
                        break
                    if name in by_provide:
                        queue.append(by_provide[name])
                        break
    return sorted(chosen.values())


# ---------------------------------------------------------------------------
# task bodies
# ---------------------------------------------------------------------------

python deb_track_prepare() {
    if d.getVar('DEB_TRACK_ENABLED') != '1':
        bb.note('deb-track: disabled')
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

    resolved = _deb_track_closure(resolved, search_archs, deploy, nativebindir,
                                  deny=(d.getVar('DEB_TRACK_CLOSURE_DENY') or '').split())
    d.setVar('DEB_TRACK_RESOLVED', ' '.join(resolved))
    bb.note('deb-track: %d deb(s) selected (%d listed, closure expanded)'
            % (len(resolved), len(wanted)))
}

python deb_track_install() {
    if d.getVar('DEB_TRACK_ENABLED') != '1':
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

    # Private copies: normalise an invalid Version only.  Maintainer scripts and
    # file payloads stay untouched so the image matches what a repository would
    # later deliver.
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
            if line.startswith('Version:'):
                ver = line.split(':', 1)[1].strip()
                # A Debian version must start with a digit.  Some Yocto recipes
                # leave it as the bare SRCREV placeholder `git` => `git-r0`:
                # dpkg installs that, but apt/dpkg-query then fail to parse the
                # package, so it could never be upgraded.
                if not ver[:1].isdigit():
                    new = '0.' + ver
                    bb.note('deb-track: %s: Version %r -> %r'
                            % (os.path.basename(deb), ver, new))
                    line = 'Version: ' + new
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

    def run(arguments, what):
        p = subprocess.run(arguments, env=env, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT)
        text = p.stdout.decode('utf-8', 'replace')
        if p.returncode != 0:
            msg = 'deb-track: %s failed (rc=%d):\n%s' % (what, p.returncode, text)
            if d.getVar('DEB_TRACK_FATAL') == '1':
                bb.fatal(msg)
            bb.warn(msg)
            return False
        for line in text.splitlines():
            if line.strip():
                bb.note('deb-track: %s' % line)
        return True

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
        # Unpack everything first so dpkg sees the whole set, then configure so
        # it can order the packages itself.
        if not run(base + ['--unpack'] + staged, 'dpkg --unpack'):
            return
        run(base + ['--configure', '-a'], 'dpkg --configure -a')
    finally:
        if statoverride_moved:
            shutil.copy2(statoverride_bak, statoverride)

    # Sanity check: every package carrying 'kernel' in its name must end up
    # fully installed, and unmatched /var/lib/dpkg entries would mean a
    # corrupted database.
    q = subprocess.run([dpkg, '--root=' + rootfs, '--admindir=' + admindir,
                        '--force-not-root', '-l'],
                       env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    broken = []
    for line in q.stdout.decode('utf-8', 'replace').splitlines():
        parts = line.split()
        if len(parts) < 2 or not parts[0][:1].isalpha():
            continue
        if parts[0] != 'ii' and 'kernel' in parts[1]:
            broken.append(line.strip())
    if broken:
        msg = ('deb-track: %d package(s) not fully installed:\n  %s'
               % (len(broken), '\n  '.join(broken[:20])))
        if d.getVar('DEB_TRACK_FATAL') == '1':
            bb.fatal(msg)
        bb.warn(msg)
    else:
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
    overlay = 'deploy_debian_gnome_rootfs'
    if overlay in cmds:
        idx = cmds.index(overlay) + 1
        cmds[idx:idx] = ['deb_track_prepare', 'deb_track_install']
    else:
        cmds += ['deb_track_prepare', 'deb_track_install']
    # setVar (not appendVar) so the placement right after the overlay is kept
    # even though we are not at the end of the list.
    d.setVar('IMAGE_PREPROCESS_COMMAND', ' '.join(cmds))
    # Keep the vardeps flag in sync so a change to the command list still
    # invalidates the task signature (image.bbclass sets this flag from the
    # pre-anonymous value).
    d.setVarFlag('IMAGE_PREPROCESS_COMMAND', 'vardeps', ' '.join(cmds))
}

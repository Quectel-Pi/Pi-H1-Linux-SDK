#!/bin/sh
# Refresh the file manager's idea of whether a mount root is writable.
#
# Background (Nautilus 48.3 / GIO directory attribute caching)
#   Nautilus re-reads a directory's "can I write here" answer only when
#     1) that exact directory produces a filesystem event (attribute or
#        content change), or
#     2) the user refreshes (F5 / Ctrl+R).
#   The moment a removable disk is unmounted or yanked, udisks2 removes the
#   mount point directory (/media/<user>/<label>, on this board
#   /media/<desktop user>/SDCARD). If a Nautilus window is showing that
#   directory at that moment (session restore at boot counts), Nautilus
#   caches "not writable" and never re-checks it. Once the card is back and
#   the path exists again -- genuinely writable, uid/gid = desktop user --
#   Nautilus still trusts the stale answer, so Paste / New Folder / Create
#   Link stay greyed out on the mount root. Subdirectories (e.g. 123) are
#   separate directory objects with a correct value, which is why pasting
#   inside a subdirectory works fine. A manual F5 restores it instantly.
#
# Measured on this board (Debian 13 + Nautilus 48.3)
#   * the window is missing view.paste / view.new-folder /
#     view.create-link-shortcut, and all three come back within a second of F5;
#   * with the mount root remounted read-only the window drops to 38 actions
#     with view.new-folder gone, and remounting read-write brings it back to
#     39 -- the greyed state follows the mount root's writability directly;
#   * one touch of the mount root makes Nautilus re-read the attributes, but
#     only while a view is actually showing that directory (Nautilus then has
#     a monitor on it and holds an open fd); touching the parent
#     /media/<user> does nothing;
#   * with no view open the touch does nothing and cannot stop a later window
#     from inheriting the cached value, hence the watch loop below.
#
# What this does: for a while after a filesystem is mounted, watch for a file
# manager process holding that mount point open (proof that a view is showing
# it) and then touch it once, producing the directory event that makes
# Nautilus re-read its attributes. Only the directory mtime changes, no data
# is touched, nothing is written when no view exists, and each new view
# triggers a single write so the card is not worn out.
#
# Invoked by the udev rule 92-quectel-sdcard-refresh.rules through
# quectel-sdcard-refresh.service on filesystem add/change.

WINDOW=${REFRESH_WINDOW:-180}   # how long to watch after mount (seconds)
INTERVAL=2                      # poll interval (seconds)
STATE=/run/quectel-sdcard-refresh.state

# Removable filesystems currently mounted under /media
mount_points() {
    awk '$1 ~ "^/dev/" && $2 ~ "^/media/" { print $2 }' /proc/mounts 2>/dev/null
}

# Which processes hold this directory open? One "pid:fd" per line.
holders() {
    for pid in $(pgrep -x nautilus 2>/dev/null) $(pgrep -x nemo 2>/dev/null); do
        find /proc/"$pid"/fd -lname "$1" -o -lname "$1/*" 2>/dev/null
    done | sed 's|^/proc/||; s|/fd/|:|' | sort
}

# Make Nautilus re-read the directory attributes
heal() {
    touch "$1" 2>/dev/null || return 1
    sleep 3
    touch "$1" 2>/dev/null          # second try, covers a monitor that was still starting
    logger -t sdcard-refresh "refreshed $1 (file manager view is open)"
}

elapsed=0
while [ "$elapsed" -lt "$WINDOW" ]; do
    for mp in $(mount_points); do
        [ -d "$mp" ] || continue
        sig=$(holders "$mp" | tr -d '\n')
        [ -n "$sig" ] || continue
        key="$(echo "$mp" | tr / _)"
        if [ "$sig" != "$(sed -n "s/^$key //p" "$STATE" 2>/dev/null)" ]; then
            heal "$mp" && {
                sed -i "/^$key /d" "$STATE" 2>/dev/null
                echo "$key $sig" >> "$STATE"
            }
        fi
    done
    sleep "$INTERVAL"
    elapsed=$((elapsed + INTERVAL))
done

exit 0

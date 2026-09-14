#!/bin/sh
/usr/sbin/selinuxenabled 2>/dev/null || exit 0
RESTORCON=/usr/sbin/restorecon
OSTREE=/usr/bin/ostree

mkdir -p /var/rootdirs/home/root

current_deployment=$(${OSTREE} admin status | awk '/^\*/ {print $3}')
if [ -n "$current_deployment" ]; then
       restore_cookie="/etc/restore-$current_deployment"
fi

#Will run only for the first time and once done ostree will create this file
#/etc/labeldone
if [ ! -f "$restore_cookie" ]; then
       ${RESTORCON} -R  /var
       ${RESTORCON}   /sysroot
       rm -rf  /root
       ln -s  /var/roothome   /root
       touch "$restore_cookie"
fi

exit 0

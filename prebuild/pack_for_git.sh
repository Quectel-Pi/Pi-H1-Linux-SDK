#!/bin/bash
# set -ex

sudo chmod u+rw,g+r,o+r debian-rootfs/etc/shadow
sudo chmod u+rw,g+r,o+r debian-rootfs/etc/shadow-
sudo chmod u+rw,g+r,o+r debian-rootfs/etc/gshadow
sudo chmod u+rw,g+r,o+r debian-rootfs/etc/gshadow-
sudo chmod u+rw,g+r,o+r debian-rootfs/etc/ssl/private
sudo chmod u+rw,g+r,o+r debian-rootfs/etc/security/opasswd
sudo chmod u+rw,g+r,o+r debian-rootfs/var/lib/apt/lists/partial
sudo chmod u+rw,g+r,o+r debian-rootfs/var/lib/private
sudo chmod u+rw,g+r,o+r debian-rootfs/var/spool/cron/crontabs
sudo chmod u+rw,g+r,o+r debian-rootfs/etc/sudoers
sudo chmod u+rw,g+r,o+r debian-rootfs/etc/sudoers.d -R
sudo chmod u+rw,g+r,o+r debian-rootfs/etc/polkit-1/rules.d
sudo chmod u+rw,g+r,o+r debian-rootfs/etc/ppp -R
sudo chmod u+rw,g+r,o+r debian-rootfs/etc/security/opasswd
sudo chmod u+rw,g+r,o+r debian-rootfs/var/lib/ -R
sudo chmod u+rw,g+r,o+r debian-rootfs/usr/lib/cups/ -R
sudo chmod u+rw,g+r,o+r debian-rootfs/etc/sssd
sudo chmod u+rw,g+r,o+r debian-rootfs/etc/brlapi.key
sudo chmod u+rw,g+r,o+r debian-rootfs/etc/credstore.encrypted -R
sudo chmod u+rw,g+r,o+r debian-rootfs/etc/credstore
sudo chmod u+rw,g+r,o+r debian-rootfs/usr/lib/netplan -R
sudo chmod u+rw,g+r,o+r debian-rootfs/usr/libexec/sssd -R
sudo rm debian-rootfs/var/lib/apt/lists/lock
sudo rm debian-rootfs/var/lib/dpkg/lock
sudo rm debian-rootfs/var/lib/dpkg/lock-frontend
sudo rm var/lib/dpkg/triggers/Lock
sudo rm debian-rootfs/etc/.pwd.lock
sudo rm debian-rootfs/var/lib/dpkg/triggers/Lock

#sudo tar --numeric-owner -cJf debian-gnome-rootfs.tar.xz -C debian-rootfs . --exclude='swapfile' --exclude='*.iso'
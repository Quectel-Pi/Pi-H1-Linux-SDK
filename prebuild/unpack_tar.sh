#!/bin/bash
# set -ex

mkdir -p debian-rootfs
tar --numeric-owner -xJf debian-gnome-rootfs.tar.xz -C debian-gnome-rootfs
tar --numeric-owner -xJf debian-weston-rootfs.tar.xz -C debian-weston-rootfs
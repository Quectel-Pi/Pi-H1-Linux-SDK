# 用 openembedded-core scarthgap 的修复版伪配方覆盖 poky 里过老的 pseudo 1.9.0,
# 使其带上 openat2 拦截。
#
# 背景: 宿主 tar 因 Ubuntu 安全补丁改用 openat2() 打开/解压目录, pseudo 1.9.0
# 完全没有 openat2 处理, 于是不认识该目录 fd, do_package 的 perform_packagecopy
# (tar | tar) 报:
#   got *at() syscall for unknown directory, fd 4
#   unknown base path for fd 4, path include
#   tar: ./usr/include: Cannot mkdir: Bad address
#
# 对齐提交: "pseudo: Update to include an openat2 fix"
#   34b74540ee497e2cc89211d7aa2772097b6fa79b
#
# 说明: 1.9.3 上游已修掉 PIE flags 与 glibc 2.38 的问题, 因此
#   0001-configure-Prune-PIE-flags.patch / glibc238.patch 已从 SRC_URI 移除
#   (它们无法再应用到新源码)。这里连同 SRC_URI 一起覆盖, 只保留仍需要的
#   fallback-passwd / fallback-group 继续从 poky 原配方的 files/ 取;
#   older-glibc-symbols.patch 需用修复版(旧的上下文对不上新 Makefile.in),
#   放在本目录 files/ 下并通过 FILESEXTRAPATHS 覆盖 poky 的旧版。
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI = "git://git.yoctoproject.org/pseudo;branch=master;protocol=https \
           file://fallback-passwd \
           file://fallback-group \
           "

SRCREV = "9ab513512d8b5180a430ae4fa738cb531154cdef"
PV = "1.9.3+git"
# Pi-H1-Linux-SDK (QSM565DWF / QCM6490 Yocto Scarthgap 5.0) 构建环境
#
# 自包含镜像: 原先这里 `FROM quectel-pi-builder:latest` (依赖 QPi-SDK-H1 Simple
# 侧另行构建的基础镜像), 导致单独拿到本 SDK 的用户无法构建 —— 插件统一走 Docker
# 编译后, 镜像必须能独立构建。以下把基础镜像 (Ubuntu 22.04 + 工具链依赖 + Python
# 多版本 + q1000..q1020 免密 sudo 用户) 的内容原样并入, 再叠加 Yocto 的适配。
FROM ubuntu:22.04

# ---------------------------------------------------------------------------
# 基础段 (来自 QPi-SDK-H1 docker_env/Dockerfile 的 quectel-pi-builder)
# ---------------------------------------------------------------------------
ENV DEBIAN_FRONTEND noninteractive
RUN apt-get update
RUN apt install -y software-properties-common
RUN add-apt-repository -y ppa:deadsnakes/ppa
RUN apt-get update
RUN apt-get install -y git-core gnupg flex bison build-essential zip curl zlib1g-dev \
						libc6-dev-i386 libncurses5 lib32ncurses5-dev x11proto-core-dev \
						libx11-dev lib32z1-dev libgl1-mesa-dev libxml2-utils xsltproc \
						unzip fontconfig pv chrpath cpio diffstat gawk wget zstd  liblz4-tool \
						rsync libxml-simple-perl libelf-dev uuid-dev python3.9 python3.8  python2.7 \
						xmlstarlet texinfo  libarchive-dev ssh libselinux1-dev \
						fakechroot libiberty-dev qemu-user-static g++ gcc \
						make libwayland-dev fakeroot libpam0g-dev openjdk-8-jdk-headless binutils-dev \
						util-linux uuid-dev  python3-distutils \
						bc device-tree-compiler python3-pefile sudo btrfs-progs \
						mtools cmake usbutils libusb-1.0-0 libxml2-dev libzip-dev \
						p7zip-full libssl-dev

# Set the locale
RUN apt-get install -y locales
RUN locale-gen en_US.UTF-8
ENV LANG en_US.UTF-8
ENV LANGUAGE en_US:en
ENV LC_ALL en_US.UTF-8


RUN ln -sf /usr/bin/python2.7 /usr/bin/python
RUN ln -sf /usr/bin/python3.9 /usr/bin/python3

RUN echo 'root:root' | chpasswd
RUN useradd -m q1000
RUN useradd -m q1001
RUN useradd -m q1002
RUN useradd -m q1003
RUN useradd -m q1004
RUN useradd -m q1005
RUN useradd -m q1006
RUN useradd -m q1007
RUN useradd -m q1008
RUN useradd -m q1009
RUN useradd -m q1010
RUN useradd -m q1011
RUN useradd -m q1012
RUN useradd -m q1013
RUN useradd -m q1014
RUN useradd -m q1015
RUN useradd -m q1016
RUN useradd -m q1017
RUN useradd -m q1018
RUN useradd -m q1019
RUN useradd -m q1020
RUN echo "q1000 ALL=(ALL:ALL) NOPASSWD: ALL" >>/etc/sudoers
RUN echo "q1001 ALL=(ALL:ALL) NOPASSWD: ALL" >>/etc/sudoers
RUN echo "q1002 ALL=(ALL:ALL) NOPASSWD: ALL" >>/etc/sudoers
RUN echo "q1003 ALL=(ALL:ALL) NOPASSWD: ALL" >>/etc/sudoers
RUN echo "q1004 ALL=(ALL:ALL) NOPASSWD: ALL" >>/etc/sudoers
RUN echo "q1005 ALL=(ALL:ALL) NOPASSWD: ALL" >>/etc/sudoers
RUN echo "q1006 ALL=(ALL:ALL) NOPASSWD: ALL" >>/etc/sudoers
RUN echo "q1007 ALL=(ALL:ALL) NOPASSWD: ALL" >>/etc/sudoers
RUN echo "q1008 ALL=(ALL:ALL) NOPASSWD: ALL" >>/etc/sudoers
RUN echo "q1009 ALL=(ALL:ALL) NOPASSWD: ALL" >>/etc/sudoers
RUN echo "q1010 ALL=(ALL:ALL) NOPASSWD: ALL" >>/etc/sudoers
RUN echo "q1011 ALL=(ALL:ALL) NOPASSWD: ALL" >>/etc/sudoers
RUN echo "q1012 ALL=(ALL:ALL) NOPASSWD: ALL" >>/etc/sudoers
RUN echo "q1013 ALL=(ALL:ALL) NOPASSWD: ALL" >>/etc/sudoers
RUN echo "q1014 ALL=(ALL:ALL) NOPASSWD: ALL" >>/etc/sudoers
RUN echo "q1015 ALL=(ALL:ALL) NOPASSWD: ALL" >>/etc/sudoers
RUN echo "q1016 ALL=(ALL:ALL) NOPASSWD: ALL" >>/etc/sudoers
RUN echo "q1017 ALL=(ALL:ALL) NOPASSWD: ALL" >>/etc/sudoers
RUN echo "q1018 ALL=(ALL:ALL) NOPASSWD: ALL" >>/etc/sudoers
RUN echo "q1019 ALL=(ALL:ALL) NOPASSWD: ALL" >>/etc/sudoers
RUN echo "q1020 ALL=(ALL:ALL) NOPASSWD: ALL" >>/etc/sudoers
ENV SHELL /bin/bash

RUN rm /bin/sh && ln -sf /bin/bash /bin/sh

# ---------------------------------------------------------------------------
# Yocto 适配段 (原 docker_env/Dockerfile 的追加内容)
# ---------------------------------------------------------------------------

# 1) 恢复系统 Python 3.10 (基础段把 python3 链到 deadsnakes 3.9、python 链到 2.7)
#    Yocto Scarthgap 官方支持 Python 3.8-3.12, 系统 3.10 与 apt 的 python3-* 包配套;
#    config_parser.py 通过 `python` 调用, 必须是 python3 语法 + pyhocon
RUN rm -f /usr/bin/python /usr/bin/python3 && \
    ln -sf /usr/bin/python3.10 /usr/bin/python3 && \
    ln -sf /usr/bin/python3.10 /usr/bin/python

# 2) 补齐 Yocto Scarthgap 构建依赖 (对照 SDK install_build_env.sh)
ENV DEBIAN_FRONTEND noninteractive
RUN apt-get update && apt-get install -y \
    python3-pip \
    python3-pexpect \
    python3-git \
    python3-jinja2 \
    python3-subunit \
    socat \
    xz-utils \
    iputils-ping \
    libsdl1.2-dev \
    mesa-common-dev \
    pylint \
    xterm \
    whiptail \
    vim \
    libxml-opml-simplegen-perl \
    repo \
    && rm -rf /var/lib/apt/lists/*

# 3) config_parser.py / version_parser_auto.py 依赖 pyhocon
RUN python3 -m pip install --no-cache-dir pyhocon
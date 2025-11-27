#!/bin/bash
#
# Script to automatically set up a build environment
#

set -e

echo "==== Updating apt package index ===="
sudo apt update

echo "==== Installing build environment dependencies ===="

sudo apt install -y \
    repo \
    gawk \
    wget \
    git \
    diffstat \
    unzip \
    texinfo \
    gcc \
    build-essential \
    chrpath \
    socat \
    cpio \
    python3 \
    python3-pip \
    python3-pexpect \
    xz-utils \
    debianutils \
    iputils-ping \
    python3-git \
    python3-jinja2 \
    libegl1-mesa \
    libsdl1.2-dev \
    pylint \
    xterm \
    python3-subunit \
    mesa-common-dev \
    zstd \
    liblz4-tool \
    locales \
    tar \
    python-is-python3 \
    file \
    libxml-opml-simplegen-perl \
    vim \
    whiptail \
    g++

echo "==== Generating UTF-8 locale (to avoid build issues) ===="
sudo locale-gen en_US.UTF-8
sudo update-locale LANG=en_US.UTF-8

echo "==== All done! ===="
echo "Build environment setup completed successfully."

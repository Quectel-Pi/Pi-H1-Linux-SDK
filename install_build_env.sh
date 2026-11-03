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
    flex \
    bison \
    libssl-dev \
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

echo "==== Allowing unprivileged user namespaces (required by BitBake) ===="
USERNS_SYSCTL=/proc/sys/kernel/apparmor_restrict_unprivileged_userns
if [ -f "${USERNS_SYSCTL}" ] && [ "$(cat ${USERNS_SYSCTL} 2>/dev/null)" = "1" ]; then
    sudo sh -c "echo 0 > ${USERNS_SYSCTL}" || true
    echo "NOTE: this setting is reset after a host reboot; re-run this script if BitBake fails again."
fi

echo "==== All done! ===="
echo "Build environment setup completed successfully."

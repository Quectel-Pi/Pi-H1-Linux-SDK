# Pi-H1-Linux-SDK (QSM565DWF / QCM6490 Yocto Scarthgap 5.0) 构建环境
# 基于 QPi-SDK-H1 的 quectel-pi-builder (Ubuntu 22.04) 扩展适配 Yocto 编译
FROM quectel-pi-builder:latest

# 1) 恢复系统 Python 3.10 (原镜像把 python3 链到 deadsnakes 3.9、python 链到 2.7)
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

Quectel PI H1 build
=====

## Host Requirements

| Component           | Recommended Configuration                           |
| ------------------ | -------------------------------------------------- |
| CPU                | **Intel i7-8700**, or **AMD Ryzen 5 3600 / 5600X** |
| Disk Space         | **300 GB** free (Swap > **32 GB**)                 |
| RAM                | **16 GB** (recommended **32 GB**)                  |
| Operating System   | **Ubuntu 22.04**                                   |

## Get SDK

    git clone https://github.com/Quectel-Pi/Pi-H1-Linux-SDK.git

## Environment Setup

    cd your/path/to/Pi-H1-Linux-SDK/

    ./install_build_env.sh

## Build

    cd your/path/to/Pi-H1-Linux-SDK/

    source quectel_build/compile/build.sh  

    buildconfig QSM565DWF <Your Project ID> STD

    setostreekernel
    
    buildall

    buildpackage

## Get your firmware

    quectel_build/<Your Project ID>



## Build kernel & dtb for debian

    cd your/path/to/Pi-H1-Linux-SDK/

    source quectel_build/compile/build.sh  

    buildconfig QSM565DWF <Your Project ID> STD

    setopenkernel

    buildkernel                 (build kernel in efi.bin if you need)

    builddtb                    (build kernel in dtb.bin if you need)

    buildpackage

## Get your kernel && dtb and replace into debian package

    quectel_build/output/dtb.bin    

    quectel_build/output/efi.bin

## Introduction
OpenEmbedded/Yocto Project layer for Qualcomm based platforms.

This layers provides aditional recipes and machine configuration files for Qualcomm platform.

This layer depends on:

| URI    | Branch |
| -------- | ------- |
| https://git.yoctoproject.org/meta-qcom | scarthgap |
| https://github.com/openembedded/meta-openembedded | scarthgap |
| https://git.yoctoproject.org/poky | scarthgap |
| https://git.yoctoproject.org/meta-security | scarthgap |
| https://git.yoctoproject.org/meta-virtualization | scarthgap |
| https://github.com/qualcomm-linux/meta-qcom-distro | scarthgap |

## Adding a new machine
### Add Machine
To add a new machine introduce a new machine configuration file at `layers/meta-qcom-hwe/conf/machine/`,
for example, `layers/meta-qcom-hwe/conf/machine/testboard.conf`

```bash
#@TYPE: Machine
#@NAME: TestBoard
#@DESCRIPTION: Machine configuration for a development board, based on Qualcomm QCM6490

MACHINEOVERRIDES =. "qcm6490:"
require conf/machine/include/qcom-qcs6490.inc
```
Adding `MACHINEOVERRIDES` helps to re-use the configurations in recipes created for qcm6490.

### Build an image for the machine added
```bash
MACHINE="testboard" DISTRO="qcom-wayland" source setup-environment
bitbake qcom-multimedia-image
```
'qcom-wayland' DISTRO and 'qcom-multimedia-image' are defined in meta-qcom-distro layer.

### Flashing the images on the device
Flash the images on the device using qdl tool.


## Known Issues


## Maintainer(s)
1. Naveen Kumar <quic_kumarn@quicinc.com>
2. Sourabh Banerjee <quic_sbanerje@quicinc.com>
3. Viswanath Kraleti <quic_vkraleti@quicinc.com>

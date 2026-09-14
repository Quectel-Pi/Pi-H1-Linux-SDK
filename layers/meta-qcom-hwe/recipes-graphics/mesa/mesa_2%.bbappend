# We can not directly use PV here as it will be expanded too early to the '2%' value.
# Thus temporary variable is used which gets substituted later.
FILESEXTRAPATHS:prepend:qcom-base-bsp := "${THISDIR}/${BPN}-${package_version}:"
package_version = '${PV}'

SRC_URI:append:qcom-base-bsp = " file://0001-freedreno-Add-support-for-Adreno-663-GPU.patch \
                                 file://0001-freedreno-Add-initial-A621-support.patch \
                                 file://0001-Add-support-for-Adreno623-GPU.patch \
                               "

inherit extrausers
do_rootfs[vardepsexclude] += "BB_TASKDEPDATA"

IMAGE_OVERHEAD_FACTOR = "1.2"
EXTRA_USERS_PARAMS:append = " \
    usermod -p '\$6\$v8m9YD0CZxYAnFr0\$riOffip4/b8mNXzxMXmxByeQP9ZjYRg9563/HhqGD/Aq0/oyWxkCNmdd9i.IRmLuoeVkVLfTV7jxs/h/zlMy5/' root; \
    useradd -p '\$6\$v8m9YD0CZxYAnFr0\$riOffip4/b8mNXzxMXmxByeQP9ZjYRg9563/HhqGD/Aq0/oyWxkCNmdd9i.IRmLuoeVkVLfTV7jxs/h/zlMy5/' pi; \
    usermod -a -G sudo,audio,video,users pi; \
    "
IMAGE_FEATURES:remove = "read-only-rootfs"
IMAGE_INSTALL:append = " pciutils usbutils"
IMAGE_INSTALL:append = " yt6801 r8168 qca1023-wlan"
IMAGE_INSTALL:append = " qmi-wwan-q quectel-cm quectel-firehose quectel-log"
IMAGE_INSTALL:append = " quectel-tests lgpio"
IMAGE_INSTALL:append = " key-event-handler"
IMAGE_INSTALL:append = " key-event-print"
IMAGE_INSTALL:append = " gstreamer1.0-libav"
IMAGE_INSTALL:append = " sudo"
IMAGE_INSTALL:append = " close-sources"

# Remove the mosquitto MQTT broker (listens on :1883) from the image.
# packagegroup-qcom-gst-dependencies pulls in the mosquitto *broker* package
# as a hard RDEPENDS; we only need the library at most. The broker service is
# not wanted on a Debian-based QuecPi device, so drop it here at the source.
# libmosquitto1 is left untouched (pulled separately if any package needs it).
RDEPENDS:packagegroup-qcom-gst-dependencies:remove = "mosquitto"

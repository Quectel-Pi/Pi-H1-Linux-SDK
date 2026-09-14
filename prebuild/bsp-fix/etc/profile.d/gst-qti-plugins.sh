# QCom adreno GStreamer plugins are installed by the Yocto BSP layer into
# /usr/lib/gstreamer-1.0/ (the Yocto default plugin dir). Debian's trixie
# libgstreamer, however, scans only the multiarch dir
# /usr/lib/aarch64-linux-gnu/gstreamer-1.0/ by default, so without this path
# override the QCom qti* plugins are never registered and customer workloads
# that rely on NPU/video DSP acceleration silently miss them.
# This is mirrored by /etc/environment.d/gst-qti-plugins.conf which covers
# PAM sessions (GNOME, ssh); this profile snippet covers non-PAM/POSIX shells.
export GST_PLUGIN_PATH=/usr/lib/gstreamer-1.0${GST_PLUGIN_PATH:+:$GST_PLUGIN_PATH}

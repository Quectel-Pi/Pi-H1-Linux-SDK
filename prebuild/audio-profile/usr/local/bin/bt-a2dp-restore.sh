#!/bin/bash
# bt-a2dp-restore.sh
#
# Keep the Bluetooth A2DP sink as the default audio output.
#
# The GDM greeter session no longer runs its own WirePlumber instance (its
# wireplumber.service is masked under /var/lib/gdm3), so a headset that
# auto-reconnects at boot gets its A2DP profile directly. What is still needed
# is the default output: when the headset shows up as an A2DP sink, PipeWire
# can keep the previous default (e.g. HDMI). This service watches the audio
# graph and, whenever a Bluetooth A2DP sink newly appears, makes it the default
# output.

# Only useful for a real user session, skip the GDM greeter.
[ "$(id -un)" = "Debian-gdm" ] && exit 0

last=""
while true; do
    cur=$(pw-dump 2>/dev/null | python3 -c '
import json, sys
for o in json.load(sys.stdin):
    if o.get("info", {}).get("props", {}).get("api.bluez5.profile") == "a2dp-sink":
        print(o.get("id"))
        break
')
    if [ -n "$cur" ] && [ "$cur" != "$last" ]; then
        wpctl set-default "$cur" >/dev/null 2>&1
    fi
    last="$cur"
    sleep 2
done

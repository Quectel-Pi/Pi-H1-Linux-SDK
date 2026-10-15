#!/bin/sh
# Reconnect paired Bluetooth audio devices at boot.
#
# BlueZ's own reconnect (main.conf [Policy] ReconnectUUIDs) only fires on link
# loss, so nothing pushes the sink back up after a reboot: the board comes up
# unconnected and the speaker has to be power-cycled to be seen again.
# This is that missing push. The first ~20 s after boot the controller is not
# ready yet (connect returns
# org.bluez.Error.Failed br-connection-aborted-by-local), hence the retries.
#
# ponytail: sequential bluetoothctl calls, fine for a handful of devices.
for _ in 1 2 3 4 5 6 7 8 9 10; do
	found=0
	missing=0
	for d in $(bluetoothctl devices Paired | cut -d' ' -f2); do
		bluetoothctl info "$d" | grep -q 'Icon: audio' || continue
		found=$((found + 1))
		bluetoothctl info "$d" | grep -q 'Connected: yes' && continue
		bluetoothctl connect "$d" || true
		bluetoothctl info "$d" | grep -q 'Connected: yes' || missing=$((missing + 1))
	done
	# found=0 means bluetoothd is still coming up and has not loaded the paired
	# device list yet - wait, never treat that as success.
	[ "$found" -gt 0 ] && [ "$missing" -eq 0 ] && exit 0
	sleep 10
done
exit 1

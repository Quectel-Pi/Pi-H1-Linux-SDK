# meta-quectel fixes for the USB gadget/adbd dead-lock seen on QuecPi H1.
#
# Symptom (2026-11-02): one OOM kill of adbd left the Type-C port enumerated as
# nothing at all, and no amount of replugging recovered it until reboot:
#   android-tools-adbd.service: Start request repeated too quickly.  (failed)
#   android-gadget-cleanup: rm: cannot remove 'configs/c.1/ffs.usb0': No such file
#   android-gadget-cleanup: rmdir: failed to remove 'adb': Directory not empty
#   android-gadget-setup: mkdir: cannot create directory 'adb': File exists
#
# Three independent defects, all fixed by overriding the three files above:
#   A android-gadget-cleanup only removed a link named "ffs.usb0", while qusb
#     links the functions as f1/f2/f3 -> cleanup never finished, gadget could
#     never be rebuilt.
#   B the drop-in added a second, unhyphenated ExecStartPre (upstream unit
#     already has "ExecStartPre=-..."), so every start was a hard failure.
#   C no start limit override: 6 fast failures = permanently failed unit.
#
# android-gadget-setup is made idempotent as well, so an adbd restart no longer
# rebuilds the gadget and throws away qusb's 902D composition (RNDIS+DIAG+ADB).
# android-gadget-start is left to meta-qcom-hwe (it already retries the UDC bind)
# and is invoked with '-' from the drop-in so a failed bind can't fail the unit.

FILESEXTRAPATHS:prepend := "${THISDIR}/files-configfs:"

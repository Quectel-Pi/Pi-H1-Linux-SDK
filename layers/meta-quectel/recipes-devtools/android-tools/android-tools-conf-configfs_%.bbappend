# meta-quectel fixes for the USB gadget/adbd dead-lock seen on QuecPi H1.
#
# Symptom (2026-11-02): one OOM kill of adbd left the Type-C port enumerated as
# nothing at all, and no amount of replugging recovered it until reboot:
#   android-tools-adbd.service: Start request repeated too quickly.  (failed)
#   android-gadget-cleanup: rm: cannot remove 'configs/c.1/ffs.usb0': No such file
#   android-gadget-cleanup: rmdir: failed to remove 'adb': Directory not empty
#   android-gadget-setup: mkdir: cannot create directory 'adb': File exists
#
# Four independent defects; A-C are fixed by overriding cleanup/setup/drop-in:
#   A android-gadget-cleanup only removed a link named "ffs.usb0", while qusb
#     links the functions as f1/f2/f3 -> cleanup never finished, gadget could
#     never be rebuilt.
#   B the drop-in added a second, unhyphenated ExecStartPre (upstream unit
#     already has "ExecStartPre=-..."), so every start was a hard failure.
#   C no start limit override: 6 fast failures = permanently failed unit.
#
# android-gadget-setup is made idempotent as well, so an adbd restart no longer
# rebuilds the gadget and throws away qusb's 902D composition (RNDIS+DIAG+ADB).
#
# D android-gadget-start (this is a meta-quectel override of the meta-qcom-hwe
#   copy, which had a hardcoded "sleep 3"): lost the race with functionfs, so
#   the UDC write hit f_fs.c:3022 "desc_ready ? 0 : -ENODEV" and the kernel
#   rolled the whole composite bind back:
#     udc a600000.usb: failed to start adb: -19
#     UDC core: adb: couldn't find an available UDC or it's busy
#   It also had no way to notice that it had been superseded, so a stale copy
#   kept writing the UDC for 30 s across three adbd generations. The override
#   probes by writing the UDC (the only reliable readiness signal), backs off
#   while ep1 is absent, and exits as soon as its own adbd PID or InvocationID
#   is gone. Measured after the fix: single OOM kill 6.4 s -> 2.7 s, storm
#   recovery 4.3 s -> 0.6 s, bind errors during a 20-kill storm 32 -> 0.
#
# It is still invoked with '-' from the drop-in so a failed bind can't fail the
# unit.

FILESEXTRAPATHS:prepend := "${THISDIR}/files-configfs:"

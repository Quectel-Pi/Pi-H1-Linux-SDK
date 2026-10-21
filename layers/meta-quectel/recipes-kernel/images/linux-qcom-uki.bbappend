# QCS6490's DTB carries a Gunyah /hypervisor node, so systemd reports the
# platform as "vm-other" and gnome-settings-daemon then refuses to arm the idle
# suspend timer ("Ignoring sleep timeout with suspend action inside VM").
# Linux runs on bare metal here, so force GSD's VM detection to false.
KERNEL_CMDLINE_EXTRA:append = " gnome.is_vm=0"

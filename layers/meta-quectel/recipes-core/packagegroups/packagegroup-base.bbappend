# Remove rpcbind (RPC portmapper, listens on :111) from the image.
# packagegroup-base-nfs hard-RDEPENDS on rpcbind, and "nfs" is in the default
# DISTRO_FEATURES so packagegroup-base-nfs is installed. The rpcbind service is
# not needed on a Debian-based QuecPi device, so drop it here at the source.
# Removing rpcbind leaves packagegroup-base-nfs effectively empty, which is
# fine for this device (no NFS server/client in use).
RDEPENDS:packagegroup-base-nfs:remove = "rpcbind"

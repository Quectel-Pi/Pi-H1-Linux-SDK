require qcom-guestvm-image.bb

SUMMARY = "Housekeeping VM for edgegateway"

IMAGE_FEATURES += " splash tools-debug allow-root-login  post-install-logging"

inherit core-image features_check extrausers

REQUIRED_DISTRO_FEATURES = "pam systemd"

CORE_IMAGE_BASE_INSTALL += " \
    packagegroup-qcom-hkvm-guest \
"


EXTRA_USERS_PARAMS = "\
    useradd -r -s /bin/false system; \
    usermod -p '\$6\$UDMimfYF\$akpHo9mLD4z0vQyKzYxYbsdYxnpUD7B7rHskq1E3zXK8ygxzq719wMxI78i0TIIE0NB1jUToeeFzWXVpBBjR8.' root; \
    "

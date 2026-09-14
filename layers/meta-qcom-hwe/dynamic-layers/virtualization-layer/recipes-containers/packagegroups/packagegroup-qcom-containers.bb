SUMMARY = "packagegroups for docker containers and docker-compose"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit packagegroup

PACKAGES = "packagegroup-qcom-containers"

RDEPENDS:packagegroup-qcom-containers = " \
    docker-compose \
    docker-conf \
    packagegroup-container \
"

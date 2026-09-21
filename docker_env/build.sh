#!/bin/bash

set -ex
cd `dirname $0`

export IMAGENAME=quectel-pi-yocto-builder
docker images | grep -q $IMAGENAME || ./docker.build
./docker.run "$*"

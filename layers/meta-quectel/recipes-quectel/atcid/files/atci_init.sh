#!/bin/sh

while [ ! -e /dev/ttyGS0 ]; do
    echo "Waiting for /dev/ttyGS0..."
    sleep 1
done

exec /usr/sbin/atcid

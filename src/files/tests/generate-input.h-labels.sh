#!/bin/bash

python generate-input.h-labels.py ${STAGING_DIR}/usr/include/linux/input-event-codes.h > input.h-labels.h

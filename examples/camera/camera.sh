#!/bin/bash
BASEDIR='/Users/martin/latproc'
ulimit -c unlimited

#CAMERA=M_GrabBaleGateCamera
CAMERA=camera
CAMERA_HOST=0.0.0.0
CAMERA_PORT=8005
CAMERA_PROPERTY="$CAMERA".CameraStatus
CAMERA_M_NAME="$CAMERA"
CAMERA_PATTERN='([a-zA-z]*).*'
TIMEOUT=--no_timeout_disconnect
sleep 1
exec ${BASEDIR}/iod/device_connector --host ${CAMERA_HOST} --port ${CAMERA_PORT} --property ${CAMERA_PROPERTY} --name ${CAMERA_M_NAME} --pattern "${CAMERA_PATTERN}" ${TIMEOUT} # > /tmp/camera.log  2>&1


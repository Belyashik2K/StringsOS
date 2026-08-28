#!/bin/bash
set -e

echo "Starting QEMU (StringsOS) with a VNC display on :0 ..."
qemu-system-i386 \
    -display vnc=0.0.0.0:0 \
    -fda /app/build/bootsect.bin \
    -fdb /app/build/kernel.bin &
QEMU_PID=$!

echo "Starting noVNC on http://0.0.0.0:6080 ..."
websockify --web=/usr/share/novnc 6080 localhost:5900 &
NOVNC_PID=$!

trap 'kill $QEMU_PID $NOVNC_PID 2>/dev/null' TERM INT
wait -n $QEMU_PID $NOVNC_PID

#!/bin/bash
echo "Setting Mosquitto password for user $1"
mosquitto_passwd -c /etc/mosquitto/passwd "$1" <<EOF
$2
$2
EOF

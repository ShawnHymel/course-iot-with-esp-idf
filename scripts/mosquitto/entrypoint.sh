#!/bin/bash

# Source .bashrc to ensure the path is set
source $HOME/.bashrc

# Start avahi daemon
avahi-daemon &

# Run MQTTX Web client in the background
http-server /var/www/mqttx/ -p 80 &

# Run Mosquitto server
/usr/sbin/mosquitto -c /etc/mosquitto/mosquitto.conf

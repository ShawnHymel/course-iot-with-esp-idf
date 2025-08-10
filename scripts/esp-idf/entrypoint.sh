#!/bin/bash

# Source .bashrc to ensure the path is set
source $HOME/.bashrc

# Enable ESP-IDF
# . /opt/toolchains/esp-idf/export.sh

# Set global environment variables
source /etc/environment
export IDF_COMPILER_PATH
export XTENSA_C_COMPILER_PATH=$(whereis -b xtensa-esp-elf-gcc | cut -d ' ' -f2)
export XTENSA_CPP_COMPILER_PATH=$(whereis -b xtensa-esp-elf-g++ | cut -d ' ' -f2)

# Copy the C/C++ extension configuration
mkdir -p /workspace/.vscode
cp /c_cpp_properties.json /workspace/.vscode/c_cpp_properties.json

# Start the SSH daemon
/usr/sbin/sshd

# Start avahi daemon
# avahi-daemon &

# Run Mosquitto server
/usr/sbin/mosquitto -c /etc/mosquitto/mosquitto.conf -d

# Start the code-server
exec code-server --auth none --bind-addr 0.0.0.0:${VS_CODE_SERVER_PORT} /esp-idf.code-workspace

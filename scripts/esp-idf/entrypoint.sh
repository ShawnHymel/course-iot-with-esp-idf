#!/bin/bash

# Enable ESP-IDF
. /opt/toolchains/esp-idf/export.sh

# Set global environment variables
source /etc/environment
export IDF_COMPILER_PATH

# Copy the C/C++ extension configuration
mkdir -p /workspace/.vscode
cp /c_cpp_properties.json /workspace/.vscode/c_cpp_properties.json

# Start the SSH daemon
/usr/sbin/sshd

# Start the code-server
exec code-server --auth none --bind-addr 0.0.0.0:${VS_CODE_SERVER_PORT} /fota.code-workspace

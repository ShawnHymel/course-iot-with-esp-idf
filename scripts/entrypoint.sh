#!/bin/bash

# Start the SSH daemon
/usr/sbin/sshd

# Start the code-server
exec code-server --auth none --bind-addr 0.0.0.0:${VS_CODE_SERVER_PORT} /zephyr.code-workspace

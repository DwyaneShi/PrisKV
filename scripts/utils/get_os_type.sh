#!/bin/bash

if [ -f "/etc/debian_version" ]; then
    echo "debian"
elif [ -f "/etc/velinux_version" ]; then
    echo "velinux"
elif [ -f "/etc/redhat-release" ]; then
    echo "redhat"
else
    echo "unknown"
fi

#!/bin/bash

if [ -f "/etc/os-release" ]; then
    os_info=$(cat /etc/os-release)
    version_codename=$(echo "$os_info" | grep -Po '(?<=^VERSION_CODENAME=)"?\K[^"]*')
    echo "${version_codename}"
else
    echo "unknown"
fi
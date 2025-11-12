#!/bin/bash

if [ -f "/etc/os-release" ]; then
    os_info=$(cat /etc/os-release)
    os_name=$(echo "$os_info" | grep -Po '(?<=^ID=)"?\K[^"]*')
    os_version=$(echo "$os_info" | grep -Po '(?<=^VERSION_ID=)"?\K[^"]*')
    echo "${os_name}${os_version}"
else
    echo "unknown"
fi

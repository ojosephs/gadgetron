#!/bin/bash

if [ "$EUID" -ne 0 ]
  then echo "Please run as root"
  exit
fi

if [ -z "$(cat /etc/lsb-release | grep "Ubuntu 20.04")" ] && [ -z "$(cat /etc/lsb-release | grep "Ubuntu 18.04")" ]; then
    echo "Error: This install script is intended for Ubuntu 18.04 and 20.04 only"
    exit 1
fi

# Dependencu list taken from github.com/mathworks-ref-arch/matlab-dockerfile
apt update --quiet
DEBIAN_FRONTEND=noninteractive apt install --no-install-recommends --no-install-suggests --yes \
        ca-certificates \
        libasound2 \
        libatk1.0-0 \
        libavcodec-extra57 \
        libavformat57 \
        libc6 \
        libcairo-gobject2 \
        libcairo2 \
        libcups2 \
        libdbus-1-3 \
        libfontconfig1 \
        libgdk-pixbuf2.0-0 \
        libgstreamer-plugins-base1.0-0 \
        libgstreamer1.0-0 \
        libgtk-3-0 \
        libnspr4 \
        libnss3 \
        libpam0g \
        libpango-1.0-0 \
        libpangocairo-1.0-0 \
        libpangoft2-1.0-0 \
        libpython2.7 \
        libpython3.6 \
        libpython3.7 \
        libselinux1 \
        libsm6 \
        libsndfile1 \
        libx11-6 \
        libx11-xcb1 \
        libxcb1 \
        libxcomposite1 \
        libxcursor1 \
        libxdamage1 \
        libxext6 \
        libxfixes3 \
        libxft2 \
        libxi6 \
        libxinerama1 \
        libxrandr2 \
        libxrender1 \
        libxt6 \
        libxtst6 \
        libxxf86vm1 \
        locales \
        locales-all \
        procps \
        sudo \
        xkb-data

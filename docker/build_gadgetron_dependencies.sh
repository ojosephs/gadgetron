#!/bin/bash

if [ "$EUID" -ne 0 ]
  then echo "Please run as root"
  exit
fi

set -e

WORKDIR="/opt/code"

usage() {
    echo "build-gadgetron.sh            [--work-dir ${WORKDIR}]"
}

# Parse command line arguments
while [ "$1" != "" ]; do
    case $1 in
        -w | --work-dir )         shift
                                  WORKDIR=$1
                                  ;;
        -h | --help )             usage
                                  exit
                                  ;;
        * )                       usage
                                  exit 1
    esac
    shift
done


# Some useful general purpose tools

apt update --quiet
DEBIAN_FRONTEND=noninteractive apt install --no-install-recommends --no-install-suggests --yes \
xterm \
vim

# VERSION & REPO TAGS

ISMRMRD_URL=https://github.com/ismrmrd/ismrmrd.git
ISMRMRD_COMMIT=master
SIEMENS_TO_ISMRMRD_URL=https://github.com/ismrmrd/siemens_to_ismrmrd.git
SIEMENS_TO_ISMRMRD_COMMIT=master
PHILIPS_TO_ISMRMRD_URL=https://github.com/ismrmrd/philips_to_ismrmrd.git
PHILIPS_TO_ISMRMRD_COMMIT=master


# Should these pip3 installs go here?
pip3 install git+https://github.com/ismrmrd/ismrmrd-python.git
pip3 install git+https://github.com/gadgetron/gadgetron-python.git


mkdir -p ${WORKDIR}

#ISMRMRD
cd ${WORKDIR} && \
    rm -rf ismrmrd && \
    git clone ${ISMRMRD_URL} && \
    cd ismrmrd && \
    git checkout ${ISMRMRD_COMMIT} && \
    mkdir build && \
    cd build && \
    cmake ../  -G Ninja && \
    ninja && \
    ninja install

# Install Python interfaces.
pip3 install ismrmrd multimethod 

#SIEMENS_TO_ISMRMRD
cd ${WORKDIR} && \
    rm -rf siemens_to_ismrmrd && \
    git clone ${SIEMENS_TO_ISMRMRD_URL} && \
    cd siemens_to_ismrmrd && \
    git checkout ${SIEMENS_TO_ISMRMRD_COMMIT} && \
    mkdir build && \
    cd build && \
    cmake ../ -G Ninja && \
    ninja && \
    ninja install 

#PHILIPS_TO_ISMRMRD
cd ${WORKDIR} && \
    rm -rf philips_to_ismrmrd && \
    git clone ${PHILIPS_TO_ISMRMRD_URL} && \
    cd philips_to_ismrmrd && \
    git checkout ${PHILIPS_TO_ISMRMRD_COMMIT} && \
    mkdir build && \
    cd build && \
    cmake ../ -G Ninja && \
    ninja && \
    ninja install

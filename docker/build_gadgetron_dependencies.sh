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


# VERSION & REPO TAGS

ISMRMRD_URL=https://github.com/ismrmrd/ismrmrd.git
ISMRMRD_REV=c86c89c
SIEMENS_TO_ISMRMRD_URL=https://github.com/ismrmrd/siemens_to_ismrmrd.git
SIEMENS_TO_ISMRMRD_REV=494963c
PHILIPS_TO_ISMRMRD_URL=https://github.com/ismrmrd/philips_to_ismrmrd.git
PHILIPS_TO_ISMRMRD_REV=9ef92a1


mkdir -p ${WORKDIR}

#ISMRMRD
cd ${WORKDIR} && \
    rm -rf ismrmrd && \
    git clone ${ISMRMRD_URL} && \
    cd ismrmrd && \
    git checkout ${ISMRMRD_REV} && \
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
    git checkout ${SIEMENS_TO_ISMRMRD_REV} && \
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
    git checkout ${PHILIPS_TO_ISMRMRD_REV} && \
    mkdir build && \
    cd build && \
    cmake ../ -G Ninja && \
    ninja && \
    ninja install

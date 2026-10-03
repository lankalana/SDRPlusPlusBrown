#!/bin/bash
set -e
cd /root

# Install dependencies and tools
apt update
apt install -y unzip build-essential cmake git libfftw3-dev libglfw3-dev libvolk-dev libzstd-dev \
            librtaudio-dev libhackrf-dev p7zip-full wget portaudio19-dev \
            autoconf libtool xxd libspdlog-dev liborc-0.4-dev

# Install SDRPlay libraries
SDRPLAY_ARCH=$(dpkg --print-architecture)
wget https://www.sdrplay.com/software/SDRplay_RSP_API-Linux-3.15.2.run
7z x ./SDRplay_RSP_API-Linux-3.15.2.run
7z x ./SDRplay_RSP_API-Linux-3.15.2
cp $SDRPLAY_ARCH/libsdrplay_api.so.3.15 /usr/lib/libsdrplay_api.so
cp inc/* /usr/include/

cd SDRPlusPlus
mkdir build
cd build
cmake .. -DOPT_BUILD_SDRPLAY_SOURCE=ON -DOPT_BUILD_NEW_PORTAUDIO_SINK=ON -DOPT_BUILD_CH_EXTRAVHF_DECODER=ON -DOPT_BUILD_CH_TETRA_DEMODULATOR=ON
make VERBOSE=1 -j2

cd ..
sh make_debian_package.sh ./build 'libfftw3-dev, libglfw3-dev, libvolk-dev, librtaudio-dev, libzstd-dev, liborc-0.4-dev'
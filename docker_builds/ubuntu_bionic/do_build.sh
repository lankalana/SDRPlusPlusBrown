#!/bin/bash
set -e
cd /root

# Update repos to get a more recent cmake version
apt update
apt install -y unzip gpg wget
wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null
echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ bionic main' | tee /etc/apt/sources.list.d/kitware.list >/dev/null
apt update

# Install dependencies and tools
apt install -y unzip build-essential cmake git libfftw3-dev libglfw3-dev libvolk1-dev libzstd-dev \
            librtaudio-dev libhackrf-dev p7zip-full wget portaudio19-dev \
            libudev-dev autoconf libtool xxd libspdlog-dev liborc-0.4-dev

# Install SDRPlay libraries
SDRPLAY_ARCH=$(dpkg --print-architecture)
wget https://www.sdrplay.com/software/SDRplay_RSP_API-Linux-3.15.2.run
7z x ./SDRplay_RSP_API-Linux-3.15.2.run
7z x ./SDRplay_RSP_API-Linux-3.15.2
cp $SDRPLAY_ARCH/libsdrplay_api.so.3.15 /usr/lib/libsdrplay_api.so
cp inc/* /usr/include/

# Install a more recent libusb version
wget https://github.com/libusb/libusb/releases/download/v1.0.25/libusb-1.0.25.tar.bz2
tar -xvf libusb-1.0.25.tar.bz2
cd libusb-1.0.25
./configure
make -j2
make install
cd ..

# Build SDR++ Itself
cd SDRPlusPlus
mkdir build
cd build
cmake .. -DOPT_BUILD_SDRPLAY_SOURCE=ON -DOPT_BUILD_NEW_PORTAUDIO_SINK=ON -DOPT_OVERRIDE_STD_FILESYSTEM=ON -DOPT_BUILD_CH_EXTRAVHF_DECODER=ON -DOPT_BUILD_CH_TETRA_DEMODULATOR=ON
make VERBOSE=1 -j2

# Generate package
cd ..
sh make_debian_package.sh ./build 'libfftw3-dev, libglfw3-dev, libvolk1-dev, librtaudio-dev, libzstd-dev, liborc-0.4-dev'
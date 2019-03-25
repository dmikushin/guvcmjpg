## Prerequisites

```
sudo apt-get install intltool autotools-dev libsdl1.2-dev libgtk-3-dev portaudio19-dev libpng-dev libavcodec-dev libavutil-dev libudev-dev libusb-1.0-0-dev libpulse-dev libgsl0-dev libv4l-dev libv4l2rds0 libsdl2-dev
```

## Building

```
./bootstrap.sh
mkdir build
cd build
CFLAGS="-g -O3 -march=native -ffast-math" ../configure --prefix=/opt/guvcmjpg
make -j4
sudo make install
```

## Deployment

```
/opt/guvcmjpg/bin/guvcmjpg
```

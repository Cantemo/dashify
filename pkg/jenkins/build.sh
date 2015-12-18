#! /bin/bash

cd $WORKSPACE/lib/ffmpeg

FFMPEG_INSTALL_PATH=$WORKSPACE/ffmpeg-install/

# Build with as little code as possible
./configure --disable-encoders --disable-decoders --disable-hwaccels --disable-bsfs --disable-devices --disable-filters --disable-swresample --disable-swscale --prefix=$FFMPEG_INSTALL_PATH --disable-decoder=h264 --disable-decoder=h263
make
make install

PKG_CONFIG_PATH=$FFMPEG_INSTALL_PATH/lib/pkgconfig/
export PKG_CONFIG_PATH

cd $WORKSPACE/src
make
cp dashify $WORKSPACE

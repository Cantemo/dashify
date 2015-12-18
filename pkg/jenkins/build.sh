#! /bin/bash

cd $WORKSPACE/lib/ffmpeg

FFMPEG_INSTALL_PATH=$WORKSPACE/ffmpeg-install/

# Build with as little as possible
./configure \
    --disable-programs \
    --disable-encoders \
    --disable-decoders \
    --disable-hwaccels \
    --disable-parsers \
    --disable-demuxers \
    --disable-muxers \
    --disable-bsfs \
    --disable-devices \
    --disable-filters \
    --disable-swresample \
    --disable-swscale \
    --disable-decoder=h263 \
    --disable-decoder=h264 \
    --disable-protocols \
    --enable-protocol=file \
    --enable-demuxer=mov \
    --enable-muxer=mp4 \
    --prefix=$FFMPEG_INSTALL_PATH \

make
make install

PKG_CONFIG_PATH=$FFMPEG_INSTALL_PATH/lib/pkgconfig/
export PKG_CONFIG_PATH

cd $WORKSPACE/src
make
(cd test && bash -xe ./test.sh)

cp dashify $WORKSPACE

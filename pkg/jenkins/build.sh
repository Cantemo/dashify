cd $WORKSPACE/lib/ffmpeg

# Build with as little code as possible
./configure --disable-encoders --disable-decoders --disable-hwaccels --disable-bsfs --disable-devices --disable-filters --disable-swresample --disable-swscale --prefix=$WORKSPACE/ffmpeg-install/ --disable-decoder=h264 --disable-decoder=h263
make
make install

cd $WORKSPACE/src
make
cp dashify $WORKSPACE

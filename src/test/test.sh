#! /bin/bash

set -e

# This should user mp4dump from Bento4 to inspect the actualy output files
echo "Testing init segment"
../dashify --init short.mp4 - > /dev/null
echo "Testing data segment"
../dashify --segment 1 short.mp4 - > /dev/null
echo "Testing codec"
CODEC=$(../dashify --stream v:0 --codec short.mp4 -)

if [[ $CODEC != "avc1.640015" ]]; then
    echo "$CODEC != avc1.640015"
    exit 1
fi

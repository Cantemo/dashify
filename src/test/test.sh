#! /bin/bash

set -e

# This should user mp4dump from Bento4 to inspect the actualy output files
../dashify --init short.mp4 - > /dev/null
../dashify --segment 1 short.mp4 - > /dev/null

#!/bin/sh

set -e

make distclean 
./tools/configure.sh sim:lvgl_lcd && make -j$(nproc)

# ./nuttx
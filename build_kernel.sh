#!/bin/bash

export ARCH=arm
export CROSS_COMPILE=/opt/toolchains/arm-eabi-4.4.3/bin/arm-eabi-

make msm8660-perf_DALI_KOR_KT_defconfig
make

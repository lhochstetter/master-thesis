#!/bin/bash

firmwarebin="build/app-template.bin"
filesize=$(stat -c"%s" $firmwarebin)
x1=$(($filesize / 16))
x2=$(expr $x1 + 1)
x3=$(expr $x2 \* 16)
padsize=$(expr $x3 - $filesize)

echo "size=$filesize" >> build/aes_key
dd if=/dev/zero ibs=1 count="$padsize" >> $firmwarebin

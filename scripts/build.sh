#! /bin/bash

export FSS_PUB=/keys/fss_pub.pem
export FSS_PRV=/keys/fss_prv.pem

#make -j$(nproc) clean

make -j$(nproc)

python $IDF_PATH/components/esptool_py/esptool/espsecure.py sign_data --keyfile $FSS_PRV /app/build/app-template.bin

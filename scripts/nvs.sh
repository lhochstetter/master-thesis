#! /bin/bash

python $IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py  --input nvs.csv --output config.bin --size 0x020000

python $IDF_PATH/components/esptool_py/esptool/esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect 0x020000 config.bin

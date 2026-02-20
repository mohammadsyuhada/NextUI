#!/bin/sh

cd $(dirname "$0")
./minput.elf &> "$LOGS_PATH/minput.txt"

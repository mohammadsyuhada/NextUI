#!/bin/sh

cd $(dirname "$0")
./bootlogo.elf &> "$LOGS_PATH/bootlogo.txt"
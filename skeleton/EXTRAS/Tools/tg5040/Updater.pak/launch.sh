#!/bin/sh

cd $(dirname "$0")
./updater.elf &> "$LOGS_PATH/updater.txt"

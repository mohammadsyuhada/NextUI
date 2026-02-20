#!/bin/sh

cd "$(dirname "$0")"
./gametime.elf &> "$LOGS_PATH/gametime.txt"

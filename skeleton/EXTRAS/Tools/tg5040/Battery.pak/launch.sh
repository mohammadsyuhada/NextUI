#!/bin/sh

cd $(dirname "$0")
./battery.elf &> "$LOGS_PATH/battery.txt"

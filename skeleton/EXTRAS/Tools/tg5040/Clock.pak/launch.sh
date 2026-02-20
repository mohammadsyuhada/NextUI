#!/bin/sh

cd $(dirname "$0")
./clock.elf &> "$LOGS_PATH/clock.txt"

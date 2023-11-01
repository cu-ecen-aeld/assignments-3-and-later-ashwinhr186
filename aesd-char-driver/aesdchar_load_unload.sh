#!/bin/sh

make clean

sudo ./aesdchar_unload

make

sudo ./aesdchar_load
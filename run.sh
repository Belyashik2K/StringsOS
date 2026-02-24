#!/bin/bash

set -e

BUILD_DIR=build
SRC_DIR=src

do_build() {
    echo "[0/5] Preparing build directory..."
    mkdir -p $BUILD_DIR

    echo "[1/5] Assembling bootloader..."
    fasm $SRC_DIR/bootsect.asm $BUILD_DIR/bootsect.bin

    echo "[2/5] Compiling kernel (C++)..."
    g++ -m32 -ffreestanding -fno-pie -fno-exceptions -fno-rtti \
        -fno-stack-protector -O2 -Wall -Wextra \
        -c $SRC_DIR/kernel.cpp -o $BUILD_DIR/kernel.o

    echo "[3/5] Linking kernel at 0x10000..."
    ld -m elf_i386 -Ttext 0x10000 \
       --oformat binary \
       $BUILD_DIR/kernel.o -o $BUILD_DIR/kernel.bin

    echo "[4/5] Padding kernel to 48 sectors (24576 bytes)..."
    size=$(stat -c%s $BUILD_DIR/kernel.bin)
    if [ "$size" -lt 24576 ]; then
        truncate -s 24576 $BUILD_DIR/kernel.bin
    fi

    echo "[5/5] Build complete."
}

do_run() {
    echo "Starting QEMU..."
    cd $BUILD_DIR
    qemu-system-i386 -display sdl -fda bootsect.bin -fdb kernel.bin
}

# Parse arguments
do_build_flag=false

for arg in "$@"; do
    case $arg in
        --build)
            do_build_flag=true
            shift
            ;;
        *)
            echo "Unknown argument: $arg"
            echo "Usage: $0 [--build]"
            echo "  --build  Build and run (default: run only)"
            exit 1
            ;;
    esac
done

# Execute
if [ "$do_build_flag" = true ]; then
    do_build
fi
do_run

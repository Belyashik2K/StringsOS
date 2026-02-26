set -e

BUILD_DIR=build
SRC_DIR=src

echo "[0/6] Preparing build directory..."
mkdir -p $BUILD_DIR

echo "[1/6] Assembling bootloader..."
as --32 $SRC_DIR/bootsect.asm -o $BUILD_DIR/bootsect.o
ld -Ttext 0x7c00 -m elf_i386 --oformat binary $BUILD_DIR/bootsect.o -o $BUILD_DIR/bootsect.bin

echo "[2/6] Compiling kernel (C++)..."
g++ -m32 -ffreestanding -fno-pie -fno-exceptions -fno-rtti -fno-stack-protector \
    -nostdlib -nostartfiles -nodefaultlibs \
    -fno-asynchronous-unwind-tables -fno-unwind-tables \
    -O2 -Wall -Wextra \
    -c $SRC_DIR/kernel.cpp -o $BUILD_DIR/kernel.bin

echo "[3/6] Linking kernel at 0x10000..."
ld -m elf_i386 -Ttext 0x10000 --oformat binary -e _start build/kernel.o -o build/kernel.bin

echo "[4/6] Padding kernel to 48 sectors (24576 bytes)..."
size=$(stat -c%s $BUILD_DIR/kernel.bin)
if [ "$size" -lt 24576 ]; then
    truncate -s 24576 $BUILD_DIR/kernel.bin
fi

echo "[5/6] Build complete."

echo "[6/6] Starting QEMU from build/..."
cd $BUILD_DIR
qemu-system-i386 -display sdl -fda bootsect.bin -fdb kernel.bin

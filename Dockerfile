FROM debian:bookworm-slim AS fasm

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
    && rm -rf /var/lib/apt/lists/*

RUN curl -sSL -o /tmp/fasm.tgz https://flatassembler.net/fasm-1.73.35.tgz \
    && tar -xzf /tmp/fasm.tgz -C /tmp \
    && chmod +x /tmp/fasm/fasm

FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ \
        gcc-multilib \
        g++-multilib \
        binutils \
        qemu-system-x86 \
        novnc \
        websockify \
    && rm -rf /var/lib/apt/lists/*

COPY --from=fasm /tmp/fasm/fasm /usr/local/bin/fasm

WORKDIR /app
COPY src ./src
COPY run.sh ./run.sh

RUN mkdir -p build \
    && fasm src/bootsect.asm build/bootsect.bin \
    && g++ -m32 -ffreestanding -fno-pie -fno-exceptions -fno-rtti \
       -fno-stack-protector -O2 -Wall -Wextra \
       -c src/kernel.cpp -o build/kernel.o \
    && ld -m elf_i386 -Ttext 0x10000 --oformat binary \
       build/kernel.o -o build/kernel.bin \
    && size=$(stat -c%s build/kernel.bin) \
    && if [ "$size" -lt 24576 ]; then truncate -s 24576 build/kernel.bin; fi \
    && rm build/kernel.o

COPY docker/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

EXPOSE 6080

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]

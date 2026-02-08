qemu-system-x86\_64 -cdrom build/shlakoblokunos.iso -bios RELEASEX64\_OVMF.fd -m 256M -serial stdio

cmake --preset default

cmake --build build --targt iso 
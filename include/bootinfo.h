#ifndef LIBAI_BOOTINFO_H
#define LIBAI_BOOTINFO_H

#include <stdint.h>

/*
 * Bootloader <-> Kernel contract.
 *
 * Magic "LIBA":
 *   0x4C 0x49 0x42 0x41
 */
#define LIBAI_BOOTINFO_MAGIC 0x4C494241u

typedef struct {
    uint64_t address;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
} LibaiFramebuffer;

typedef struct {
    uint64_t magic;

    uint64_t kernel_entry;

    /*
     * Physical pointer to the final UEFI memory map
     * (array of EFI_MEMORY_DESCRIPTOR).
     *
     * Descriptor stride is memory_map_descriptor_size,
     * not necessarily sizeof(EFI_MEMORY_DESCRIPTOR).
     */
    uint64_t memory_map;
    uint64_t memory_map_size;
    uint64_t memory_map_descriptor_size;
    uint32_t memory_map_descriptor_version;

    /* Reserved for later stages. M0.4 leaves these zero. */
    LibaiFramebuffer framebuffer;
} LibaiBootInfo;

#endif

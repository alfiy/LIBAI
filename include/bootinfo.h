#ifndef LIBAI_BOOTINFO_H
#define LIBAI_BOOTINFO_H

#include <stdint.h>

#define LIBAI_BOOTINFO_MAGIC 0x4C494241

typedef struct {
    uint64_t base;
    uint64_t size;
    uint32_t type;
} LibaiMemoryRegion;

typedef struct {
    uint64_t framebuffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
} LibaiFramebuffer;

typedef struct {
    uint64_t magic;

    LibaiFramebuffer framebuffer;

    uint64_t memory_map;
    uint64_t memory_map_size;
    uint64_t memory_map_descriptor_size;

} LibaiBootInfo;

#endif

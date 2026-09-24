#ifndef LIBAI_UEFI_MMAP_H
#define LIBAI_UEFI_MMAP_H

#include <stdint.h>

/*
 * UEFI memory descriptor layout used by GetMemoryMap().
 *
 * The firmware stride may be larger than this struct.
 * Always advance by BootInfo->memory_map_descriptor_size.
 */
typedef struct {
    uint32_t type;
    uint32_t pad;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} LibaiEfiMemoryDescriptor;

#define LIBAI_EFI_RESERVED               0
#define LIBAI_EFI_LOADER_CODE            1
#define LIBAI_EFI_LOADER_DATA            2
#define LIBAI_EFI_BOOT_SERVICES_CODE     3
#define LIBAI_EFI_BOOT_SERVICES_DATA     4
#define LIBAI_EFI_RUNTIME_SERVICES_CODE  5
#define LIBAI_EFI_RUNTIME_SERVICES_DATA  6
#define LIBAI_EFI_CONVENTIONAL_MEMORY    7
#define LIBAI_EFI_UNUSABLE_MEMORY        8
#define LIBAI_EFI_ACPI_RECLAIM_MEMORY    9
#define LIBAI_EFI_ACPI_MEMORY_NVS        10
#define LIBAI_EFI_MMIO                   11
#define LIBAI_EFI_MMIO_PORT_SPACE        12
#define LIBAI_EFI_PAL_CODE               13
#define LIBAI_EFI_PERSISTENT_MEMORY      14
#define LIBAI_EFI_UNACCEPTED_MEMORY      15
#define LIBAI_EFI_TYPE_COUNT             16

static inline int
libai_efi_type_usable_now(uint32_t type)
{
    switch (type) {
    case LIBAI_EFI_LOADER_CODE:
    case LIBAI_EFI_LOADER_DATA:
    case LIBAI_EFI_BOOT_SERVICES_CODE:
    case LIBAI_EFI_BOOT_SERVICES_DATA:
    case LIBAI_EFI_CONVENTIONAL_MEMORY:
        return 1;
    default:
        return 0;
    }
}

static inline const char *
libai_efi_type_name(uint32_t type)
{
    switch (type) {
    case LIBAI_EFI_RESERVED:              return "Reserved";
    case LIBAI_EFI_LOADER_CODE:           return "LoaderCode";
    case LIBAI_EFI_LOADER_DATA:           return "LoaderData";
    case LIBAI_EFI_BOOT_SERVICES_CODE:    return "BS_Code";
    case LIBAI_EFI_BOOT_SERVICES_DATA:    return "BS_Data";
    case LIBAI_EFI_RUNTIME_SERVICES_CODE: return "RT_Code";
    case LIBAI_EFI_RUNTIME_SERVICES_DATA: return "RT_Data";
    case LIBAI_EFI_CONVENTIONAL_MEMORY:   return "Conventional";
    case LIBAI_EFI_UNUSABLE_MEMORY:       return "Unusable";
    case LIBAI_EFI_ACPI_RECLAIM_MEMORY:   return "ACPIReclaim";
    case LIBAI_EFI_ACPI_MEMORY_NVS:       return "ACPINVS";
    case LIBAI_EFI_MMIO:                  return "MMIO";
    case LIBAI_EFI_MMIO_PORT_SPACE:       return "MMIOPort";
    case LIBAI_EFI_PAL_CODE:              return "PalCode";
    case LIBAI_EFI_PERSISTENT_MEMORY:     return "Persistent";
    case LIBAI_EFI_UNACCEPTED_MEMORY:     return "Unaccepted";
    default:                             return "Unknown";
    }
}

#endif

#include <efi.h>
#include <efilib.h>
#include <stddef.h>

#include <stdint.h>

/*
 * Minimal ELF64 definitions
 */

#define EI_NIDENT 16

#define ELF_MAGIC0 0x7f
#define ELF_MAGIC1 'E'
#define ELF_MAGIC2 'L'
#define ELF_MAGIC3 'F'

#define ELFCLASS64 2
#define ELFDATA2LSB 1

#define ET_EXEC 2
#define EM_X86_64 62

#define PT_LOAD 1

typedef struct {
    unsigned char e_ident[EI_NIDENT];

    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;

    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;

    uint32_t e_flags;

    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;

    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;


typedef struct {
    uint32_t p_type;
    uint32_t p_flags;

    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;

    uint64_t p_filesz;
    uint64_t p_memsz;

    uint64_t p_align;
} Elf64_Phdr;


/*
 * Print an EFI status and halt.
 */
static void
fatal(const CHAR16 *message, EFI_STATUS status)
{
    Print(L"[ERROR] ");
    Print(message);
    Print(L"\r\n");

    Print(L"        EFI_STATUS = %r\r\n", status);

    while (1) {
        __asm__ volatile ("hlt");
    }
}


/*
 * Wait forever.
 */
static void
halt(void)
{
    while (1) {
        __asm__ volatile ("hlt");
    }
}


EFI_STATUS
efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS status;

    InitializeLib(ImageHandle, SystemTable);

    Print(L"\r\n");
    Print(L"================================\r\n");
    Print(L"        Libai EFI Loader\r\n");
    Print(L"================================\r\n");
    Print(L"\r\n");

    Print(L"[M0.2] Libai EFI Loader started.\r\n");
    Print(L"\r\n");


    /*
     * ------------------------------------------------------------
     * Step 1: Get Loaded Image Protocol
     * ------------------------------------------------------------
     */

    EFI_LOADED_IMAGE *LoadedImage = NULL;

    status = uefi_call_wrapper(
        BS->HandleProtocol,
        3,
        ImageHandle,
        &LoadedImageProtocol,
        (void **)&LoadedImage
    );

    if (EFI_ERROR(status)) {
        fatal(L"HandleProtocol(LoadedImage) failed.", status);
    }

    Print(L"[M0.2] Loaded Image Protocol OK.\r\n");


    /*
     * ------------------------------------------------------------
     * Step 2: Get Simple File System Protocol
     * ------------------------------------------------------------
     */

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem = NULL;

    status = uefi_call_wrapper(
        BS->HandleProtocol,
        3,
        LoadedImage->DeviceHandle,
        &FileSystemProtocol,
        (void **)&FileSystem
    );

    if (EFI_ERROR(status)) {
        fatal(L"HandleProtocol(FileSystem) failed.", status);
    }

    Print(L"[M0.2] Simple File System Protocol OK.\r\n");


    /*
     * ------------------------------------------------------------
     * Step 3: Open ESP root directory
     * ------------------------------------------------------------
     */

    EFI_FILE_HANDLE Root = NULL;

    status = uefi_call_wrapper(
        FileSystem->OpenVolume,
        2,
        FileSystem,
        &Root
    );

    if (EFI_ERROR(status)) {
        fatal(L"OpenVolume() failed.", status);
    }

    Print(L"[M0.2] ESP root opened.\r\n");


    /*
     * ------------------------------------------------------------
     * Step 4: Open kernel ELF
     * ------------------------------------------------------------
     */

    EFI_FILE_HANDLE KernelFile = NULL;

    status = uefi_call_wrapper(
        Root->Open,
        5,
        Root,
        &KernelFile,
        L"\\libai-kernel.elf",
        EFI_FILE_MODE_READ,
        0
    );

    if (EFI_ERROR(status)) {
        fatal(L"Cannot open \\libai-kernel.elf", status);
    }

    Print(L"[M0.2] Found: \\libai-kernel.elf\r\n");


    /*
     * ------------------------------------------------------------
     * Step 5: Get file size
     * ------------------------------------------------------------
     */

    UINTN FileInfoSize = 0;

    status = uefi_call_wrapper(
        KernelFile->GetInfo,
        4,
        KernelFile,
        &gEfiFileInfoGuid,
        &FileInfoSize,
        NULL
    );

    if (status != EFI_BUFFER_TOO_SMALL) {
        fatal(L"GetInfo(size query) failed.", status);
    }

    EFI_FILE_INFO *FileInfo = NULL;

    status = uefi_call_wrapper(
        BS->AllocatePool,
        3,
        EfiLoaderData,
        FileInfoSize,
        (void **)&FileInfo
    );

    if (EFI_ERROR(status)) {
        fatal(L"AllocatePool(FileInfo) failed.", status);
    }

    status = uefi_call_wrapper(
        KernelFile->GetInfo,
        4,
        KernelFile,
        &gEfiFileInfoGuid,
        &FileInfoSize,
        FileInfo
    );

    if (EFI_ERROR(status)) {
        fatal(L"GetInfo() failed.", status);
    }

    UINTN KernelSize = (UINTN)FileInfo->FileSize;

    Print(L"[M0.2] Kernel size: %lu bytes\r\n", KernelSize);


    /*
     * ------------------------------------------------------------
     * Step 6: Allocate memory and read entire ELF
     * ------------------------------------------------------------
     */

    void *KernelBuffer = NULL;

    status = uefi_call_wrapper(
        BS->AllocatePool,
        3,
        EfiLoaderData,
        KernelSize,
        &KernelBuffer
    );

    if (EFI_ERROR(status)) {
        fatal(L"AllocatePool(KernelBuffer) failed.", status);
    }

    UINTN ReadSize = KernelSize;

    status = uefi_call_wrapper(
        KernelFile->Read,
        3,
        KernelFile,
        &ReadSize,
        KernelBuffer
    );

    if (EFI_ERROR(status)) {
        fatal(L"Read(kernel ELF) failed.", status);
    }

    if (ReadSize != KernelSize) {
        Print(L"[ERROR] Short read.\r\n");
        halt();
    }

    Print(L"[M0.2] Kernel ELF loaded into temporary buffer.\r\n");


    /*
     * ------------------------------------------------------------
     * Step 7: Validate ELF header
     * ------------------------------------------------------------
     */

    if (KernelSize < sizeof(Elf64_Ehdr)) {
        Print(L"[ERROR] File too small for ELF64 header.\r\n");
        halt();
    }

    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)KernelBuffer;

    if (ehdr->e_ident[0] != ELF_MAGIC0 ||
        ehdr->e_ident[1] != ELF_MAGIC1 ||
        ehdr->e_ident[2] != ELF_MAGIC2 ||
        ehdr->e_ident[3] != ELF_MAGIC3) {

        Print(L"[ERROR] Invalid ELF magic.\r\n");
        halt();
    }

    if (ehdr->e_ident[4] != ELFCLASS64) {
        Print(L"[ERROR] ELF is not 64-bit.\r\n");
        halt();
    }

    if (ehdr->e_ident[5] != ELFDATA2LSB) {
        Print(L"[ERROR] ELF is not little-endian.\r\n");
        halt();
    }

    if (ehdr->e_machine != EM_X86_64) {
        Print(L"[ERROR] ELF architecture is not x86-64.\r\n");
        halt();
    }

    if (ehdr->e_type != ET_EXEC) {
        Print(L"[ERROR] ELF is not an executable.\r\n");
        halt();
    }

    Print(L"\r\n");
    Print(L"[M0.2] ELF64 detected.\r\n");

    Print(L"[M0.2] Entry: 0x%lx\r\n", ehdr->e_entry);

    Print(
        L"[M0.2] Program Headers: %u\r\n",
        ehdr->e_phnum
    );


    /*
     * ------------------------------------------------------------
     * Step 8: Validate Program Header table
     * ------------------------------------------------------------
     */

    UINT64 ph_end =
        ehdr->e_phoff +
        ((UINT64)ehdr->e_phnum * ehdr->e_phentsize);

    if (ph_end > KernelSize) {
        Print(L"[ERROR] Program Header table outside ELF.\r\n");
        halt();
    }


    /*
     * ------------------------------------------------------------
     * Step 9: Parse PT_LOAD segments
     * ------------------------------------------------------------
     */

    UINTN LoadSegmentCount = 0;

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {

        Elf64_Phdr *phdr =
            (Elf64_Phdr *)(
                (uint8_t *)KernelBuffer +
                ehdr->e_phoff +
                ((UINT64)i * ehdr->e_phentsize)
            );

        if (phdr->p_type != PT_LOAD) {
            continue;
        }

        Print(L"\r\n");
        Print(L"[M0.3.1] Loading PT_LOAD #%u\r\n", i);

        Print(L"        Offset : 0x%lx\r\n", phdr->p_offset);
        Print(L"        VAddr  : 0x%lx\r\n", phdr->p_vaddr);
        Print(L"        PAddr  : 0x%lx\r\n", phdr->p_paddr);
        Print(L"        FileSz : 0x%lx\r\n", phdr->p_filesz);
        Print(L"        MemSz  : 0x%lx\r\n", phdr->p_memsz);
        Print(L"        Flags  : 0x%x\r\n", phdr->p_flags);
        Print(L"        Align  : 0x%lx\r\n", phdr->p_align);


        /*
        * --------------------------------------------------------
        * Validation
        * --------------------------------------------------------
        */

        if (phdr->p_filesz > phdr->p_memsz) {
            Print(L"[ERROR] FileSz > MemSz.\r\n");
            halt();
        }

        if (phdr->p_offset > KernelSize) {
            Print(L"[ERROR] PT_LOAD offset outside ELF.\r\n");
            halt();
        }

        if (phdr->p_filesz > KernelSize - phdr->p_offset) {
            Print(L"[ERROR] PT_LOAD file range outside ELF.\r\n");
            halt();
        }

        if (phdr->p_vaddr > UINT64_MAX - phdr->p_memsz) {
            Print(L"[ERROR] PT_LOAD memory range overflow.\r\n");
            halt();
        }


        /*
        * --------------------------------------------------------
        * Calculate page-aligned destination
        * --------------------------------------------------------
        */

        UINT64 PageSize = 0x1000;

        UINT64 SegmentStart = phdr->p_vaddr;
        UINT64 SegmentEnd = phdr->p_vaddr + phdr->p_memsz;

        UINT64 PageStart =
            SegmentStart & ~(PageSize - 1);

        UINT64 PageEnd =
            (SegmentEnd + PageSize - 1) &
            ~(PageSize - 1);

        UINT64 PageCount =
            (PageEnd - PageStart) / PageSize;


        if (PageCount == 0) {
            Print(L"[ERROR] PT_LOAD requires zero pages.\r\n");
            halt();
        }

        Print(L"        PageStart: 0x%lx\r\n", PageStart);
        Print(L"        PageEnd  : 0x%lx\r\n", PageEnd);
        Print(L"        Pages    : %lu\r\n", PageCount);


        /*
        * --------------------------------------------------------
        * Allocate physical pages.
        *
        * For M0.3.1 we deliberately request the exact address
        * described by p_paddr.
        * --------------------------------------------------------
        */

        EFI_PHYSICAL_ADDRESS LoadAddress =
            (EFI_PHYSICAL_ADDRESS)PageStart;

        status = uefi_call_wrapper(
            BS->AllocatePages,
            4,
            AllocateAddress,
            EfiLoaderData,
            PageCount,
            &LoadAddress
        );

        if (EFI_ERROR(status)) {
            Print(L"[ERROR] AllocatePages() failed.\r\n");
            Print(L"        EFI_STATUS = %r\r\n", status);
            halt();
        }

        Print(L"        Allocated at: 0x%lx\r\n",
            (UINT64)LoadAddress);


        /*
        * --------------------------------------------------------
        * Zero the entire memory image first.
        *
        * This guarantees that the area corresponding to:
        *
        *     p_memsz - p_filesz
        *
        * is zero.
        * --------------------------------------------------------
        */

        SetMem(
            (void *)(UINTN)PageStart,
            (UINTN)(PageEnd - PageStart),
            0
        );


        /*
        * --------------------------------------------------------
        * Copy file-backed portion.
        *
        * Destination:
        *
        *     p_vaddr
        *
        * Source:
        *
        *     KernelBuffer + p_offset
        * --------------------------------------------------------
        */

        if (phdr->p_filesz > 0) {

            CopyMem(
                (void *)(UINTN)phdr->p_vaddr,
                (uint8_t *)KernelBuffer + phdr->p_offset,
                (UINTN)phdr->p_filesz
            );
        }

        Print(L"        Copied : %lu bytes\r\n",
            phdr->p_filesz);

        Print(L"        Zeroed  : %lu bytes\r\n",
            phdr->p_memsz - phdr->p_filesz);


        /*
        * --------------------------------------------------------
        * Verify the first bytes of the loaded segment.
        *
        * This is temporary diagnostic code.
        * --------------------------------------------------------
        */

        if (phdr->p_filesz >= 4) {

            uint8_t *Loaded =
                (uint8_t *)(UINTN)phdr->p_vaddr;

            Print(
                L"        Memory  : %02x %02x %02x %02x\r\n",
                Loaded[0],
                Loaded[1],
                Loaded[2],
                Loaded[3]
            );
        }

        Print(L"[M0.3.1] PT_LOAD loaded successfully.\r\n");

        LoadSegmentCount++;
    }


    if (LoadSegmentCount == 0) {
        Print(L"[ERROR] No PT_LOAD segment found.\r\n");
        halt();
    }

    Print(L"\r\n");
    Print(L"[M0.3.2] Verifying loaded kernel image...\r\n");

    UINTN VerifySegmentCount = 0;

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr *phdr =
            (Elf64_Phdr *)(
                (uint8_t *)KernelBuffer +
                ehdr->e_phoff +
                ((UINT64)i * ehdr->e_phentsize)
            );

        if (phdr->p_type != PT_LOAD) {
            continue;
        }

        Print(L"\r\n");
        Print(L"[M0.3.2] Verifying PT_LOAD #%u\r\n", i);

        /*
        * ------------------------------------------------------------
        * 1. Verify file-backed portion
        * ------------------------------------------------------------
        */
        if (phdr->p_filesz > 0) {
            uint8_t *Loaded =
                (uint8_t *)(UINTN)phdr->p_vaddr;

            uint8_t *Expected =
                (uint8_t *)KernelBuffer + phdr->p_offset;

            for (UINT64 j = 0; j < phdr->p_filesz; j++) {
                if (Loaded[j] != Expected[j]) {
                    Print(L"[ERROR] Loaded image mismatch.\r\n");
                    Print(L"        Offset = 0x%lx\r\n", j);
                    Print(L"        Expected = 0x%02x\r\n", Expected[j]);
                    Print(L"        Actual   = 0x%02x\r\n", Loaded[j]);
                    halt();
                }
            }

            Print(
                L"        File-backed data verified: %lu bytes\r\n",
                phdr->p_filesz
            );
        } else {
            Print(L"        File-backed data: 0 bytes\r\n");
        }

        /*
        * ------------------------------------------------------------
        * 2. Verify zero-filled portion
        * ------------------------------------------------------------
        */
        UINT64 ZeroSize = phdr->p_memsz - phdr->p_filesz;

        if (ZeroSize > 0) {
            uint8_t *ZeroArea =
                (uint8_t *)(UINTN)(
                    phdr->p_vaddr + phdr->p_filesz
                );

            for (UINT64 j = 0; j < ZeroSize; j++) {
                if (ZeroArea[j] != 0) {
                    Print(L"[ERROR] Zero-filled area is not zero.\r\n");
                    Print(L"        Offset = 0x%lx\r\n", j);
                    Print(L"        Actual  = 0x%02x\r\n", ZeroArea[j]);
                    halt();
                }
            }

            Print(
                L"        Zero-filled data verified: %lu bytes\r\n",
                ZeroSize
            );
        } else {
            Print(L"        Zero-filled data: 0 bytes\r\n");
        }

        Print(
            L"[M0.3.2] PT_LOAD #%u verification OK.\r\n",
            i
        );

        VerifySegmentCount++;
    }

    if (VerifySegmentCount == 0) {
        Print(L"[ERROR] No PT_LOAD segment verified.\r\n");
        halt();
    }

    Print(L"\r\n");
    Print(L"[M0.3.2] Kernel image verification successful.\r\n");
    Print(
        L"[M0.3.2] Verified PT_LOAD segments: %lu\r\n",
        VerifySegmentCount
    );

    Print(L"\r\n");
    Print(L"[M0.3.2] Kernel is still NOT started.\r\n");

    halt();

    return EFI_SUCCESS;
}


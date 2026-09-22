#include <efi.h>
#include <efilib.h>

EFI_STATUS
efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    InitializeLib(ImageHandle, SystemTable);

    Print(L"\r\n");
    Print(L"================================\r\n");
    Print(L"        Libai UEFI Boot\r\n");
    Print(L"================================\r\n");
    Print(L"\r\n");
    Print(L"M0.1 boot successful.\r\n");
    Print(L"Hello from Libai!\r\n");
    Print(L"\r\n");

    while (1) {
        __asm__ volatile ("hlt");
    }

    return EFI_SUCCESS;
}

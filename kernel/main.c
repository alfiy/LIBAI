#include <stdint.h>

volatile uint8_t libai_bss_buffer[4096];

void libai_kernel_entry(void)
{
	libai_bss_buffer[0] = 0x42;

	for(;;){
		__asm__ volatile ("hlt");
	}
}

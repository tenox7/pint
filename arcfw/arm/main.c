void uart_init(void);
void uart_puts(const char *s);
void BlStartup(char *PartitionName);

void cmain(void)
{
    uart_init();
    uart_puts("\n=== NT 3.5 ARC Firmware Emulator  (ARM32 / Raspberry Pi 2) ===\n");
    BlStartup("multi(0)disk(0)rdisk(0)partition(1)");
}

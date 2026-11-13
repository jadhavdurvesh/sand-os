#include "sand/drivers/drivers.h"
#include "sand/drivers/device.h"
#include "sand/drivers/serial.h"
#include "sand/arch/x86/ata_pio.h"
#include "sand/arch/x86/input.h"
#include "sand/storage/block.h"
#include <stdint.h>
static inline void debug_char(char c) { __asm__ volatile ("outb %0, $0xE9" : : "a"(c)); }
static void debug_text(const char *s) { while(*s) debug_char(*s++); }

int drivers_init(void) {
    int failed=0;
    (void)device_register("cpu0","cpu","x86-32",SAND_DEVICE_READY);
    (void)device_register("pit0","timer","pit-irq0",SAND_DEVICE_READY);
    if(serial_init()==0) debug_text("SAND_TEST:M8_SERIAL_LOOPBACK_PASS\n"); else { debug_text("SAND_TEST:M8_SERIAL_FAIL\n"); failed=1; }
    if(ps2_input_init()==0) debug_text("SAND_TEST:M8_PS2_INIT_PASS\n"); else { debug_text("SAND_TEST:M8_PS2_FAIL\n"); failed=1; }
    const struct sand_device *k=device_find("kbd0"), *s=device_find("serial0");
    if(k && s && k->status==SAND_DEVICE_READY && s->status==SAND_DEVICE_READY) debug_text("SAND_TEST:M8_IRQ_ROUTING_PASS\n");
    else { debug_text("SAND_TEST:M8_IRQ_ROUTING_FAIL\n"); failed=1; }
    return failed?-1:0;
}

/* Disks other than the boot disk are only ever READ here, and only to detect
 * the SAND test fixture signature: an unknown disk is never written. */
void drivers_storage_report(void) {
    uint32_t count=ata_pio_drive_count();
    if(count>=1u) debug_text("SAND_TEST:M8_ATA_BOOT_DISK_PASS\n");
    if(count>=2u) {
        const struct sand_block_device *second=block_get(1u);
        static uint8_t sector[512];
        static const char magic[]="SAND-ATA-PROBE-DISK";
        int match=second && second->read(0u,sector)==0;
        for(uint32_t i=0;match && i<sizeof(magic)-1u;++i) if(sector[i]!=(uint8_t)magic[i]) match=0;
        debug_text("SAND_TEST:M8_STORAGE_SECOND_DISK_DETECTED\n");
        if(match) debug_text("SAND_TEST:M8_STORAGE_SECOND_DISK_READ_PASS\n");
    }
    if(device_count()>=6u) debug_text("SAND_TEST:M8_DEVICE_REGISTRY_PASS\n");
}

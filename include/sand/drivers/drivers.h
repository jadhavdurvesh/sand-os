#ifndef SAND_DRIVERS_DRIVERS_H
#define SAND_DRIVERS_DRIVERS_H
/* M8 driver bring-up: serial console, PS/2 keyboard + mouse, interrupt routing,
 * device registry. Emits SAND_TEST markers on the debug port. Call after
 * paging/heap and the IDT are up, before interrupts are enabled for good. */
int drivers_init(void);
/* Read-only probe of every disk beyond the boot disk; emits storage markers. */
void drivers_storage_report(void);
#endif

#ifndef SAND_AUDIO_HDA_H
#define SAND_AUDIO_HDA_H
#include <stdint.h>
/* Minimal Intel High Definition Audio output driver: controller found on PCI bus 0 (class 04:03), registers mapped in the
 * kernel MMIO window, codec driven through the immediate-command interface, one output stream, polled (no interrupts). */
int hda_init(void);                                                     /* 0 when a controller and a working output path were found */
int hda_play(uint32_t rate, uint8_t *ring, uint32_t period_bytes, uint32_t periods);   /* 16-bit stereo, cyclic; `ring` must be identity-mapped */
uint32_t hda_position(void);                                            /* byte offset the DMA engine has reached inside the ring */
void hda_stop(void);
#endif

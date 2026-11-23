#ifndef SAND_AUDIO_GJALLAR_H
#define SAND_AUDIO_GJALLAR_H
#include <stdint.h>
/* GJALLAR: SAND's boot score. The score ("Gjallarhorn") is stored as 4-bit IMA ADPCM in a region right after the MIMIR
 * store (written by tools/gjallar_pack.py), decoded block by block by the kernel and streamed to an Intel HD Audio
 * controller (kernel/audio/hda.c). The boot splash is paced by the music: the reveal lands on the score's first big hit. */
#define GJLR_BLOCK_FRAMES 2552u
#define GJLR_BLOCK_BYTES 2560u
/* Pure codec (host-testable): decodes one block into GJLR_BLOCK_FRAMES interleaved stereo frames. */
void gjallar_decode_block(const uint8_t *block, int16_t *out);

#ifndef GJALLAR_HOST
struct sand_block_device;
int gjallar_begin(const struct sand_block_device *disk, uint32_t region_lba);   /* 0: playing. Non-zero: no score, no controller or bad data (not an error) */
void gjallar_service(void);                 /* refill the ring; call at least every ~200 ms while playing */
int gjallar_playing(void);
uint32_t gjallar_ms(void);                  /* how much of the score has been played */
uint32_t gjallar_cue_ms(void);              /* the first big hit, from the score header */
void gjallar_fade_out(void);                /* ~0.5 s fade, then silence (idempotent) */
#endif
#endif

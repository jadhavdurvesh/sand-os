#include "sand/audio/gjallar.h"
#include "sand/audio/hda.h"
#include "sand/storage/block.h"
#define PERIOD_BYTES 4096u
#define PERIODS 64u                                   /* 64 x 4 KiB: about 1.5 s of audio kept ahead, so slow stretches of boot do not stutter */
#define RING_BYTES (PERIOD_BYTES * PERIODS)
#define VOLUME 200u                                   /* /256: headroom for the score's loud sections */
#define FADE_AHEAD (3u * PERIOD_BYTES)                /* the fade starts this far past the DMA position, so the engine cannot overtake the edit */
#define FADE_BYTES (16u * PERIOD_BYTES)               /* about 0.37 s */

struct header { char magic[4]; uint32_t version, rate, channels, total_frames, block_frames, block_bytes, block_count, cue_ms; };
static uint8_t ring[RING_BYTES] __attribute__((aligned(4096)));
static uint8_t raw[GJLR_BLOCK_BYTES];
static int16_t pcm[GJLR_BLOCK_FRAMES * 2u];
static const struct sand_block_device *disk;
static struct header head;
static uint32_t region, next_block, pcm_pos, pcm_len;       /* pcm_pos / pcm_len in frames */
static uint32_t filled, played, last_pos, wraps;            /* bytes: written into the ring / consumed by the DMA engine */
static uint32_t ended_at, fade_requested;                   /* ended_at: byte offset after which the ring holds only silence (0: not yet) */
static int playing;

static int load_block(uint32_t b) {
    uint32_t lba = region + 1u + b * (GJLR_BLOCK_BYTES / 512u);
    for (uint32_t s = 0; s < GJLR_BLOCK_BYTES / 512u; ++s) if (disk->read(lba + s, raw + s * 512u) != 0) return -1;
    gjallar_decode_block(raw, pcm); pcm_pos = 0; pcm_len = GJLR_BLOCK_FRAMES; return 0;
}
/* Write one period (1024 stereo frames) into the ring: the next decoded audio, or silence once the score (or a fade) has ended. */
static void fill_period(void) {
    int16_t *dst = (int16_t *)(ring + (filled % RING_BYTES));
    for (uint32_t f = 0; f < PERIOD_BYTES / 4u; ++f) {
        int32_t l = 0, r = 0;
        if (ended_at == 0u) {
            if (pcm_pos >= pcm_len && (next_block >= head.block_count || load_block(next_block++) != 0)) ended_at = filled + f * 4u;
            else { l = pcm[pcm_pos * 2u]; r = pcm[pcm_pos * 2u + 1u]; ++pcm_pos; }
        }
        dst[f * 2u] = (int16_t)((l * (int32_t)VOLUME) / 256); dst[f * 2u + 1u] = (int16_t)((r * (int32_t)VOLUME) / 256);
    }
    filled += PERIOD_BYTES;
}
/* Fade what is already in the ring: ramp the next FADE_BYTES down, silence everything after. */
static void fade_in_place(void) {
    uint32_t start = (played / PERIOD_BYTES) * PERIOD_BYTES + FADE_AHEAD;
    if (start >= filled) { ended_at = ended_at ? ended_at : start; return; }
    for (uint32_t x = start; x < filled; x += 4u) {
        int16_t *s = (int16_t *)(ring + (x % RING_BYTES));
        uint32_t off = x - start;
        int32_t g = off >= FADE_BYTES ? 0 : (int32_t)(256u - (off * 256u) / FADE_BYTES);
        s[0] = (int16_t)((s[0] * g) / 256); s[1] = (int16_t)((s[1] * g) / 256);
    }
    if (ended_at == 0u || ended_at > start + FADE_BYTES) ended_at = start + FADE_BYTES;
}
int gjallar_begin(const struct sand_block_device *d, uint32_t region_lba) {
    static uint8_t sector[512];
    if (d == 0 || hda_init() != 0) return -1;
    if (d->read(region_lba, sector) != 0) return -2;
    const struct header *h = (const struct header *)sector;
    if (h->magic[0] != 'G' || h->magic[1] != 'J' || h->magic[2] != 'L' || h->magic[3] != 'R' || h->version != 1u || h->rate != 44100u ||
        h->channels != 2u || h->block_frames != GJLR_BLOCK_FRAMES || h->block_bytes != GJLR_BLOCK_BYTES || h->block_count == 0u) return -3;
    head = *h; disk = d; region = region_lba; next_block = 0u; pcm_pos = pcm_len = 0u;
    filled = played = last_pos = wraps = ended_at = fade_requested = 0u;
    while (filled + PERIOD_BYTES <= RING_BYTES - PERIOD_BYTES) fill_period();                  /* prefill, keeping one period of guard */
    if (hda_play(head.rate, ring, PERIOD_BYTES, PERIODS) != 0) return -4;
    playing = 1; return 0;
}
void gjallar_service(void) {
    if (!playing) return;
    uint32_t pos = hda_position() % RING_BYTES;
    if (pos < last_pos) ++wraps;
    last_pos = pos; played = wraps * RING_BYTES + pos;
    if (fade_requested) { fade_requested = 0u; fade_in_place(); }
    while (filled + PERIOD_BYTES <= played + RING_BYTES - PERIOD_BYTES) fill_period();
    if (ended_at != 0u && played > ended_at + PERIOD_BYTES) { hda_stop(); playing = 0; }        /* the last real sample has been played */
}
int gjallar_playing(void) { return playing; }
uint32_t gjallar_ms(void) { return (played / 4u) * 10u / 441u; }                                 /* 44100 frames per second */
uint32_t gjallar_cue_ms(void) { return head.cue_ms; }
void gjallar_fade_out(void) { if (playing) fade_requested = 1u; }

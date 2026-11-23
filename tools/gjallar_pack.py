#!/usr/bin/env python3
"""GJALLAR packer: turns a score (any ffmpeg-readable file) into SAND's boot-score region.

Region layout (512-byte sectors, placed right after the MIMIR store):
  sector 0   header: "GJLR" u32 version u32 rate u32 channels u32 total_frames u32 block_frames u32 block_bytes
             u32 block_count u32 cue_ms u32 reserved[3] char title[32] u8 sha256(data)[32]
  sector 1.. blocks of 2560 bytes (5 sectors): per channel a 4-byte header (s16 predictor, u8 step index, u8 0),
             then 2552 frames as 4-bit IMA ADPCM nibbles in groups of 4 bytes (8 samples) per channel, left then right.
The ADPCM codec is the classic IMA step/index table (implemented here and in kernel/audio/gjallar.c). cue_ms is the
first big musical hit (found automatically), used to time the boot splash's reveal.
Usage: gjallar_pack.py in.mp3 out.bin [--title NAME] [--reference ref.pcm]"""
import hashlib
import struct
import subprocess
import sys

import numpy as np

STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
        157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552,
        1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
        12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]
INDEX = [-1, -1, -1, -1, 2, 4, 6, 8]
BLOCK_FRAMES = 2552
BLOCK_BYTES = 8 + BLOCK_FRAMES
RATE = 44100


def encode_channel(samples, pred, index):
    """Returns (nibbles list, final pred, final index). Matches the decoder exactly (same reconstruction)."""
    out = []
    for s in samples:
        step = STEP[index]
        diff = int(s) - pred
        code = 0
        if diff < 0:
            code = 8
            diff = -diff
        delta = step >> 3
        if diff >= step:
            code |= 4; diff -= step; delta += step
        step2 = step >> 1
        if diff >= step2:
            code |= 2; diff -= step2; delta += step2
        step4 = step >> 2
        if diff >= step4:
            code |= 1; delta += step4
        pred += -delta if code & 8 else delta
        pred = max(-32768, min(32767, pred))
        index = max(0, min(88, index + INDEX[code & 7]))
        out.append(code)
    return out, pred, index


def decode_block(block):
    """Reference decoder for one block -> int16 array (frames, 2)."""
    res = np.zeros((BLOCK_FRAMES, 2), dtype=np.int16)
    for ch in range(2):
        pred, index = struct.unpack_from("<hB", block, ch * 4)
        pos = 8
        n = 0
        while n < BLOCK_FRAMES:
            for b in range(4):
                byte = block[pos + ch * 4 + b]
                for nib in (byte & 15, byte >> 4):
                    step = STEP[index]
                    delta = step >> 3
                    if nib & 4: delta += step
                    if nib & 2: delta += step >> 1
                    if nib & 1: delta += step >> 2
                    pred += -delta if nib & 8 else delta
                    pred = max(-32768, min(32767, pred))
                    index = max(0, min(88, index + INDEX[nib & 7]))
                    res[n, ch] = pred
                    n += 1
            pos += 8
    return res


def find_cue_ms(pcm):
    """First big hit: 50 ms RMS above 0.2 and 1.8x the mean of the preceding 2 s (the drop of the build-up)."""
    mono = pcm.astype(np.float32).mean(axis=1) / 32768.0
    win = RATE // 20
    n = len(mono) // win
    rms = np.sqrt((mono[:n * win].reshape(n, win) ** 2).mean(axis=1))
    for i in range(60, n):
        base = rms[i - 40:i - 2].mean()
        if rms[i] > 0.2 and rms[i] > 1.8 * base:
            return i * 50 - 50
    return 0


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        sys.exit(__doc__)
    src, dst = args[0], args[1]
    title = "Gjallarhorn"
    ref = None
    if "--title" in args: title = args[args.index("--title") + 1]
    if "--reference" in args: ref = args[args.index("--reference") + 1]
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", src, "-f", "s16le", "-ac", "2", "-ar", str(RATE), "-"], capture_output=True, check=True).stdout
    pcm = np.frombuffer(raw, dtype="<i2").reshape(-1, 2)
    total = len(pcm)
    blocks = (total + BLOCK_FRAMES - 1) // BLOCK_FRAMES
    pad = np.zeros((blocks * BLOCK_FRAMES, 2), dtype=np.int16)
    pad[:total] = pcm
    data = bytearray()
    state = [(0, 0), (0, 0)]
    for b in range(blocks):
        seg = pad[b * BLOCK_FRAMES:(b + 1) * BLOCK_FRAMES]
        head = bytearray(); chans = []
        for ch in range(2):
            pred, index = state[ch]
            head += struct.pack("<hBB", pred, index, 0)
            nib, pred, index = encode_channel(seg[:, ch], pred, index)
            state[ch] = (pred, index)
            chans.append(nib)
        body = bytearray()
        for g in range(BLOCK_FRAMES // 8):
            for ch in range(2):
                n = chans[ch][g * 8:(g + 1) * 8]
                body += bytes(n[i] | (n[i + 1] << 4) for i in range(0, 8, 2))
        assert len(head) + len(body) == BLOCK_BYTES
        data += head + body
    digest = hashlib.sha256(data).digest()
    cue = find_cue_ms(pcm)
    header = struct.pack("<4sIIIIIIII3I32s32s", b"GJLR", 1, RATE, 2, total, BLOCK_FRAMES, BLOCK_BYTES, blocks, cue, 0, 0, 0,
                         title.encode()[:31], digest)
    header = header.ljust(512, b"\0")
    open(dst, "wb").write(header + bytes(data))
    if ref:   # what the kernel decoder must reproduce, bit for bit
        out = np.concatenate([decode_block(bytes(data[b * BLOCK_BYTES:(b + 1) * BLOCK_BYTES])) for b in range(blocks)])
        out.astype("<i2").tofile(ref)
    print(f"{title}: {total / RATE:.2f}s, {blocks} blocks, {len(data) + 512} bytes, cue {cue} ms")


if __name__ == "__main__":
    main()

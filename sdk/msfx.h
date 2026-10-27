// MONOCORE-1 "msfx": the sound effects of MONOSFX (jump, coin, explosion...), for the games.
//
// An effect is a short sound on one PSG channel, computed every video frame (60 a second), in the
// manner of sfxr: a wave (square, triangle, saw, noise), a pitch that glides (slide, acceleration),
// a vibrato, an arpeggio (a jump of some semitones at a given frame), a volume envelope (attack,
// sustain, decay), a duty cycle that sweeps, and a repeat. A bank (file .mfx, or a slot of the
// MONOSFX NVRAM) holds up to 32 named effects.
//
// In a game (library = mc, modules = msfx; data = sounds.mfx):
//     static ms_bank_t sfx;
//     ms_open(&sfx, sounds_file, sounds_size);
//     ms_play(&sfx, ms_find(&sfx, "SAUT"));        // on a free channel of ms_channels (6-7)
//     ... and every frame: ms_tick() (mj_frame() of the mjeu module calls it)
// Format: MSFX.md. The same file is read and written in Python by tools/msfx/msbank.py.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define MS_MAGIC   0x3158464Du        // "MFX1"
#define MS_EFFECTS 32

typedef struct {
  uint32_t magic, size;
  uint8_t  count, version;            // effects in the bank; version 1
  uint16_t reserved;
  char     name[16];
  uint8_t  reserved2[4];
} ms_header_t;                        // 32 bytes, then count effects of 32 bytes

typedef struct {
  char    name[8];
  uint8_t wave;                       // PSG_SQUARE, PSG_TRIANGLE, PSG_SAW, PSG_NOISE
  uint8_t volume;                     // 0-255
  uint8_t attack, sustain, decay;     // frames: rising, full, falling (the length is their sum)
  uint8_t duty;                       // duty cycle of the square (0-255, 128 = half)
  int8_t  duty_sweep;                 // added every frame
  uint8_t repeat;                     // the pitch starts again every n frames (0: never)
  uint16_t freq;                      // Hz at the start
  int16_t slide;                      // 1/16 Hz added every frame
  int16_t accel;                      // 1/16 Hz added to the slide every frame
  uint8_t vib_depth;                  // vibrato: Hz
  uint8_t vib_speed;                  // vibrato: frames a period (0: none)
  int8_t  arp;                        // arpeggio: semitones (-24 to 24)
  uint8_t arp_time;                   // frame of the jump (0: none)
  uint8_t flags;
  uint8_t reserved[5];
} ms_effect_t;                        // 32 bytes

typedef struct {
  const ms_header_t *h;
  const ms_effect_t *fx;
} ms_bank_t;

int  ms_open(ms_bank_t *b, const void *data, uint32_t max);   // 0, or -1 if invalid
int  ms_check(const void *data, uint32_t max);
int  ms_count(const ms_bank_t *b);
int  ms_find(const ms_bank_t *b, const char *name);            // its number, or -1
const ms_effect_t *ms_effect(const ms_bank_t *b, int n);       // NULL if out of range

void ms_channels(int first, int count);   // PSG channels the effects may take (default 6 and 7)
int  ms_play(const ms_bank_t *b, int n);  // on a free channel (else the oldest); returns it, or -1
void ms_play_on(const ms_effect_t *e, int ch);   // on that channel
void ms_stop(int ch);
void ms_stop_all(void);
bool ms_playing(int ch);
void ms_tick(void);                       // every frame: the next step of every effect

// the sound, frame by frame, without the hardware (the editor draws it; the Python twin renders it)
typedef struct { const ms_effect_t *e; int t; int32_t f, slide; int duty; } ms_voice_t;
void ms_voice_start(ms_voice_t *v, const ms_effect_t *e);
// the next frame: frequency (1/16 Hz), volume and duty; false when the effect is over
bool ms_voice_step(ms_voice_t *v, uint32_t *freq16, int *vol, int *duty);
int  ms_length(const ms_effect_t *e);     // frames

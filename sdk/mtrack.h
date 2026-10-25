// MONOCORE-1 "mtrack": the music player of MONOTRACKER, as a library for games.
//
// Plays the songs made with the MONOTRACKER ROM (examples/tracker) on the 16 sound channels of
// the console: tracks 0-7 drive the PSG channels 0-7, tracks 8-15 the PCM voices 0-7. The
// sound hardware has no envelope, vibrato or slide: all of that is computed here, once per tick,
// from a timer interrupt (50 Hz at 125 BPM, as in the classic trackers).
//
// In a game (library = mc, modules = mtrack in project.txt):
//     data = ../tracker/mtbank.bin mysong.mts      // PCM sample bank and the exported song
//     mt_init(mtbank_file);
//     mt_play(mysong_file, 0);                     // starts the song (timer 1 by default)
//     mt_sfx(MT_PSG(7), 60, 3, 64);                // a sound effect on track 7, instrument 3
// Song files come from the NVRAM save of the tracker: tools/mtrack/sav2song.py. Format: MTRACK.md.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define MT_TRACKS      16          // 0-7 PSG, 8-15 PCM
#define MT_ROWS        64          // rows per pattern
#define MT_INSTRUMENTS 32          // instrument 0 = none, 1-31 usable
#define MT_ORDERS      128
#define MT_PSG(c)      (c)         // track of PSG channel c
#define MT_PCM(v)      (8 + (v))   // track of PCM voice v
#define MT_TIMER_DEFAULT 1

// ---------------------------------------------------------------- pattern cells (32 bits)
// note: 0 none, 1-96 = C-0 to B-7, 97 = note off; inst 0-31; vol 0 none, 1-65 = volume 0-64;
// fx 0 none, 1-16 = effect 0-F; param 0-255.
#define MT_NOTE_OFF 97
#define MT_CELL(note, inst, vol, fx, param) \
  ((uint32_t)(note) | ((uint32_t)(inst) << 7) | ((uint32_t)(vol) << 12) | ((uint32_t)(fx) << 19) | ((uint32_t)(param) << 24))
#define MT_C_NOTE(c)  ((c) & 0x7F)
#define MT_C_INST(c)  (((c) >> 7) & 0x1F)
#define MT_C_VOL(c)   (((c) >> 12) & 0x7F)
#define MT_C_FX(c)    (((c) >> 19) & 0x1F)
#define MT_C_PARAM(c) ((c) >> 24)

// ---------------------------------------------------------------- instruments (32 bytes)
enum { MT_TYPE_PSG = 0, MT_TYPE_PCM = 1 };
typedef struct {
  char    name[10];
  uint8_t type;          // MT_TYPE_PSG or MT_TYPE_PCM
  uint8_t wave;          // PSG: PSG_SQUARE...PSG_NOISE; PCM: sample number in the bank
  uint8_t duty;          // PSG: square duty (0-255, 128 = 50 %); PCM: 0 = loop of the bank, 1 = no loop
  int8_t  duty_sweep;    // PSG: added to the duty at each tick
  uint8_t volume;        // 0-64
  uint8_t pan;           // 0 = left, 128 = centre, 255 = right
  uint8_t attack;        // ticks to full volume (0 = at once)
  uint8_t decay;         // ticks from full volume to the sustain level
  uint8_t sustain;       // 0-64
  uint8_t release;       // ticks to silence after a note off (0 = at once)
  uint8_t vib_speed;     // vibrato: steps of 1/64 turn per tick
  uint8_t vib_depth;     // in 1/16 semitone
  uint8_t vib_delay;     // ticks before the vibrato starts
  int8_t  arp1, arp2;    // arpeggio: semitones added to the note (0, arp1, arp2, 0...)
  uint8_t arp_speed;     // ticks per arpeggio step, 0 = no arpeggio
  int8_t  pitch_sweep;   // 1/16 semitone per tick (drums: strongly negative)
  int8_t  finetune;      // 1/64 semitone
  int8_t  transpose;     // semitones
  uint8_t flags;         // bit 0: the duty sweep bounces (else it wraps); bit 1: fixed pitch (C-4)
  uint8_t reserved[2];
} mt_instrument_t;
#define MT_IF_BOUNCE 1
#define MT_IF_FIXED  2

// ---------------------------------------------------------------- sample bank (mtbank.bin)
typedef struct {
  uint32_t offset;       // from the start of the bank
  uint32_t length;       // samples (16-bit signed)
  uint32_t loop;         // loop start, 0xFFFFFFFF = none
  uint32_t rate;         // Hz, at the root note
  uint8_t  root;         // note number (1-96) at which the sample plays at its rate
  uint8_t  format;       // 1 = signed 16 bits
  uint8_t  pad[2];
  char     name[12];
} mt_sample_t;
int mt_bank_count(void);
const mt_sample_t *mt_bank_sample(int n);   // NULL if out of range

// ---------------------------------------------------------------- source of a song
// The player reads a song through this structure: compact songs (files .mts, NVRAM) are decoded
// by the library; the tracker gives its own patterns being edited.
typedef struct {
  const mt_instrument_t *ins;             // MT_INSTRUMENTS entries
  const uint8_t *orders;                  // pattern of each position
  int n_orders, restart;                  // restart: position played after the last one
  const uint32_t *(*pattern)(void *ctx, int pat);   // MT_ROWS x MT_TRACKS cells, row by row
  void *ctx;
  int speed, bpm, master;                 // ticks per row, beats per minute, master volume 0-64
} mt_source_t;

// ---------------------------------------------------------------- player
void mt_init(const void *bank);           // the bank (mtbank_file), once
int  mt_play(const void *song, int order); // a compact song (.mts); returns 0, or -1 if invalid
void mt_play_source(const mt_source_t *src, int order, int row, bool loop_pattern);
void mt_stop(void);                       // stops the song and silences every track
void mt_pause(bool on);
bool mt_playing(void);
void mt_set_timer(int timer);             // 0 or 1 (MT_TIMER_DEFAULT), before mt_play
void mt_tick(void);                       // one tick (called by the timer; or by the program
                                          // itself, at mt_tick_hz(), if it uses no timer)
uint32_t mt_tick_hz(void);
void mt_set_tempo(int speed, int bpm);
int  mt_order(void);                      // position being played
int  mt_row(void);
void mt_master(int vol);                  // 0-64

// tracks
void mt_mute(int track, bool on);         // the song no longer plays on this track
bool mt_muted(int track);
void mt_reserve(int track, bool on);      // the song leaves this track to mt_sfx / mt_note
int  mt_level(int track);                 // current output volume 0-255 (VU meters)
int  mt_track_type(int track);            // MT_TYPE_PSG or MT_TYPE_PCM

// notes outside the song: sound effects, keyboard of the tracker
void mt_note(int track, int note, const mt_instrument_t *ins, int vol);   // note 1-96, vol 0-64
void mt_note_off(int track);
void mt_sfx(int track, int note, int inst, int vol);   // instrument of the song being played

// compact songs: helpers for the tracker and the tools
#define MT_MAGIC 0x3153544Du                // "MTS1"
typedef struct {
  uint32_t magic, size;                     // size of the whole song in bytes
  uint8_t  n_orders, n_patterns, speed, bpm;
  uint8_t  restart, master, n_ins, flags;
  uint8_t  reserved[4];
  char     name[16];
} mt_header_t;                              // 36 bytes, then: instruments (n_ins x 32 bytes),
                                            // orders, pattern offsets (u16), patterns
const char *mt_song_name(const void *song);
int  mt_song_check(const void *song, uint32_t max_size);   // 0 if valid
int  mt_song_instruments(const void *song, mt_instrument_t *out);   // copies them (MT_INSTRUMENTS)
int  mt_song_decode(const void *song, int pat, uint32_t *cells);    // MT_ROWS x MT_TRACKS
// encodes one pattern (MT_ROWS x MT_TRACKS cells) at dst; returns its size in bytes
uint32_t mt_pattern_encode(const uint32_t *cells, uint8_t *dst);

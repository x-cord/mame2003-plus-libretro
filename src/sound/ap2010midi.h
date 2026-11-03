/* AP2010 ROM wavetable synthesizer. SPDX-License-Identifier: BSD-3-Clause */
#ifndef AP2010_MIDI_H
#define AP2010_MIDI_H
#include <stdint.h>

#define AP2010_MIDI_RATE 22050
/* Measured hardware pitch agrees with 81 MHz / (0x0e6f + 1). */
#define AP2010_MIDI_NATIVE_RATE (81000000.0 / 3696.0)
#define AP2010_MIDI_FIFO 65536
#define AP2010_MIDI_VOICES 32

typedef struct {
    uint8_t program, volume, expression, pan, sustain, rpn_msb, rpn_lsb, bend_range;
    uint16_t bend;
} ap2010_midi_channel;

typedef struct {
    uint8_t active, channel, note, velocity, released, held;
    uint16_t zone, sample, length, loop, envelope, modulation;
    uint32_t age, serial;
    double position;
    float step, level, vibrato;
} ap2010_midi_voice;

typedef struct {
    ap2010_midi_channel channel[16];
    ap2010_midi_voice voice[AP2010_MIDI_VOICES];
    uint8_t fifo[AP2010_MIDI_FIFO];
    uint32_t head, count, serial, tempo, divisor;
    uint8_t running, pending, status, a, b, ended, error;
    uint32_t pending_tempo;
    double clock, deadline;
    double native_fraction, previous_l, previous_r, current_l, current_r;
} ap2010_midi_state;

/* ROM is the unmodified, linear 32 KiB 9h0-0008.midipcm.ic1 dump. */
void ap2010_midi_reset(ap2010_midi_state *s, const uint8_t *rom);
void ap2010_midi_message(ap2010_midi_state *s, const uint8_t *rom, uint8_t status, uint8_t a, uint8_t b);
void ap2010_midi_start(ap2010_midi_state *s, unsigned divisor);
void ap2010_midi_stop(ap2010_midi_state *s);
int ap2010_midi_can_accept_word(const ap2010_midi_state *s);
int ap2010_midi_push(ap2010_midi_state *s, uint8_t byte);
void ap2010_midi_render(ap2010_midi_state *s, const uint8_t *rom, int16_t *left, int16_t *right, unsigned frames);
/* Exposed for ROM-format validation and hardware probe comparisons. */
unsigned ap2010_midi_zone(const uint8_t *rom, unsigned program, unsigned note);
float ap2010_midi_rom_float(const uint8_t *rom, unsigned address);
#endif

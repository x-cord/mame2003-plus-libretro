/*
 * AP2010 ROM wavetable synthesizer. SPDX-License-Identifier: BSD-3-Clause
 *
 * Reconstructed from 9h0-0008.midipcm.ic1 and QUFB's published hardware
 * register captures.
 *
 * The ROM directory at 0x0dd4 has 51 big-endian pointers (base 0x8000).
 * A patch is {u8 program, u8 zones, u16 zone[zones]}. Each 20-byte zone:
 *   u8 upper_key, root_key; u16 PCM, loop_length, PCM_length;
 *   f32 tuning; u16 amplitude_envelope, pitch_modulation; f32 release.
 * PCM is signed 8-bit. The final loop_length samples repeat.
 * Envelope: f32 initial; repeated {u32 mode:8/end_tick:24, f32 delta}.
 * Mode 0 adds delta; mode 1 multiplies. End tick 0 means indefinitely;
 * a zero descriptor terminates the envelope. Ticks are absolute native frames.
 * Pitch modulation: u16 delay, interval, table, reserved; 16 f32 deltas.
 */
#include "ap2010midi.h"
#include <math.h>
#include <string.h>

static unsigned r16(const uint8_t *r, unsigned p)
{
    p &= 0x7fff;
    return ((unsigned)r[p] << 8) | r[(p + 1) & 0x7fff];
}
static uint32_t r32(const uint8_t *r, unsigned p)
{
    return ((uint32_t)r16(r, p) << 16) | r16(r, p + 2);
}
float ap2010_midi_rom_float(const uint8_t *r, unsigned p)
{
    uint32_t bits = r32(r, p);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

unsigned ap2010_midi_zone(const uint8_t *r, unsigned program, unsigned note)
{
    unsigned i, patch = 0, best = 256, last = 0, last_program = 0;
    for (i = 0; i < 51; i++) {
        unsigned p = r16(r, 0xdd4 + i * 2) & 0x7fff;
        unsigned id = r[p];
        if (p < 0xe3c || p > 0x207f || !r[p + 1] || r[p + 1] > 24) continue;
        if (id == program) { patch = p; break; }
        if (id < 128 && id >= program && id < best) { best = id; patch = p; }
        if (id < 128 && id >= last_program) { last_program = id; last = p; }
    }
    if (!patch) patch = last;
    if (!patch) return 0;
    for (i = 0; i < r[patch + 1]; i++) {
        unsigned zone = r16(r, patch + 2 + 2 * i) & 0x7fff;
        if (zone < 0xe3c || zone + 20 > 0x2090) return 0;
        if (note <= r[zone]) return zone;
    }
    return 0;
}

static void channel_reset(ap2010_midi_channel *c)
{
    c->volume = 100; c->expression = 127; c->pan = 64;
    c->sustain = 0; c->bend = 8192; c->bend_range = 2;
    c->rpn_msb = c->rpn_lsb = 127;
}
void ap2010_midi_reset(ap2010_midi_state *s, const uint8_t *r)
{
    unsigned i;
    memset(s, 0, sizeof(*s));
    for (i = 0; i < 16; i++) {
        channel_reset(&s->channel[i]);
        s->channel[i].program = r[r16(r, 0xdd4 + 2 * i) & 0x7fff];
    }
    s->tempo = 500000; s->divisor = 480;
}
static void release(ap2010_midi_state *s, ap2010_midi_voice *v)
{
    v->held = 1;
    if (!s->channel[v->channel].sustain) v->released = 1;
}
void ap2010_midi_message(ap2010_midi_state *s, const uint8_t *r, uint8_t status, uint8_t a, uint8_t b)
{
    unsigned ch = status & 15, i;
    ap2010_midi_channel *c = &s->channel[ch];
    a &= 127; b &= 127;
    switch (status & 0xf0) {
    case 0x80:
    case 0x90:
        if ((status & 0xf0) == 0x80 || !b) {
            for (i = 0; i < AP2010_MIDI_VOICES; i++)
                if (s->voice[i].active && s->voice[i].channel == ch && s->voice[i].note == a)
                    release(s, &s->voice[i]);
        } else {
            unsigned zone = ap2010_midi_zone(r, ch == 9 ? 128 : c->program, a);
            unsigned victim = 0;
            ap2010_midi_voice *v;
            if (!zone) return;
            for (i = 0; i < AP2010_MIDI_VOICES; i++) {
                if (!s->voice[i].active) { victim = i; break; }
                if (s->voice[i].serial < s->voice[victim].serial) victim = i;
            }
            v = &s->voice[victim]; memset(v, 0, sizeof(*v));
            v->channel = ch; v->note = a; v->velocity = b; v->zone = zone;
            v->sample = r16(r, zone + 2) & 0x7fff;
            v->loop = r16(r, zone + 4); v->length = r16(r, zone + 6);
            v->envelope = r16(r, zone + 12) & 0x7fff;
            v->modulation = r16(r, zone + 14) & 0x7fff;
            v->step = (float)(pow(2.0, ((int)a - r[zone + 1]) / 12.0) * ap2010_midi_rom_float(r, zone + 8));
            v->level = ap2010_midi_rom_float(r, v->envelope);
            v->serial = ++s->serial;
            v->active = v->length && v->loop <= v->length && v->sample + v->length <= 0x8000;
        }
        break;
    case 0xb0:
        switch (a) {
        case 7: c->volume = b; break;
        case 10: c->pan = b; break;
        case 11: c->expression = b; break;
        case 64:
            c->sustain = b >= 64;
            if (!c->sustain) for (i = 0; i < AP2010_MIDI_VOICES; i++)
                if (s->voice[i].channel == ch && s->voice[i].held) s->voice[i].released = 1;
            break;
        case 100: c->rpn_lsb = b; break;
        case 101: c->rpn_msb = b; break;
        case 6: if (!c->rpn_lsb && !c->rpn_msb) c->bend_range = b; break;
        case 120:
            for (i = 0; i < AP2010_MIDI_VOICES; i++) if (s->voice[i].channel == ch) s->voice[i].active = 0;
            break;
        case 121:
            channel_reset(c);
            for (i = 0; i < AP2010_MIDI_VOICES; i++)
                if (s->voice[i].channel == ch && s->voice[i].held) s->voice[i].released = 1;
            break;
        case 123:
            for (i = 0; i < AP2010_MIDI_VOICES; i++) if (s->voice[i].channel == ch) release(s, &s->voice[i]);
            break;
        }
        break;
    case 0xc0: c->program = a; break;
    case 0xe0: c->bend = a | (b << 7); break;
    }
}

void ap2010_midi_stop(ap2010_midi_state *s)
{
    unsigned i;
    s->head = s->count = 0; s->pending = s->running = 0; s->ended = 1;
    for (i = 0; i < AP2010_MIDI_VOICES; i++) s->voice[i].active = 0;
}
void ap2010_midi_start(ap2010_midi_state *s, unsigned divisor)
{
    s->head = s->count = 0; s->pending = s->running = s->ended = s->error = 0;
    s->clock = s->deadline = 0; s->tempo = 500000;
    s->divisor = divisor & 0x7fff;
    if (!s->divisor || divisor & 0x8000) { s->error = 1; s->ended = 1; }
}
int ap2010_midi_can_accept_word(const ap2010_midi_state *s)
{
    return s->count <= AP2010_MIDI_FIFO - 4;
}
int ap2010_midi_push(ap2010_midi_state *s, uint8_t byte)
{
    if (s->ended) return 1;
    if (s->count == AP2010_MIDI_FIFO) { s->error = 1; return 0; }
    s->fifo[(s->head + s->count) & (AP2010_MIDI_FIFO - 1)] = byte;
    s->count++;
    return 1;
}
static int peek(ap2010_midi_state *s, unsigned p)
{
    return p < s->count ? s->fifo[(s->head + p) & (AP2010_MIDI_FIFO - 1)] : -1;
}
static int variable(ap2010_midi_state *s, unsigned *p, uint32_t *value)
{
    unsigned i;
    *value = 0;
    for (i = 0; i < 4; i++) {
        int b = peek(s, *p);
        if (b < 0) return 0;
        (*p)++; *value = (*value << 7) | (b & 127);
        if (!(b & 128)) return 1;
    }
    s->error = s->ended = 1;
    return 0;
}
static void next_event(ap2010_midi_state *s)
{
    unsigned p = 0, status, a = 0, b = 0;
    uint32_t delta, length, tempo = 0;
    int v;
    if (s->pending || s->ended || !variable(s, &p, &delta)) return;
    v = peek(s, p);
    if (v < 0) return;
    status = v;
    if (status & 128) p++;
    else status = s->running;
    if (status >= 0x80 && status <= 0xef) {
        unsigned n = (status >> 4 == 0xc || status >> 4 == 0xd) ? 1 : 2;
        if (p + n > s->count) return;
        a = peek(s, p++); if (n == 2) b = peek(s, p++);
        if ((a | b) & 128) { s->error = s->ended = 1; return; }
    } else if (status == 0xff || status == 0xf0 || status == 0xf7) {
        unsigned type = 0;
        if (status == 0xff) {
            v = peek(s, p++); if (v < 0) return; type = v;
        }
        if (!variable(s, &p, &length)) return;
        if (length > AP2010_MIDI_FIFO - 8) { s->error = s->ended = 1; return; }
        if (p + length > s->count) return;
        if (status == 0xff && type == 0x51 && length == 3)
            tempo = ((uint32_t)peek(s, p) << 16) | (peek(s, p + 1) << 8) | peek(s, p + 2);
        status = (status == 0xff && type == 0x2f) ? 0xff : 0;
        p += length;
    } else { s->error = s->ended = 1; return; }
    if (status >= 0x80 && status <= 0xef) s->running = status;
    s->head = (s->head + p) & (AP2010_MIDI_FIFO - 1); s->count -= p;
    /* A tempo event's delta uses the PREVIOUS tempo. */
    s->deadline += (double)delta * s->tempo * AP2010_MIDI_RATE / (1000000.0 * s->divisor);
    s->status = status; s->a = a; s->b = b; s->pending_tempo = tempo; s->pending = 1;
}

static void envelope(ap2010_midi_voice *v, const uint8_t *r)
{
    unsigned p = v->envelope + 4, i;
    if (v->released) { v->level *= ap2010_midi_rom_float(r, v->zone + 16); return; }
    for (i = 0; i < 16 && p + 8 <= 0x2090; i++, p += 8) {
        uint32_t cmd = r32(r, p), end = cmd & 0xffffff;
        if (!cmd) return;
        if (!end || v->age < end) {
            float delta = ap2010_midi_rom_float(r, p + 4);
            if (cmd & 0x1000000) v->level *= delta;
            else v->level += delta;
            return;
        }
    }
}
static int16_t clip(double value)
{
    return value > 32767 ? 32767 : value < -32768 ? -32768 : (int16_t)value;
}
void ap2010_midi_render(ap2010_midi_state *s, const uint8_t *r, int16_t *left, int16_t *right, unsigned frames)
{
    unsigned n, i;
    double gain_l[16], gain_r[16], bend[16];
    for (n = 0; n < frames; n++) {
        double l = 0, rr = 0;
        unsigned messages = 0;
        while (!s->ended && messages++ < AP2010_MIDI_FIFO) {
            next_event(s);
            if (!s->pending || s->deadline > s->clock) break;
            if (s->status == 0xff) { s->ended = 1; s->count = 0; }
            else if (s->status) ap2010_midi_message(s, r, s->status, s->a, s->b);
            if (s->pending_tempo) s->tempo = s->pending_tempo;
            s->pending = 0;
        }
        /* Controllers may change inside this buffer. */
        if (n == 0 || messages > 1) for (i = 0; i < 16; i++) {
            ap2010_midi_channel *c = &s->channel[i];
            double gain = c->volume / 127.0 * c->expression / 127.0;
            double angle = c->pan * (1.5707963267948966 / 128.0);
            gain = gain * gain * 64.0;
            gain_l[i] = gain * cos(angle); gain_r[i] = gain * sin(angle);
            bend[i] = pow(2.0, ((int)c->bend - 8192) * c->bend_range / (8192.0 * 12.0));
        }
        s->native_fraction += AP2010_MIDI_NATIVE_RATE / AP2010_MIDI_RATE;
        if (s->native_fraction >= 1.0) {
        s->native_fraction -= 1.0;
        s->previous_l = s->current_l; s->previous_r = s->current_r;
        for (i = 0; i < AP2010_MIDI_VOICES; i++) {
            ap2010_midi_voice *v = &s->voice[i];
            unsigned pos, next;
            double fraction, value, velocity;
            if (!v->active) continue;
            envelope(v, r);
            if (v->released && v->level < 1e-6f) { v->active = 0; continue; }
            if (v->modulation) {
                unsigned delay = r16(r, v->modulation), interval = r16(r, v->modulation + 2);
                unsigned table = r16(r, v->modulation + 4) & 0x7fff;
                if (interval && v->age >= delay && table + 64 <= 0x2090)
                    v->vibrato += ap2010_midi_rom_float(r, table + 4 * (((v->age - delay) / interval) & 15));
            }
            pos = (unsigned)v->position;
            if (pos >= v->length) {
                if (!v->loop) { v->active = 0; continue; }
                v->position = v->length - v->loop + fmod(v->position - v->length, v->loop);
                pos = (unsigned)v->position;
            }
            next = pos + 1;
            if (next >= v->length) next = v->loop ? (unsigned)(v->length - v->loop) : pos;
            fraction = v->position - pos;
            value = (int8_t)r[v->sample + pos] * (1 - fraction) + (int8_t)r[v->sample + next] * fraction;
            velocity = v->velocity / 127.0;
            value *= v->level * velocity * velocity;
            l += value * gain_l[v->channel]; rr += value * gain_r[v->channel];
            v->position += v->step * (1.0 + v->vibrato) * bend[v->channel];
            if (v->age != UINT32_MAX) v->age++;
        }
        s->current_l = l; s->current_r = rr;
        }
        left[n] = clip(s->previous_l + (s->current_l - s->previous_l) * s->native_fraction);
        right[n] = clip(s->previous_r + (s->current_r - s->previous_r) * s->native_fraction);
        s->clock++;
    }
}

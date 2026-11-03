/***************************************************************************
    BREW SMAF phrase (MMMG/SEQU) sequencer and Yamaha MA-family FM synthesis.

    FM waveform, envelope, feedback and routing calculations adapted to C from
    but80/fmfm.core (MIT, copyright 2018 but80). See licenses/fmfm-core-MIT.txt.
    Container/event ABI: supplied Qualcomm audfmt and BREW IMedia references.
***************************************************************************/
#include "brew_smaf.h"
#include "driver.h"
#include "state.h"
#include <math.h>
#include <string.h>

#define SMAF_SLOTS 32
#define SMAF_VOICES 64
#define SMAF_WAVE 1024
#define SMAF_MOD 8192
#define SMAF_PI 3.14159265358979323846
#define SMAF_EPSILON (1.0 / 32768.0)

/* Operator state lives entirely in numeric arrays for portable save states. */
enum
{
    OP_PHASE,
    OP_STEP,
    OP_LEVEL,
    OP_ATTACK,
    OP_DECAY,
    OP_SUSTAIN,
    OP_RELEASE,
    OP_SL,
    OP_GAIN,
    OP_FEEDBACK,
    OP_VALUES
};
enum
{
    EG_OFF,
    EG_ATTACK,
    EG_DECAY,
    EG_SUSTAIN,
    EG_RELEASE
};
struct smaf_voice
{
    UINT32 owner, channel, note, gate, algorithm, lfo, velocity, stages[4];
    UINT8 patch[30];
    double op[4][OP_VALUES], feedback[4], lfo_phase, pan[2], gain;
};
struct smaf_sequence
{
    UINT32 address, size, begin, end, cursor, ticks, position, next, timebase;
    UINT32 duration, playing, paused, finished, patch_count, volume;
    UINT8 patches[4][30];
    UINT32 program[4], velocity[4], level[4], expression[4], pan[4], bend[4], modulation[4];
    INT32 octave[4];
};
static struct smaf_sequence sequences[SMAF_SLOTS];
static struct smaf_voice voices[SMAF_VOICES];
static UINT8* (*smaf_memory)(UINT32, UINT32);
static int smaf_stream = -1;
static UINT32 smaf_rate;
static float waves[32][SMAF_WAVE], vibrato[4][SMAF_MOD], tremolo[4][SMAF_MOD];
static double volume_gain[128], pan_gain[128][2], ksl_gain[4][8][32];
static const UINT8 multiples[16] = { 1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30 };
static const UINT8 carriers[8][4] = { { 0, 1, 0, 0 }, { 1, 1, 0, 0 }, { 1, 1, 1, 1 }, { 0, 0, 0, 1 },
    { 0, 0, 0, 1 }, { 0, 1, 0, 1 }, { 1, 0, 0, 1 }, { 1, 0, 1, 1 } };
static const double feedback_gain[8] = { 0, 1.0 / 32, 1.0 / 16, 1.0 / 8, 1.0 / 4, 1.0 / 2, 1, 2 };
static const double lfo_hz[4] = { 1.8, 4, 5.9, 7 };
static const double attack_time[2][9]
    = { { 3.07068, 3.07068, 3.07068, 2.45670, 2.45670, 2.04699, 2.04699, 1.75471, 1.75471 },
          { 3.07082, 2.45660, 1.75489, 1.22816, .87737, .61414, .43876, .30714, .21935 } };
static const double decay_db[2][16]
    = { { 17.9342, 17.9342, 17.9342, 17.9342, 17.9342, 22.4116, 22.4116, 22.4116, 22.4116, 26.9076, 26.9076,
            26.9076, 26.9076, 31.3661, 31.3661, 31.3661 },
          { 17.9465, 22.4376, 22.4376, 31.4026, 31.4026, 44.8696, 44.8696, 62.7959, 62.7959, 89.6707, 89.6707,
              125.5546, 125.5546, 179.2684, 179.2684, 250.9128 } };
static const double detune[4][16] = { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, .05, .05, .05, .05, .09, .09, .14, .14, .18, .23, .27, .32, .37, .37 },
    { .05, .05, .09, .09, .14, .14, .18, .23, .27, .32, .41, .46, .59, .64, .73, .73 },
    { .09, .09, .14, .14, .18, .23, .28, .32, .41, .46, .59, .64, .87, .91, 1, 1 } };

static UINT32 smaf_be32(const UINT8* p)
{
    return ((UINT32)p[0] << 24) | ((UINT32)p[1] << 16) | ((UINT32)p[2] << 8) | p[3];
}
static int smaf_value(const UINT8* p, UINT32 end, UINT32* cursor, UINT32* value)
{
    UINT32 v;
    if (*cursor >= end)
        return 0;
    v = p[(*cursor)++];
    if (v & 128)
    {
        if (*cursor >= end)
            return 0;
        v = (((v & 127) << 7) | (p[(*cursor)++] & 127)) + 128;
    }
    *value = v;
    return 1;
}
static UINT32 smaf_samples(UINT32 ticks, UINT32 timebase)
{
    return (UINT64)ticks * timebase * smaf_rate / 1000;
}
static void smaf_tables(void)
{
    static const double volume_db[32]
        = { 1e30, 47.9, 42.6, 37.2, 33.1, 29.8, 27, 24.6, 22.4, 20.6, 18.9, 17.3, 15.9, 14.6, 13.4, 12.2,
              11.1, 10.1, 9.2, 8.3, 7.4, 6.6, 5.8, 5.1, 4.4, 3.6, 3, 2.3, 1.7, 1.1, .6, 0 };
    static const double vb_depth[4] = { 3.4, 6.7, 13.5, 26.8 }, am_depth[4] = { 1.3, 2.8, 5.8, 11.8 };
    static const double ksl_base[4] = { 0, .08, 1.0 / 15, 1.0 / 15 }, ksl_block[4] = { 0, 3, 1.5, 6.01 },
                        ksl_fnum[4] = { 0, .38, .185, .75 };
    unsigned i, j, k, b, f;
    memset(waves, 0, sizeof(waves));
    for (i = 0; i < SMAF_WAVE; i++)
    {
        double phase = (double)i / SMAF_WAVE, sine = sin(2 * SMAF_PI * phase), clipped = sine * sqrt(2.0);
        if (clipped > 1)
            clipped = 1;
        if (clipped < -1)
            clipped = -1;
        waves[0][i] = sine;
        waves[8][i] = clipped;
        waves[16][i] = phase < .25 ? phase * 4 : phase < .75 ? 2 - phase * 4 : phase * 4 - 4;
        waves[24][i] = phase < .5 ? phase * 2 : phase * 2 - 2;
        waves[6][i] = i < 512 ? 1 : -1;
        if (i < 512)
        {
            waves[7][i] = pow(2, -(double)i / 16);
            waves[7][1023 - i] = -pow(2, -((double)i + 1) / 16);
        }
    }
    for (b = 0; b < 32; b += 8)
        for (i = 0; i < 1024; i++)
        {
            waves[b + 1][i] = i < 512 ? waves[b][i] : 0;
            waves[b + 2][i] = waves[b][i % 512];
            waves[b + 3][i] = (i % 512) < 256 ? waves[b][i % 256] : 0;
            waves[b + 4][i] = i < 512 ? waves[b][i * 2] : 0;
            waves[b + 5][i] = i < 512 ? waves[b][(i % 256) * 2] : 0;
        }
    for (i = 0; i < 1024; i++)
    {
        waves[14][i] = i < 512 ? 1 : 0;
        waves[22][i] = (i % 512) < 256 ? 1 : 0;
        waves[30][i] = i < 256 ? 1 : 0;
    }
    for (i = 0; i < 128; i++)
    {
        volume_gain[i] = pow(10, -volume_db[i >> 2] / 20);
        pan_gain[i][0] = cos(SMAF_PI * .5 * i / 127);
        pan_gain[i][1] = sin(SMAF_PI * .5 * i / 127);
    }
    for (j = 0; j < 4; j++)
        for (i = 0; i < SMAF_MOD; i++)
        {
            double phase = (double)i / SMAF_MOD * 4;
            double ts = phase < 1 ? phase : phase < 3 ? 2 - phase : phase - 4;
            double tc = phase < 2 ? 1 - phase : phase - 3;
            vibrato[j][i] = pow(2, ts * vb_depth[j] / 1200);
            tremolo[j][i] = pow(10, (tc - 1) * .5 * am_depth[j] / 20);
        }
    for (k = 0; k < 4; k++)
        for (b = 0; b < 8; b++)
            for (f = 0; f < 32; f++)
            {
                double db
                    = ksl_base[k] - ksl_block[k] * ((int)b - 2) - ksl_fnum[k] * ((int)(f > 15 ? 15 : f) - 7);
                if (b < 2 || db > 0)
                    db = 0;
                ksl_gain[k][b][f] = pow(10, db / 20);
            }
}
static double smaf_rate_coef(unsigned rate, unsigned ksr, unsigned key)
{
    return rate ? pow(10, -decay_db[ksr][key] * pow(2, (double)rate) / (16 * 20 * smaf_rate)) : 1;
}
static void smaf_voice_gain(struct smaf_voice* v)
{
    struct smaf_sequence* s = &sequences[v->owner - 1];
    int pan = s->pan[v->channel];
    if (v->patch[1] & 32)
        pan += ((v->patch[0] >> 3) - 15) * 4;
    if (pan < 0)
        pan = 0;
    if (pan > 127)
        pan = 127;
    v->pan[0] = pan_gain[pan][0];
    v->pan[1] = pan_gain[pan][1];
    v->gain = volume_gain[s->level[v->channel]] * volume_gain[s->expression[v->channel]]
        * volume_gain[v->velocity];
}
static void smaf_voice_pitch(struct smaf_voice* v, int initialize)
{
    struct smaf_sequence* s = &sequences[v->owner - 1];
    /* FNUM conversion precedes the MA basic-octave divider (BO=1). */
    double hz = 440 * pow(2, ((double)(INT32)v->note - 57 + ((double)s->bend[v->channel] - 64) / 32) / 12);
    double coef = (double)(1 << 19) / 48000 * .5;
    int bo = v->patch[0] & 3, block = ((int)v->note + 3 - 12) / 12, fnum, key, bks, i;
    if (block < 0)
        block = 0;
    if (block > 7)
        block = 7;
    fnum = (int)(hz * coef * 2 + pow(2, block) * .5) >> block;
    while (fnum > 1023 && block < 7)
    {
        fnum >>= 1;
        block++;
    }
    if (fnum > 1023)
        fnum = 1023;
    key = (block + 1 - bo) * 2 + (fnum >> 9);
    if (key < 0)
        key = 0;
    if (key > 15)
        key = 15;
    bks = block + 1 - bo;
    if (bks < 0)
        bks = 0;
    if (bks > 7)
        bks = 7;
    for (i = 0; i < 4; i++)
    {
        const UINT8* p = v->patch + 2 + i * 7;
        double *op = v->op[i], dt = detune[p[5] & 3][(block << 1) | (fnum >> 9)];
        unsigned ksr = p[0] & 1, ar = p[2] >> 4, tl = p[3] >> 2;
        if (p[5] & 4)
            dt = -dt;
        op[OP_STEP] = ((double)fnum * pow(2, block + 3 - bo) / (16 * coef) + dt) / smaf_rate
            * multiples[p[5] >> 4] * .5;
        if (initialize)
        {
            op[OP_ATTACK] = ar
                ? 1 / (attack_time[ksr][(key >> 1) + (key & 1)] / pow(2, (double)ar - 1) * smaf_rate)
                : 0;
            op[OP_DECAY] = smaf_rate_coef(p[1] & 15, ksr, key);
            op[OP_SUSTAIN] = smaf_rate_coef(p[0] >> 4, ksr, key);
            op[OP_RELEASE] = smaf_rate_coef(p[1] >> 4, ksr, key);
            op[OP_SL] = (p[2] & 15) == 15 ? 0 : pow(10, -3.0 * (p[2] & 15) / 20);
            op[OP_GAIN] = tl >= 63 ? 0 : pow(10, -.75 * tl / 20) * ksl_gain[p[3] & 3][bks][fnum >> 5];
            op[OP_FEEDBACK] = feedback_gain[p[6] & 7];
            v->stages[i] = ar ? EG_ATTACK : EG_OFF;
        }
    }
}
static void smaf_note_off(struct smaf_voice* v)
{
    unsigned i;
    v->gate = 0;
    for (i = 0; i < 4; i++)
        if (v->stages[i] && !(v->patch[2 + i * 7] & 8))
            v->stages[i] = EG_RELEASE;
}
static void smaf_note_on(unsigned slot, unsigned channel, unsigned pitch, UINT32 gate)
{
    struct smaf_sequence* s = &sequences[slot];
    struct smaf_voice* v = NULL;
    unsigned i, program = s->program[channel];
    if (!gate || program >= s->patch_count)
        return;
    for (i = 0; i < SMAF_VOICES; i++)
        if (!voices[i].owner)
        {
            v = &voices[i];
            break;
        }
    if (!v)
    {
        v = &voices[0];
        for (i = 1; i < SMAF_VOICES; i++)
            if (voices[i].gate < v->gate)
                v = &voices[i];
    }
    memset(v, 0, sizeof(*v));
    v->owner = slot + 1;
    v->channel = channel;
    v->velocity = s->velocity[channel];
    v->note = (INT32)pitch + s->octave[channel] * 12;
    v->gate = s->position + smaf_samples(gate, s->timebase);
    memcpy(v->patch, s->patches[program], 30);
    v->algorithm = v->patch[1] & 7;
    v->lfo = v->patch[1] >> 6;
    smaf_voice_pitch(v, 1);
    smaf_voice_gain(v);
}
static void smaf_channel_update(unsigned slot, unsigned channel, int pitch)
{
    unsigned i;
    for (i = 0; i < SMAF_VOICES; i++)
        if (voices[i].owner == slot + 1 && voices[i].channel == channel)
        {
            if (pitch)
                smaf_voice_pitch(&voices[i], 0);
            else
                smaf_voice_gain(&voices[i]);
        }
}
static void smaf_control(unsigned slot, unsigned command, unsigned value)
{
    struct smaf_sequence* s = &sequences[slot];
    unsigned channel = command >> 6, type = command & 15;
    if ((command & 0x30) == 0x30)
    {
        switch (type)
        {
        case 0:
            s->program[channel] = value & 3;
            break;
        case 2:
            if (value <= 4)
                s->octave[channel] = value;
            else if (value >= 0x81 && value <= 0x84)
                s->octave[channel] = -(INT32)(value & 15);
            break;
        case 3:
            s->modulation[channel] = value;
            break;
        case 4:
            s->bend[channel] = value & 127;
            smaf_channel_update(slot, channel, 1);
            break;
        case 6:
            s->velocity[channel] = value & 127;
            break;
        case 7:
            s->level[channel] = value & 127;
            smaf_channel_update(slot, channel, 0);
            break;
        case 10:
            s->pan[channel] = value & 127;
            smaf_channel_update(slot, channel, 0);
            break;
        case 11:
            s->expression[channel] = value & 127;
            smaf_channel_update(slot, channel, 0);
            break;
        }
    }
    else
    {
        unsigned short_type = command & 0x30, v = command & 15;
        if (short_type == 0 && v)
        {
            s->expression[channel] = v == 1 ? 0 : (v - 2) * 8 + 31;
            smaf_channel_update(slot, channel, 0);
        }
        if (short_type == 0x10)
        {
            s->bend[channel] = v * 8;
            smaf_channel_update(slot, channel, 1);
        }
        if (short_type == 0x20)
            s->modulation[channel] = v * 8;
    }
}
/* Consume one event. Scan mode validates and computes the duration without playing. */
static int smaf_event(
    unsigned slot, const UINT8* p, UINT32* at, UINT32 end, UINT32 ticks, UINT32* last, int scan)
{
    unsigned cmd, sub;
    UINT32 value;
    if (*at >= end)
        return -1;
    cmd = p[(*at)++];
    if (!cmd)
    {
        if (*at >= end)
            return -1;
        sub = p[(*at)++];
        if (!sub)
        {
            if (*at >= end || p[(*at)++])
                return -1;
            return 0;
        }
        value = 0;
        if ((sub & 0x30) == 0x30)
        {
            if (*at >= end)
                return -1;
            value = p[(*at)++];
        }
        if (!scan)
            smaf_control(slot, sub, value);
    }
    else if (cmd == 255)
    {
        if (*at >= end)
            return -1;
        sub = p[(*at)++];
        if (sub == 240)
        {
            if (*at >= end)
                return -1;
            value = p[(*at)++];
            if (value > end - *at)
                return -1;
            *at += value;
        }
        else if (sub != 0 && (sub < 0x10 || sub > 0x1f))
            return -1;
    }
    else
    {
        if (!smaf_value(p, end, at, &value))
            return -1;
        if (ticks + value > *last)
            *last = ticks + value;
        if (!scan)
            smaf_note_on(slot, cmd >> 6, (cmd & 15) + ((cmd >> 4) & 3) * 12 + 36, value);
    }
    return 1;
}
UINT32 brew_smaf_load(unsigned slot, UINT32 address, UINT32 size)
{
    struct smaf_sequence parsed;
    const UINT8* p = smaf_memory ? smaf_memory(address, size) : NULL;
    UINT32 at, end, chunk, voice_end, delta, ticks = 0, last = 0;
    int status = 1;
    if (slot >= SMAF_SLOTS || !p || size < 10 || memcmp(p, "MMMD", 4))
        return 0;
    memset(&parsed, 0, sizeof(parsed));
    end = smaf_be32(p + 4);
    if (end > size - 8)
        return 0;
    end += 8;
    for (at = 8; at + 8 <= end; at += 8 + chunk)
    {
        chunk = smaf_be32(p + at + 4);
        if (chunk > end - at - 8)
            return 0;
        if (!memcmp(p + at, "MMMG", 4))
            break;
    }
    if (at + 10 > end || chunk < 2 || memcmp(p + at, "MMMG", 4) || p[at + 8] != 1 || !p[at + 9])
        return 0;
    parsed.timebase = p[at + 9];
    end = at + 8 + chunk;
    at += 10;
    while (at + 8 <= end)
    {
        chunk = smaf_be32(p + at + 4);
        if (chunk > end - at - 8)
            return 0;
        if (!memcmp(p + at, "VOIC", 4))
        {
            voice_end = at + 8 + chunk;
            at += 8;
            while (at + 8 <= voice_end)
            {
                UINT32 n = smaf_be32(p + at + 4);
                if (n > voice_end - at - 8)
                    return 0;
                if (!memcmp(p + at, "EXVO", 4))
                {
                    const UINT8* v = p + at + 8;
                    if (parsed.patch_count >= 4 || (n != 23 && n != 37) || v[0] != 255 || v[1] != 240
                        || v[2] != n - 3 || v[3] != 0x43 || v[4] != 4 || v[5] != 1 || v[n - 1] != 247)
                        return 0;
                    memcpy(parsed.patches[parsed.patch_count], v + 6, n - 7);
                    parsed.patch_count++;
                }
                at += 8 + n;
            }
            if (at != voice_end)
                return 0;
            continue;
        }
        if (!memcmp(p + at, "SEQU", 4))
        {
            parsed.begin = at + 8;
            parsed.end = at + 8 + chunk;
        }
        at += 8 + chunk;
    }
    if (!parsed.patch_count || !parsed.begin)
        return 0;
    at = parsed.begin;
    while (at < parsed.end && status > 0)
    {
        if (!smaf_value(p, parsed.end, &at, &delta) || delta > 3600000 / parsed.timebase - ticks)
            return 0;
        ticks += delta;
        status = smaf_event(slot, p, &at, parsed.end, ticks, &last, 1);
    }
    if (status < 0)
        return 0;
    if (ticks > last)
        last = ticks;
    if (last > 3600000 / parsed.timebase)
        return 0;
    parsed.duration = last * parsed.timebase;
    if (!parsed.duration)
        return 0;
    parsed.address = address;
    parsed.size = size;
    parsed.volume = 100;
    brew_smaf_stop(slot);
    sequences[slot] = parsed;
    return parsed.duration;
}
static void smaf_advance(unsigned slot)
{
    struct smaf_sequence* s = &sequences[slot];
    const UINT8* p = smaf_memory(s->address, s->size);
    UINT32 delta, last = 0;
    int status;
    if (!p)
    {
        s->playing = 0;
        s->finished = 1;
        return;
    }
    while (s->cursor < s->end && s->next <= s->position)
    {
        status = smaf_event(slot, p, &s->cursor, s->end, s->ticks, &last, 0);
        if (status <= 0)
        {
            s->cursor = s->end;
            break;
        }
        if (!smaf_value(p, s->end, &s->cursor, &delta))
        {
            s->cursor = s->end;
            break;
        }
        s->ticks += delta;
        s->next = smaf_samples(s->ticks, s->timebase);
    }
}
static double smaf_operator(struct smaf_voice* v, unsigned index, double modulation, unsigned lfo)
{
    const UINT8* p = v->patch + 2 + index * 7;
    double *op = v->op[index], gain, phase, step = op[OP_STEP];
    unsigned wave = p[6] >> 3, at;
    if (!v->stages[index])
        return 0;
    if (p[4] & 1)
        step *= vibrato[(p[4] >> 1) & 3][lfo];
    op[OP_PHASE] += step;
    op[OP_PHASE] -= floor(op[OP_PHASE]);
    switch (v->stages[index])
    {
    case EG_ATTACK:
        op[OP_LEVEL] += op[OP_ATTACK];
        if (op[OP_LEVEL] < 1)
            break;
        op[OP_LEVEL] = 1;
        v->stages[index] = EG_DECAY;
        /* fall through */
    case EG_DECAY:
        if (op[OP_LEVEL] > op[OP_SL])
        {
            op[OP_LEVEL] *= op[OP_DECAY];
            break;
        }
        v->stages[index] = EG_SUSTAIN;
        /* fall through */
    case EG_SUSTAIN:
        if (op[OP_LEVEL] > SMAF_EPSILON)
            op[OP_LEVEL] *= op[OP_SUSTAIN];
        else
            v->stages[index] = EG_OFF;
        break;
    case EG_RELEASE:
        if (op[OP_LEVEL] > SMAF_EPSILON)
            op[OP_LEVEL] *= op[OP_RELEASE];
        else
        {
            op[OP_LEVEL] = 0;
            v->stages[index] = EG_OFF;
        }
        break;
    }
    gain = op[OP_LEVEL] * op[OP_GAIN];
    if (p[4] & 16)
        gain *= tremolo[(p[4] >> 5) & 3][lfo];
    phase = op[OP_PHASE] + modulation;
    phase -= floor(phase);
    at = (unsigned)(phase * 1024) & 1023;
    return waves[wave][at] * gain;
}
static double smaf_voice_next(struct smaf_voice* v)
{
    double a = 0, b = 0, c = 0, d = 0, result = 0, blend = .5 * 48000 / smaf_rate;
    unsigned i, lfo = (unsigned)(v->lfo_phase * SMAF_MOD) & (SMAF_MOD - 1), live = 0;
    v->lfo_phase += lfo_hz[v->lfo] / smaf_rate;
    if (v->lfo_phase >= 1)
        v->lfo_phase -= 1;
    for (i = 0; i < 4; i++)
        if (carriers[v->algorithm][i] && v->stages[i])
            live = 1;
    if (!live)
    {
        v->owner = 0;
        return 0;
    }
    switch (v->algorithm)
    {
    case 0:
        a = smaf_operator(v, 0, v->feedback[0], lfo);
        result = smaf_operator(v, 1, a * 4, lfo);
        break;
    case 1:
        a = smaf_operator(v, 0, v->feedback[0], lfo);
        b = smaf_operator(v, 1, 0, lfo);
        result = a + b;
        break;
    case 2:
        a = smaf_operator(v, 0, v->feedback[0], lfo);
        b = smaf_operator(v, 1, 0, lfo);
        c = smaf_operator(v, 2, v->feedback[2], lfo);
        d = smaf_operator(v, 3, 0, lfo);
        result = a + b + c + d;
        break;
    case 3:
        a = smaf_operator(v, 0, v->feedback[0], lfo);
        b = smaf_operator(v, 1, 0, lfo);
        c = smaf_operator(v, 2, b * 4, lfo);
        result = smaf_operator(v, 3, (a + c) * 4, lfo);
        break;
    case 4:
        a = smaf_operator(v, 0, v->feedback[0], lfo);
        b = smaf_operator(v, 1, a * 4, lfo);
        c = smaf_operator(v, 2, b * 4, lfo);
        result = smaf_operator(v, 3, c * 4, lfo);
        break;
    case 5:
        a = smaf_operator(v, 0, v->feedback[0], lfo);
        b = smaf_operator(v, 1, a * 4, lfo);
        c = smaf_operator(v, 2, v->feedback[2], lfo);
        d = smaf_operator(v, 3, c * 4, lfo);
        result = b + d;
        break;
    case 6:
        a = smaf_operator(v, 0, v->feedback[0], lfo);
        b = smaf_operator(v, 1, 0, lfo);
        c = smaf_operator(v, 2, b * 4, lfo);
        d = smaf_operator(v, 3, c * 4, lfo);
        result = a + d;
        break;
    case 7:
        a = smaf_operator(v, 0, v->feedback[0], lfo);
        b = smaf_operator(v, 1, 0, lfo);
        c = smaf_operator(v, 2, b * 4, lfo);
        d = smaf_operator(v, 3, 0, lfo);
        result = a + c + d;
        break;
    }
    if (blend > 1)
        blend = 1;
    a *= v->op[0][OP_FEEDBACK];
    c *= v->op[2][OP_FEEDBACK];
    v->feedback[0] = v->feedback[1] * (1 - blend) + a * blend;
    v->feedback[1] = a;
    v->feedback[2] = v->feedback[3] * (1 - blend) + c * blend;
    v->feedback[3] = c;
    return result * v->gain;
}
static void smaf_update(int param, INT16** buffer, int length)
{
    int sample;
    unsigned i;
    for (sample = 0; sample < length; sample++)
    {
        double left = 0, right = 0;
        for (i = 0; i < SMAF_SLOTS; i++)
            if (sequences[i].playing && !sequences[i].paused)
                smaf_advance(i);
        for (i = 0; i < SMAF_VOICES; i++)
            if (voices[i].owner)
            {
                struct smaf_voice* v = &voices[i];
                struct smaf_sequence* s = &sequences[v->owner - 1];
                double value;
                if (!s->playing || s->paused)
                    continue;
                if (v->gate && s->position >= v->gate)
                    smaf_note_off(v);
                value = smaf_voice_next(v) * (double)s->volume / 100;
                left += value * v->pan[0];
                right += value * v->pan[1];
            }
        for (i = 0; i < SMAF_SLOTS; i++)
            if (sequences[i].playing && !sequences[i].paused)
            {
                struct smaf_sequence* s = &sequences[i];
                if (++s->position >= (UINT64)s->duration * smaf_rate / 1000)
                {
                    unsigned j, live = 0;
                    for (j = 0; j < SMAF_VOICES; j++)
                        if (voices[j].owner == i + 1)
                            live = 1;
                    if (!live)
                    {
                        s->playing = 0;
                        s->finished = 1;
                    }
                    /* A corrupt/no-release voice must not hold IMedia forever. */
                    else if (s->position >= (UINT64)(s->duration + 2000) * smaf_rate / 1000)
                    {
                        for (j = 0; j < SMAF_VOICES; j++)
                            if (voices[j].owner == i + 1)
                                voices[j].owner = 0;
                        s->playing = 0;
                        s->finished = 1;
                    }
                }
            }
        left *= 5000;
        right *= 5000;
        if (left > 32767)
            left = 32767;
        if (left < -32768)
            left = -32768;
        if (right > 32767)
            right = 32767;
        if (right < -32768)
            right = -32768;
        buffer[0][sample] = (INT16)left;
        buffer[1][sample] = (INT16)right;
    }
}
void brew_smaf_sync(void)
{
    if (smaf_stream >= 0)
        stream_update(smaf_stream, 0);
}
void brew_smaf_stop(unsigned slot)
{
    unsigned i;
    if (slot >= SMAF_SLOTS)
        return;
    brew_smaf_sync();
    sequences[slot].playing = 0;
    sequences[slot].paused = 0;
    sequences[slot].finished = 0;
    for (i = 0; i < SMAF_VOICES; i++)
        if (voices[i].owner == slot + 1)
            voices[i].owner = 0;
}
void brew_smaf_play(unsigned slot)
{
    struct smaf_sequence* s;
    const UINT8* p;
    UINT32 delta;
    unsigned i;
    if (slot >= SMAF_SLOTS)
        return;
    brew_smaf_stop(slot);
    s = &sequences[slot];
    p = smaf_memory ? smaf_memory(s->address, s->size) : NULL;
    if (!p || !s->begin || !s->duration)
        return;
    s->cursor = s->begin;
    s->ticks = s->position = s->finished = 0;
    for (i = 0; i < 4; i++)
    {
        s->program[i] = 0;
        s->octave[i] = 0;
        s->velocity[i] = 127;
        s->level[i] = 100;
        s->expression[i] = 127;
        s->pan[i] = 64;
        s->bend[i] = 64;
        s->modulation[i] = 0;
    }
    if (!smaf_value(p, s->end, &s->cursor, &delta))
        return;
    s->ticks = delta;
    s->next = smaf_samples(delta, s->timebase);
    s->playing = 1;
}
void brew_smaf_pause(unsigned slot, int paused)
{
    if (slot < SMAF_SLOTS)
    {
        brew_smaf_sync();
        sequences[slot].paused = paused;
    }
}
void brew_smaf_volume(unsigned slot, unsigned volume)
{
    if (slot < SMAF_SLOTS)
    {
        brew_smaf_sync();
        sequences[slot].volume = volume > 100 ? 100 : volume;
    }
}
int brew_smaf_finished(unsigned slot) { return slot < SMAF_SLOTS && sequences[slot].finished; }
void brew_smaf_reset(UINT8* (*reader)(UINT32, UINT32))
{
    brew_smaf_sync();
    smaf_memory = reader;
    memset(sequences, 0, sizeof(sequences));
    memset(voices, 0, sizeof(voices));
}
static int smaf_start(const struct MachineSound* msound)
{
    unsigned i;
    static const char* names[2] = { "BREW SMAF left", "BREW SMAF right" };
    static const int levels[2] = { MIXER(80, MIXER_PAN_LEFT), MIXER(80, MIXER_PAN_RIGHT) };
    smaf_rate = Machine->sample_rate ? Machine->sample_rate : 48000;
    smaf_tables();
    smaf_stream = -1;
    for (i = 0; i < SMAF_SLOTS; i++)
    {
        struct smaf_sequence* s = &sequences[i];
        char module[40];
        sprintf(module, "brew_smaf_sequence_%u", i / 16);
#define SAVE_SEQ(name, count) state_save_register_UINT32(module, i % 16, #name, &s->name, count)
        SAVE_SEQ(address, 1);
        SAVE_SEQ(size, 1);
        SAVE_SEQ(begin, 1);
        SAVE_SEQ(end, 1);
        SAVE_SEQ(cursor, 1);
        SAVE_SEQ(ticks, 1);
        SAVE_SEQ(position, 1);
        SAVE_SEQ(next, 1);
        SAVE_SEQ(timebase, 1);
        SAVE_SEQ(duration, 1);
        SAVE_SEQ(playing, 1);
        SAVE_SEQ(paused, 1);
        SAVE_SEQ(finished, 1);
        SAVE_SEQ(patch_count, 1);
        SAVE_SEQ(volume, 1);
#undef SAVE_SEQ
        state_save_register_UINT8(module, i % 16, "patches", s->patches[0], sizeof(s->patches));
        state_save_register_UINT32(module, i % 16, "program", s->program, 4);
        state_save_register_UINT32(module, i % 16, "velocity", s->velocity, 4);
        state_save_register_UINT32(module, i % 16, "level", s->level, 4);
        state_save_register_UINT32(module, i % 16, "expression", s->expression, 4);
        state_save_register_UINT32(module, i % 16, "pan", s->pan, 4);
        state_save_register_UINT32(module, i % 16, "bend", s->bend, 4);
        state_save_register_UINT32(module, i % 16, "modulation", s->modulation, 4);
        state_save_register_INT32(module, i % 16, "octave", s->octave, 4);
    }
    for (i = 0; i < SMAF_VOICES; i++)
    {
        struct smaf_voice* v = &voices[i];
        char module[40];
        sprintf(module, "brew_smaf_voice_%u", i / 16);
        state_save_register_UINT32(module, i % 16, "owner", &v->owner, 1);
        state_save_register_UINT32(module, i % 16, "channel", &v->channel, 1);
        state_save_register_UINT32(module, i % 16, "note", &v->note, 1);
        state_save_register_UINT32(module, i % 16, "gate", &v->gate, 1);
        state_save_register_UINT32(module, i % 16, "algorithm", &v->algorithm, 1);
        state_save_register_UINT32(module, i % 16, "lfo", &v->lfo, 1);
        state_save_register_UINT32(module, i % 16, "velocity", &v->velocity, 1);
        state_save_register_UINT32(module, i % 16, "stages", v->stages, 4);
        state_save_register_UINT8(module, i % 16, "patch", v->patch, 30);
        state_save_register_double(module, i % 16, "operators", v->op[0], 4 * OP_VALUES);
        state_save_register_double(module, i % 16, "feedback", v->feedback, 4);
        state_save_register_double(module, i % 16, "lfo_phase", &v->lfo_phase, 1);
        state_save_register_double(module, i % 16, "pan", v->pan, 2);
        state_save_register_double(module, i % 16, "gain", &v->gain, 1);
    }
    state_save_register_func_presave(brew_smaf_sync);
    if (Machine->sample_rate)
        smaf_stream = stream_init_multi(2, names, levels, smaf_rate, 0, smaf_update);
    return Machine->sample_rate && smaf_stream < 0;
}
static void smaf_stop(void) { smaf_stream = -1; }
struct CustomSound_interface brew_smaf_interface = { smaf_start, smaf_stop, 0 };

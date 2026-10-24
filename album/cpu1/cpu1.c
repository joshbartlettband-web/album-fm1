/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hausway */
/* Album's second core (device only; included by app.c inside the unity build, after the platform).
 *
 * Core 1 renders half of the voices and the reverse reverb (engine.h) while core 0 renders the other half, inside
 * the audio interrupt: core 0 asks, works, then waits for core 1 before it leaves the interrupt. Core 1 is otherwise
 * spinning in RAM (.c1_text), so core 0 may stop the flash between blocks (a save, an update) with nothing of core 1
 * reading it.
 *
 * What is known about the AC79's second core, and where it comes from (registers and steps, not code):
 *   the start (felucca-bsp chip/fm1_cpu1.h, after the SDK's EnableOtherCpu): the entry address in the word at
 *     0x01C7FFF8, SYS_DIV (0x10008) bit 3 set during the start, CORE_CON (0x1EEE004) bit 3 set and bit 1 cleared
 *   core 1's interrupt bank at 0x1EEF300 (cleared before the start; it runs polled) and its stack-limit registers at
 *     0x1EEF2D8.. (core 0's are at 0x1EEF0D8..); holding it again (CORE_CON bit 1 set, bit 3 cleared) can abort a bus
 *     access and raise a late bus-invalid flag on core 0, so those traps are masked around the hold and the flag
 *     cleared after (Felucca 1.6.1's fm1_cpu1_halt and Melodee's notes, keremimo/melodee)
 *   on some FM-1s core 1 now and then reads shared RAM wrong (keremimo/melodee#17): every request carries a check
 *     word and runs only when it matches; a job whose results do not look like numbers is dropped
 *
 * Core 1 is given up for the session (until power-off: .noinit) when it does not start, stalls, gets its results
 * wrong three times, or anything crashes while it runs. Core 0 then renders every voice alone. */
#include "engine.h"

#define C1_MAGIC 0x41314A42u             /* "BJ1A" */
#define C1_BAR_MAGIC 0x52414231u         /* the session's verdict: core 1 given up until power-off */
#define C1_STALL_US 3000u                /* a job is ~1-2 ms; core 0 gives up waiting after 3 */

typedef struct {
    volatile uint32_t req, check;        /* core 0's: the job asked for, and its check */
    uint32_t pad0[6];
    volatile uint32_t done, ready, misread, rsv;     /* core 1's, on a line of their own */
    uint32_t pad1[4];
} c1_box_t;
static c1_box_t c1_box __attribute__((aligned(32)));
static struct { uint32_t magic, barred, why; } c1_bar __attribute__((section(".noinit")));
static uint8_t c1_on;                    /* core 1 is up and taking jobs */
static uint32_t c1_seq, c1_faults, c1_jobs, c1_why;
enum { C1_WHY_NONE, C1_WHY_NOSTART, C1_WHY_STALL, C1_WHY_RESULTS, C1_WHY_CRASH, C1_WHY_BARRED, C1_WHY_OFF };

extern uint8_t album_cpu1_stacks_lo[], album_cpu1_stacks_hi[];
extern void album_cpu1_entry(void);

#define C1_REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define C1_SYNC() __asm__ volatile("csync" ::: "memory")

static inline __attribute__((always_inline)) uint32_t c1_load(volatile uint32_t *p)
{
    uint32_t v = *p;
    C1_SYNC();
    return v;
}
static inline __attribute__((always_inline)) void c1_store(volatile uint32_t *p, uint32_t v)
{
    C1_SYNC();
    *p = v;
    C1_SYNC();
}

/* hold core 1: registers only (app_cstart calls it before the RAM is set up) */
static inline __attribute__((always_inline)) void c1_hold(void)
{
    uint32_t locked = !(C1_REG(0x1EEE240u) & 1u), en, mask = 0x003F0030u;
    volatile uint32_t n;
    __asm__("" : "+r"(mask));            /* (the build's ROM-address check would read ~mask as a ROM address) */
    if (locked)
        C1_REG(0x1EEE240u) = 0xE7u;      /* the debug unit's write lock open */
    en = C1_REG(0x1EEE340u);
    C1_REG(0x1EEE340u) = en & ~mask;     /* the bus traps masked */
    C1_SYNC();
    C1_REG(0x1EEE004u) = C1_REG(0x1EEE004u) | 2u;    /* held */
    C1_REG(0x1EEE004u) = C1_REG(0x1EEE004u) & ~8u;
    C1_SYNC();
    for (n = 0; n < 256u; n++)            /* the late flag arrives */
        ;
    C1_REG(0x1EEE248u) = mask;           /* and is cleared */
    C1_SYNC();
    C1_REG(0x1EEE340u) = en;
    if (locked && (C1_REG(0x1EEE240u) & 1u))
        C1_REG(0x1EEE240u) = 0xE7u;
    C1_SYNC();
}

/* core 1: wait for a job, check it, do it, say so. In RAM, all of it but the job itself (which runs only while
 * core 0 waits inside the audio interrupt, the flash on) */
void __attribute__((section(".c1_text"), noinline, noreturn, used)) album_cpu1_main(void)
{
    uint32_t last = 0, lo = (uint32_t)(uintptr_t)album_cpu1_stacks_lo + 256u,
             hi = (uint32_t)(uintptr_t)album_cpu1_stacks_hi - 1u;
    C1_REG(0x1EEF2D8u) = hi;             /* its own stack limits, as core 0 has */
    C1_REG(0x1EEF2DCu) = lo;
    C1_REG(0x1EEF2E0u) = hi;
    C1_REG(0x1EEF2E4u) = lo;
    C1_REG(0x1EEF2D0u) = (C1_REG(0x1EEF2D0u) & ~((1u << 2) | (0x1Fu << 16))) | (1u << 3);
    c1_store(&c1_box.ready, 1u);
    for (;;) {
        uint32_t r = c1_load(&c1_box.req);
        if (r == last)
            continue;                    /* (reads only while it waits: no traffic on the bus for core 0) */
        if (r != last + 1u || c1_load(&c1_box.check) != (r ^ C1_MAGIC)) {   /* misread: look again */
            c1_box.misread++;
            continue;
        }
        last = r;
        eng_voices_b();
        c1_store(&c1_box.done, r);
    }
}

static void c1_retire(uint32_t why)
{
    if (!c1_on && why != C1_WHY_NOSTART)
        return;
    c1_hold();
    c1_on = 0;
    c1_why = why;
    c1_bar.magic = C1_BAR_MAGIC;
    c1_bar.barred = 1;
    c1_bar.why = why;
    eng_voice_cap(8);                    /* one core: fewer voices, the same sound */
}

static int c1_start(void)
{
    uint32_t i, div, t0;
    if (c1_bar.magic == C1_BAR_MAGIC && c1_bar.barred) {
        c1_why = C1_WHY_BARRED;
        eng_voice_cap(8);
        return 0;
    }
    c1_hold();
    c1_store(&c1_box.ready, 0u);
    c1_store(&c1_box.req, 0u);
    c1_store(&c1_box.check, C1_MAGIC);
    c1_store(&c1_box.done, 0u);
    c1_seq = 0;
    for (i = 0; i < 32u; i++)            /* its interrupt bank: nothing (it runs polled) */
        C1_REG(0x1EEF300u + 4u * i) = 0u;
    C1_REG(0x01C7FFF8u) = (uint32_t)(uintptr_t)album_cpu1_entry;
    div = C1_REG(0x10008u);
    C1_REG(0x10008u) = div | 8u;
    C1_SYNC();
    C1_REG(0x1EEE004u) = C1_REG(0x1EEE004u) | 8u;
    C1_REG(0x1EEE004u) = C1_REG(0x1EEE004u) & ~2u;
    t0 = plat_ticks24();
    while (!c1_load(&c1_box.ready) && plat_ticks24() - t0 < 10000u * PLAT_TICKS_PER_US)
        ;
    C1_REG(0x10008u) = div;
    C1_SYNC();
    if (!c1_load(&c1_box.ready)) {
        c1_retire(C1_WHY_NOSTART);
        return 0;
    }
    c1_on = 1;
    return 1;
}

/* core 0, in the render: core 1 takes its half (0: it is not up, core 0 does it) */
static int c1_kick(void)
{
    if (!c1_on)
        return 0;
    c1_seq++;
    c1_store(&c1_box.check, c1_seq ^ C1_MAGIC);
    c1_store(&c1_box.req, c1_seq);
    return 1;
}

/* core 0: wait for core 1's half. 1 = in and sane; 0 = not (the half is dropped; core 1 may be given up) */
static int c1_join(void)
{
    uint32_t t0 = plat_ticks24();
    while (c1_load(&c1_box.done) != c1_seq)
        if (plat_ticks24() - t0 > C1_STALL_US * PLAT_TICKS_PER_US) {
            c1_retire(C1_WHY_STALL);
            eng_drop_b();
            return 0;
        }
    c1_jobs++;
    if (!eng_b_sane()) {
        eng_drop_b();
        if (++c1_faults >= 3u)
            c1_retire(C1_WHY_RESULTS);
        return 0;
    }
    return 1;
}

void app_cstart(void)                    /* first thing after any reset: core 1 held until we start it */
{
    c1_hold();
}

void app_fault(const fm1_crash_t *c)     /* a crash: core 1 held, and one core until power-off */
{
    (void)c;
    if (c1_on)
        c1_retire(C1_WHY_CRASH);
}

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
static uint32_t c1_bus_en;               /* the bus traps as they were before core 1 started */
static uint8_t c1_bus_masked;            /* and masked meanwhile (c1_start, c1_bus_back) */
static uint8_t c1_said_why;              /* the failure that turned two cores off at this boot (app_init) */
static uint8_t c1_wait;                  /* two cores wanted, held until the radio's start is over (app.c c1_radio) */

/* the trace: how far the start got, and how many jobs core 1 finished after, in RAM that a crash's restart keeps
 * (.noinit). A crash with two cores leaves it for the next start to show on the screen (app.c), then it is cleared.
 * Steps: 1 held, 2 mailbox set, 3 interrupt bank cleared, 4 entry address written, 5 clock divider set, 6 core 1
 * released, 7 core 1 answered from its own code; 8 its first job done and sane */
#define C1_TRACE_MAGIC 0x54523143u       /* "C1RT" */
static struct { uint32_t magic, step, jobs, crashed, sum; } c1_trace __attribute__((section(".noinit")));
/* core 1 writes this first thing in its own code: a crash at step 6 then says whether it got that far */
#define C1_IN_MAGIC 0x494E3143u          /* "C1NI" */
static volatile uint32_t c1_in __attribute__((section(".noinit")));
#define C1_TRACE_SUM() (c1_trace.magic ^ c1_trace.step * 0x9E3779B1u ^ c1_trace.jobs * 0x85EBCA6Bu ^ c1_trace.crashed ^ 0x5A5AA5A5u)
static void c1_step(uint32_t s)
{
    c1_trace.magic = C1_TRACE_MAGIC;
    c1_trace.step = s;
    c1_trace.crashed = 0;
    if (s <= 7u)
        c1_trace.jobs = 0;
    c1_trace.sum = C1_TRACE_SUM();
}
static uint32_t c1_seq, c1_faults, c1_jobs, c1_why;
enum { C1_WHY_NONE, C1_WHY_NOSTART, C1_WHY_STALL, C1_WHY_RESULTS, C1_WHY_CRASH, C1_WHY_BARRED, C1_WHY_OFF };

extern uint8_t album_cpu1_stacks_lo[], album_cpu1_stacks_hi[];
static void (*volatile c1_job)(void) = eng_voices_b;   /* core 1's job, called through this (see album_cpu1_main) */
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
    c1_in = C1_IN_MAGIC;                 /* (got here: see c1_report) */
    (void)lo;                            /* (its own stack limits, 0x1EEF2D0.., are left off: one unknown fewer
    (void)hi;                             * while its crashes are chased) */
    c1_store(&c1_box.ready, 1u);
    for (;;) {
        uint32_t r = c1_load(&c1_box.req);
        if (r == last)
            continue;                    /* (reads only while it waits: no traffic on the bus for core 0) */
        if (r != last + 1u || c1_load(&c1_box.check) != (r ^ C1_MAGIC)) {   /* misread: look again */
            c1_box.misread++;
            continue;
        }
        {   /* the job is in the flash, too far for a call from RAM: through a pointer, read until it reads right
             * (a misread pointer called is a crash) */
            void (*job)(void) = c1_job;
            if (job != eng_voices_b) {
                c1_box.misread++;
                continue;                /* (not taken: the next look reads it again) */
            }
            last = r;
            __asm__("" : "+r"(job));     /* (the compiler must not turn this back into a direct call) */
            job();
        }
        c1_store(&c1_box.done, r);
    }
}

/* core 1 held: the bus traps back as they were, once the hold's own late flag has come and gone */
static void c1_bus_back(void)
{
    uint32_t t0, bmask = 0x003F0030u;
    __asm__("" : "+r"(bmask));
    if (!c1_bus_masked)
        return;
    t0 = plat_ticks24();
    while (plat_ticks24() - t0 < 2000u * PLAT_TICKS_PER_US)
        ;
    fm1__dbg_unlock();
    FM1_DBG_MSG_CLR = bmask;
    C1_SYNC();
    FM1_DBG_EN = c1_bus_en;
    C1_SYNC();
    fm1__dbg_lock();
    c1_bus_masked = 0;
}

static void c1_retire(uint32_t why)
{
    if (!c1_on && why != C1_WHY_NOSTART)
        return;
    c1_hold();
    c1_bus_back();
    c1_on = 0;
    c1_why = why;
    if (why != C1_WHY_OFF) {             /* (one core asked for, OCT- held: nothing wrong, nothing barred) */
        c1_bar.magic = C1_BAR_MAGIC;
        c1_bar.barred = 1;
        c1_bar.why = why;
    }
    eng_voice_cap(8);                    /* one core: fewer voices, the same sound */
}

/* OCT+ held on the boot screen: a deliberate try, so an earlier verdict is set aside (the bar lives in .noinit,
 * which the FM-1's power switch does not always clear: once barred, every later try was refused unseen) */
static void c1_unbar(void)
{
    c1_bar.magic = 0;
    c1_bar.barred = 0;
}

/* why two cores are not running, for the screen's second line */
static const char *c1_fail_text(void)
{
    switch (c1_why) {
    case C1_WHY_NOSTART: return c1_in == C1_IN_MAGIC ? "NO ANSWER (IN)" : "NO ANSWER (ROM)";
    case C1_WHY_STALL: return "STALLED";
    case C1_WHY_RESULTS: return "BAD RESULTS";
    case C1_WHY_CRASH: return "CRASHED";
    case C1_WHY_BARRED: return "BARRED";
    default: return "8 VOICES";
    }
}

static int c1_start(void)
{
    uint32_t i, div, t0, bus, top, bmask = 0x003F0030u;
    __asm__("" : "+r"(bmask));           /* (as in c1_hold: kept out of the build's ROM-address check) */
    if (c1_bar.magic == C1_BAR_MAGIC && c1_bar.barred) {
        c1_why = C1_WHY_BARRED;
        eng_voice_cap(8);
        return 0;
    }
    c1_hold();
    c1_step(1u);
    c1_store(&c1_box.ready, 0u);
    c1_store(&c1_box.req, 0u);
    c1_store(&c1_box.check, C1_MAGIC);
    c1_store(&c1_box.done, 0u);
    c1_seq = 0;
    c1_step(2u);
    for (i = 0; i < 32u; i++)            /* its interrupt bank: nothing (it runs polled) */
        C1_REG(0x1EEF300u + 4u * i) = 0u;
    c1_step(3u);
    /* the entry word sits in the reserved top of RAM, which fm1_guard write-protects for this core
     * once the vectors are in (main_fm1.c): open it for the one write, then close it again */
    top = FM1_C0_WR_LIMIT_EN & 4u;       /* locked now? (at power-on, app_init runs before the platform locks it:
                                          * locking it here then stopped audio_init's vector write, every boot) */
    if (top)
        fm1_guard_unlock_top();
    C1_REG(0x01C7FFF8u) = (uint32_t)(uintptr_t)album_cpu1_entry;
    C1_SYNC();
    if (top)
        fm1_guard_lock_top();
    c1_step(4u);
    div = C1_REG(0x10008u);
    C1_REG(0x10008u) = div | 8u;
    C1_SYNC();
    c1_step(5u);
    c1_in = 0;
    /* The debug unit's guards are shared by both cores. Released, core 1 starts in the chip's boot ROM
     * (0x01E00000..) and reads the entry word there: the PC window of fm1_guard (XIP and .ram_text only) stopped
     * it on its second instruction (crash PC 01E00004). The XIP window now starts at the ROM (between the two is
     * unmapped, which the bus guard still catches). The bus traps are masked over the release itself, as c1_hold
     * does, and their late flag cleared after. */
    fm1__dbg_unlock();
    FM1_PC_LIMIT0_L = 0x01E00000u;
    bus = FM1_DBG_EN;
    FM1_DBG_EN = bus & ~bmask;
    C1_SYNC();
    fm1__dbg_lock();
    C1_REG(0x1EEE004u) = C1_REG(0x1EEE004u) | 8u;
    C1_REG(0x1EEE004u) = C1_REG(0x1EEE004u) & ~2u;
    c1_step(6u);
    t0 = plat_ticks24();
    while (!c1_load(&c1_box.ready) && plat_ticks24() - t0 < 10000u * PLAT_TICKS_PER_US)
        ;
    C1_REG(0x10008u) = div;
    C1_SYNC();
    /* The bus traps stay masked while core 1 runs. Its start (the ROM, the release) raises a bus-invalid flag
     * (DBG_MSG bit 19) that can arrive late: cleared at once and unmasked, it came after and crashed whichever
     * core took it, core 1 idle in its loop (album_cpu1_main+0x64) or core 0 drawing. c1_bus_back puts them back
     * once core 1 is held again. */
    t0 = plat_ticks24();
    while (plat_ticks24() - t0 < 2000u * PLAT_TICKS_PER_US)
        ;                                /* (the late flag arrives) */
    fm1__dbg_unlock();
    FM1_DBG_MSG_CLR = bmask;
    C1_SYNC();
    fm1__dbg_lock();
    c1_bus_en = bus;
    c1_bus_masked = 1;
    if (!c1_load(&c1_box.ready)) {
        c1_retire(C1_WHY_NOSTART);
        return 0;
    }
    c1_step(7u);
    c1_on = 1;
    eng_voice_cap((int)ENG_VOICES);      /* (eight until now, if it ran on one meanwhile) */
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
    if (c1_trace.step == 7u && eng_b_sane())
        c1_trace.step = 8u;
    c1_trace.jobs = c1_jobs;
    c1_trace.sum = C1_TRACE_SUM();
    if (!eng_b_sane()) {
        eng_drop_b();
        if (++c1_faults >= 3u)
            c1_retire(C1_WHY_RESULTS);
        return 0;
    }
    return 1;
}

static void text_cat(char *d, const char *s);   /* (app.c's, further down the unity build) */
static void int_text(int32_t v, char *b);

/* the last start's crash, if core 1 was in it: a, b the screen's two lines; 1 = there is one (then forgotten) */
static int c1_report(char *a, char *b)
{
    char n[12];
    if (c1_trace.magic != C1_TRACE_MAGIC || c1_trace.crashed != 1u || c1_trace.sum != C1_TRACE_SUM()
        || c1_trace.step < 1u || c1_trace.step > 8u || c1_trace.jobs > 100000000u) {
        c1_trace.magic = 0;              /* (nothing, or not whole: said nothing) */
        return 0;
    }
    a[0] = b[0] = 0;
    text_cat(a, "CORE 2 CRASHED");
    if (c1_trace.step >= 8u) {
        text_cat(b, "RUNNING, BLOCK ");
        int_text((int32_t)c1_trace.jobs, n);
    } else {
        text_cat(b, "STEP ");
        int_text((int32_t)c1_trace.step, n);
        text_cat(n, " OF 7");
        if (c1_trace.step == 6u)
            text_cat(n, c1_in == C1_IN_MAGIC ? " IN" : " ROM");   /* core 2 reached its own code, or not */
    }
    text_cat(b, n);
    c1_trace.magic = 0;
    return 1;
}

void app_cstart(void)                    /* first thing after any reset: core 1 held until we start it */
{
    c1_hold();
}

void app_fault(const fm1_crash_t *c)     /* a crash: core 1 held, and one core until power-off */
{
    (void)c;
    if (c1_trace.magic == C1_TRACE_MAGIC && c1_trace.step >= 1u && (c1_on || c1_trace.step < 7u))
    {
        c1_trace.crashed = 1;            /* it happened with core 1 starting or running: the next start says so */
        c1_trace.sum = C1_TRACE_SUM();
    }
    if (c1_on)
        c1_retire(C1_WHY_CRASH);
    else if (c1_trace.crashed)           /* (mid-start: given up all the same) */
        c1_retire(C1_WHY_NOSTART);
}

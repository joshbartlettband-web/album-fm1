/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hausway */
/* Album's supply setup (device only; included by app.c inside the unity build, after the platform, whose fm1_sys.h
 * gives the P33 access). fm1-base raises VDC14 and SYSVDD; this adds the rest of M-VAVE's own start. */
/* ------------------------------------------------- the stock supply setup --- */
/* M-VAVE's V15 does more than raise the two levels as it starts: it hands the 1.3 V supply from its LDO to its
 * DC-DC, sets a handful of analog registers and the SYSVDD drive bit, then VDC14 7 and SYSVDD 15. The sequence
 * as Melodee read it out of FM-1_015 (keremimo/melodee BUILDING.md, "The stock supply setup"; their 1.3.6 and
 * later apply it in every build, and run both cores by default since). Written here from that description.
 * At the levels alone, with the LDO still feeding the core, some units read SRAM wrong with both cores busy:
 * single bit flips, a return address popped as 0x03xxxxxx (Melodee's 03801494, Album's 030063F4).
 * Two departures, both only to avoid a dip on a warm start (the levels already up): register 6 is never written
 * below its current level, and register 5 goes straight to VDDIOM 4 (stock writes 0x40 first, VDDIOM 0 for a moment).
 * Interrupts off, core 1 held. 1: done and read back as stock leaves it; 0: a transfer timed out or a field did
 * not take (core 1 then stays off). */
static uint8_t fm1_supply_regs[4];           /* registers 0, 5, 6, 9 as read back after */
static uint8_t fm1_supply_ok;                /* it ran and read back right (app_init) */
static int fm1__supply_field(uint32_t reg, uint32_t sh, uint32_t w, uint32_t v)
{
    uint32_t t = fm1_p33_timeouts, m = ((1u << w) - 1u) << sh;
    uint8_t old = fm1_p33_read(reg);
    if (fm1_p33_timeouts != t)
        return 0;                            /* (never write back a failed read) */
    fm1_p33_write(reg, (uint8_t)((old & ~m) | ((v << sh) & m)));
    return fm1_p33_timeouts == t;
}
static int fm1_supply_stock(void)
{
    uint32_t t = fm1_p33_timeouts, lvl;
    if (!fm1__supply_field(0x00u, 2, 1, 1))  /* LDO13 on, */
        return 0;
    fm1_delay_us(30);
    if (!fm1__supply_field(0x00u, 3, 1, 1))  /* DCDC13 on, */
        return 0;
    fm1_delay_us(30);
    if (!fm1__supply_field(0x00u, 2, 1, 0))  /* LDO13 off: the DC-DC carries it */
        return 0;
    fm1_delay_us(30);
    lvl = fm1_vdc14();
    if (fm1_p33_timeouts != t)
        return 0;
    fm1_p33_write(0x01u, 0x20u);
    fm1_p33_write(0x05u, 0x44u);
    fm1_p33_write(0x06u, (uint8_t)(0x08u | (lvl > 4u ? lvl : 4u)));
    fm1_p33_write(0x07u, 0xA5u);
    fm1_p33_write(0x08u, 0x96u);
    if (fm1_p33_timeouts != t || !fm1__supply_field(0x09u, 4, 1, 1)   /* SYSVDD drive */
        || !fm1__supply_field(0x05u, 0, 3, 4)                         /* VDDIOM 3.2 V */
        || !fm1__supply_field(0x06u, 0, 3, 7)                         /* VDC14 1.60 V */
        || !fm1__supply_field(0x05u, 4, 2, 3))                        /* VDDIOW 3.2 V */
        return 0;
    fm1_delay_us(100);
    fm1_p33_or(0x00u, 0x80u);
    if (fm1_p33_timeouts != t || !fm1__supply_field(0x16u, 0, 6, 0))
        return 0;
    fm1_p33_or(0x02u, 0x40u);
    fm1_p33_and(0x17u, 0xDFu);
    fm1_sysvdd_raise(15);                    /* SYSVDD 1.38 V last (stock in one write; here a level per 100 us) */
    fm1_supply_regs[0] = fm1_p33_read(0x00u);
    fm1_supply_regs[1] = fm1_p33_read(0x05u);
    fm1_supply_regs[2] = fm1_p33_read(0x06u);
    fm1_supply_regs[3] = fm1_p33_read(0x09u);
    return fm1_p33_timeouts == t && (fm1_supply_regs[0] & 0x0Cu) == 0x08u && (fm1_supply_regs[1] & 0x37u) == 0x34u
           && (fm1_supply_regs[2] & 7u) == 7u && (fm1_supply_regs[3] & 0x1Fu) == 0x1Fu;
}

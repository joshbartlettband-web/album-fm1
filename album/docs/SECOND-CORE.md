# Album's second core: what happened, what others found, where it stands

Written 2026-10-29, after a night of testing on Josh's FM-1 (orange, 360 MHz). It's for whoever picks this up
next: Josh, Claude, or another model.

## Where it stands

- **One core is the default and is stable.** Two cores are opt-in: hold OCT+ on the boot screen. OCT- goes
  back to one core.
- **On Josh's unit the second core starts and runs.** It reported 12 voices and rendered audio. Then it
  crashes within seconds to minutes with a single flipped bit in memory. The latest crash: the main core
  popped a return address of `030063F4` where `020063F4` was stored, in `lcd_sync`, and the PC guard
  (DBG `0x1000`) stopped it.
- **Latest build (untested on hardware):** M-VAVE's full supply setup at boot (`album/cpu1/supply.h`), core 2's
  register setup restored, the bus-trap fixes, the deferred start after Bluetooth, and clearer failure messages.

## What the crash screens told us, in order

| Screen | Meaning | Fix |
|---|---|---|
| step 4, dbg `2000` | wrote core 2's entry word into the write-protected top of RAM | unlock it for that one write |
| step 6, PC `01E00004` | core 2 boots through the chip ROM; the PC window (shared by both cores) refused it | PC window opened to `0x01E00000` |
| boot crash in `fm1_cstart` vector attach | `c1_start` relocked the top of RAM before `audio_init` had written its vectors | relock only if it was locked |
| once a boot, PC in `shim_delay_us` | core 2 running through Bluetooth's high-current calibration | start core 2 after `radio_quiet`; hold it through later starts |
| TWO CORES FAILED at once, every boot | the "until power-off" bar sits in `.noinit`, which the power switch doesn't always clear; OCT- also set it | OCT+ clears it, OCT- doesn't set it, failures say why |
| dbg `80000` (bus), core 2 idle in its loop | the start's late bus flag cleared too early | wait 2 ms, clear, keep bus traps masked while core 2 runs |
| dbg `1000`, PC `030063F4` | **a single bit flipped in a value read from SRAM** | not software: see below |

## What other FM-1 firmware authors found

The same thing, on some units. Their measurements and fixes:

- **Jangada** (zednaked), in [charlesvestal/fm1-x0x#10](https://github.com/charlesvestal/fm1-x0x/issues/10)
  and [keremimo/melodee#17](https://github.com/keremimo/melodee/issues/17):
  - With core 1 running, some FM-1s read SRAM wrong: single-bit flips, mostly bits 20 and 21, more under audio,
    screen and USB load. The same image with core 1 never started is stable.
  - On their unit, raising VDC14 (`P3_ANA_CON6`) mattered most. 15/5 (SYSVDD 1.38 V, VDC14 1.50 V) ran 15
    minutes, 1.23 M blocks, with 0 misreads.
  - A misread counter that goes quiet can mean core 1 died; check it still answers.
- **Melodee** (keremimo), BUILDING.md "Units whose CPU1 misreads SRAM" and "The stock supply setup":
  - Its crash `00000001 03801494 00000000 00001000 0201257A` is the same as ours: core 0 popped a return
    address with a bit set. "No software check can catch that."
  - Their fix: replay M-VAVE V15's whole supply startup. That means the LDO13 to DCDC13 handover, analog
    registers 1, 5, 6, 7, 8, the SYSVDD drive bit (reg 9 bit 4), VDDIO 3.2 V, reg 0 bit 7, reg 0x16, reg 2
    bit 6 and reg 0x17 bit 5, then VDC14 7 and SYSVDD 15.
  - Since 1.3.6 every Melodee build does this. Dual core is the default, and single-core packages are retired.
  - Melodee also notes that M-VAVE's stock firmware is **single-core** (`CPU_CORE_NUM 1`), so stock behavior
    doesn't prove dual-core works at 360 MHz.
  - Their maintainer warns against going above stock voltages.
- **fm1-base** (Album's platform) raises VDC14 to 7 and SYSVDD to 15, but does not do the handover or the other
  registers. Album now does them itself, in `album/cpu1/supply.h`.
- **The 320 MHz probe** ([curist/fm1-clk320-probe](https://github.com/curist/fm1-clk320-probe)):
  - On a unit where every 360 MHz run failed, 320 MHz (JieLi's rated maximum) with no voltage change ran a
    30-minute soak with 0 bad reads.
  - It also saw screen activity change the failure rate. Most of Josh's crashes landed while the screen was
    drawing (`lcd_sync`).
  - Risk: Melodee's 1.3.0 retune to 320 put some units into UBOOT behind a black screen; the probe is more
    careful.
  - M-VAVE's newer V16 boot loader starts the chip at 320 MHz, but fm1-base sets it back to 360.
- **Felucca** 1.6.1 ([PR #247](https://github.com/hugelton/Felucca/pull/247)) ships two cores off by default
  ("2ND CORE"). On a misread it gives up and saves OFF ("2ND CORE MISREAD: ONE CORE").
- **X0X's three hardening fixes** (#10): hold core 1 before the `.bss` clear (Album does), run core 1's own
  faults on core 1, and set core 1's EMU stack limits and `EMU_CON` (restored in Album).

## Next steps, cheapest first

1. **Test the stock supply build.**
   - Hold OCT+ and play hard with the screen busy.
   - If the boot screen says SUPPLY NOT SET, the sequence didn't read back as stock leaves it. Core 2 stays
     off, and the register values are in `fm1_supply_regs`.
2. **If it still flips bits, keep core 2 idle while the LCD DMA runs.**
   - Core 0 renders those blocks alone.
   - If the crashes stop, screen load is the trigger.
3. **If it still flips bits, try 320 MHz while two cores run.**
   - Port the probe's switch (MIT).
   - Risk of UBOOT on some units, so make it opt-in and power-cycle after install.
4. **Otherwise, accept it.**
   - Like Felucca and Melodee: two cores where the unit allows, one core and a clear message where it doesn't.
   - Album already does this.

## Files

- `album/cpu1/cpu1.c`: the start, the job loop, the bar, the trace, the failure messages
- `album/cpu1/entry.S`: the core 1 entry stub and its stacks
- `album/cpu1/supply.h`: M-VAVE's supply setup
- `album/app.c` `c1_radio`: start after Bluetooth, hold through later starts
- `album/tools/addr2sym.py`, `crash-addresses.txt`, `crash-disasm.txt`: crash screen to function names in CI
  (notice "Crash addresses"); `sym NAME` in `crash-disasm.txt` dumps one function

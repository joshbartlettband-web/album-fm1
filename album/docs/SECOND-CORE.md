# Album's second core: what happened, what others found, where it stands

Written 2026-10-29, after a night of testing on Josh's FM-1 (orange, 360 MHz). Updated 2026-11-12, when Album moved
to fm1-base's second core and lost its Bluetooth. It's for whoever picks this up next: Josh, Claude, or another
model.

## Where it stands

- **Two cores are the default since 2026-11-12** (Josh's call, as in Album 02), one core the fallback. OCT- on
  the boot screen chooses one core; OCT+ goes back to two, with a restart. The setting is `one_core` in the saved
  settings, which was 0 in every earlier install, so existing installs get two cores without losing their settings.
- **One core is stable.** It ran every build until 0.1.63.
- **0.1.69 on Josh's FM-1 (2026-11-12): both cores ran, then it crashed after a while.** That build had twelve
  voices with two cores. No crash screen or message was noted. Since then two cores start with eight voices, four
  a core, and the most voices is the player's choice (EDIT held and OCT+ / OCT-, six to twelve), so the load can
  be raised a step at a time on the FM-1 itself. What isn't known yet: whether the crash was the load (the render
  running out of time), a misread, or something else. The next crash screen, or what the boot screen says after it,
  decides that.
- **Album 02 runs two cores on Josh's FM-1.** It uses fm1-base's second core (`cpu2 = true`,
  `cpu2_fallback = true`), started in `app_init`, and has no Bluetooth (`ble = false`). Album 01 now does the same:
  - `album/fm1.toml` has the same three settings. Album's own `cpu1/cpu1.c`, `entry.S` and `supply.h` are gone.
  - The second core is felucca-bsp's `chip/fm1_cpu1.h`: Melodee's start, its check word on every job, and its
    fault path.
  - It starts in `app_init`, at power-on, before the audio and the timers. Melodee, X0X and LOOM-8 start it
    there. Album's old start came after Bluetooth, with the audio running; nobody else has tested a late start.
  - So OCT+ (from one core) saves the choice and restarts the FM-1.
  - A failure doesn't restart: the core is held, its half of that block is dropped, one core is saved, and the
    screen says why (the manual lists the messages).
  - **Album 01's build of this is untested on hardware as of this writing.**
- **With the old start, on Josh's unit, the second core started and ran.** It reported 12 voices and rendered
  audio. Then it crashed within seconds to minutes with a single flipped bit in memory. The last crash: the main
  core popped a return address of `030063F4` where `020063F4` was stored, in `lcd_sync`, and the PC guard
  (DBG `0x1000`) stopped it.
- **The supply:** fm1-base 33cb407 raises VDC14 and SYSVDD to M-VAVE's levels and moves the 1.3 V supply onto its
  DC-DC at every start (`fm1_power_stock`). Album adds nothing to it, as Album 02 doesn't.
- **Bluetooth is out** (0.1.63 was the last build with it). The radio's calibration at power-on draws about half a
  second of high current; with the old start it crashed once a boot when the second core ran through it.

## What the crash screens told us, in order (the old start)

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
  registers (as of 2026-10-29; since 33cb407 it does the handover and most of the registers).
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

1. **Test this build.**
   - The boot screen should say TWO CORES, 8 VOICES. Play hard with the screen busy, then raise the voices a
     step at a time (EDIT held and OCT+) and watch CPU and PEAK.
   - Note what the screen says if it gives up, and hold EDIT for the stack reading.
   - A crash screen: photograph it. The addresses go in `album/tools/crash-addresses.txt` for CI to name.
2. **Watch the reports.** Two cores are on for everyone now. A unit that misreads falls back to one core by
   itself; a report of repeated crashes at boot would be the reason to go back to opt-in.
3. **If it still flips bits, try 320 MHz while two cores run.**
   - M-VAVE's own V16 boot loader and m-bootloader start the chip at 320 MHz; fm1-base sets it back to 360
     (`fm1_clock_stock`). On one unit where every 360 MHz run failed, the probe's 30-minute soak at 320 passed.
   - The cheap way: the system clock's source 2 instead of 6 (`CLK_SYS_CON`, felucca-bsp `chip/fm1_clock.h`). The
     peripherals' clock then falls from 60 to 53 MHz, so the TRS MIDI rate, the LCD's SPI and the 10 kHz tick need
     checking. The probe's way retunes the PLL and the flash's clock too.
   - Risk: Melodee's retune to 320 put some units into UBOOT behind a black screen. Make it opt-in and
     power-cycle after install.
   - Everything gets about 11 % slower, so twelve voices may not all fit.
4. **Otherwise, accept it.** Like Felucca and Melodee: two cores where the unit allows, one core and a clear
   message where it doesn't. Album already does this.

## Files

- `album/fm1.toml`: `cpu2`, `cpu2_fallback`, `ble = false`
- `album/app.c`: `c2_job` (the second core's half), `app_render` (hand over, wait, check), `app_init` (the start),
  `c2_watch` (a failure said and saved; the restart after OCT+)
- `base/bsp/chip/fm1_cpu1.h`, `base/device/cpu2.c`: the second core itself
- `album/tools/addr2sym.py`, `crash-addresses.txt`, `crash-disasm.txt`: crash screen to function names in CI
  (notice "Crash addresses"); `sym NAME` in `crash-disasm.txt` dumps one function

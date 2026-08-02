# Cross-Cutting Improvements & Bugs — Pico Console

**Scope:** the two shipping MVP firmwares — `spaceShipGame` (783-line `Core/Src/main.c`)
and `EmbeddedPaint` (691-line `Core/Src/main.c`) — plus the greenfield **Snake**
cartridge and the **console shell** that integrate them. This is the *consolidated,
self-contained* cross-repo bug and improvement register. Fuller per-game detail also
lives in each game repo at `docs/IMPROVEMENTS-AND-BUGS.md`
(`/home/user/spaceShipGame/docs/IMPROVEMENTS-AND-BUGS.md` and
`/home/user/EmbeddedPaint/docs/IMPROVEMENTS-AND-BUGS.md`); everything needed to act
on the issues is summarized here so this file stands alone.

> **Framing — read this first.** Both games are **proof-of-concept "cartridges"**:
> each one independently proves a hard real-time path (analog joystick → game logic →
> ST7735 over SPI) works end-to-end on the metal. The MVP scope was deliberately
> *one game per firmware image*. The gaps below are **not** hidden defects — they are
> the precise list of work the always-planned **console** (shared platform → cartridge
> contract → shell → persistence → perf → RP2350 port) closes. Every gap is tagged
> with the named release tier (**T1–T7**, from `CONSOLE-PRD.md` §10) that resolves it.
> Where a gap is a genuine code bug that ships today, it is marked as such and
> separated from *design-intent* mismatches and *roadmap* items. I never claim a
> capability the code lacks: **as of today there is no PLL, no DMA, no double-buffer,
> no game-exit path, and no shared driver.** Those are Full-Release additions.

---

## 1. Severity-ranked master table

Severity: **S1** = wrong/broken behavior or resource leak that bites in normal use ·
**S2** = correctness/robustness hazard or significant perf ceiling · **S3** = polish,
consistency, hygiene. Type: **Bug** (ships wrong today) · **Design-gap** (MVP scope
boundary the roadmap closes) · **Intent-mismatch** (code contradicts stated design) ·
**Hygiene** (dead/confusing code or stale config).

| ID | Area | Title | Sev | Type | Closed by |
|----|------|-------|-----|------|-----------|
| **X1** | Cross | No shared HAL — two copy-pasted, **divergent** ST7735/ADC/GPIO drivers | S2 | Design-gap | **T1** |
| **X2** | Cross | **Endianness disagreement** between the two drivers (`{lo,hi}` vs `{hi,lo}`) | S2 | Bug (latent) | **T1** |
| **X3** | Cross | No game-exit / no shell — each game **owns** `main()`+`while(1)`; can't return | S1 | Design-gap | **T2/T3** |
| **X4** | Cross | **`.ioc` drift** — hand-edited C (ADC 2-conv, 144-cyc) vs stale CubeMX (1-conv, 3-cyc) | S2 | Hygiene/Risk | **T1** |
| **X5** | Cross | **16 MHz / no-PLL** clock + **no-DMA** byte-at-a-time SPI = the perf ceiling | S2 | Design-gap | **T6** |
| **X6** | Cross | **Unseeded `rand()`** — identical sequence every power-up (spaceShip) | S2 | Bug | **T1** |
| **X7** | Cross | Geometry/`#define` inconsistencies (logical size ≠ address-window size) | S2 | Bug | **T1** |
| **X8** | Cross | Brittle 2-axis ADC read pattern (1 start, 2 back-to-back gets) — axes can swap | S2 | Bug | **T1** |
| **X9** | Cross | **Axis-mapping contradiction** in docs/comments (main.c says X=PA1 vs pin_layout says A1=Y) | S2 | Intent-mismatch | **T1** |
| **SS1** | Spaceship | **Score = survival ticks**, not dodges (`score += 1` every loop) | S1 | Intent-mismatch | **T2/T4** |
| **SS2** | Spaceship | **Unbounded heap growth** — `malloc` per spawn, list never shrinks in play | S1 | Bug | **T1/T4** |
| **SS3** | Spaceship | **Unbounded difficulty** — `Delay -= 1` with no floor → 0/negative | S2 | Bug | **T2** |
| **SS4** | Spaceship | Slow render — full erase+redraw per star, byte-at-a-time SPI | S2 | Design-gap | **T1/T6** |
| **SS5** | Spaceship | Geometry mismatch — `LCD_Fill` writes 80×260 into a 128×160 window (overspill) | S2 | Bug | **T1** |
| **SS6** | Spaceship | Magic offsets / off-screen score text (`DrawString(90,35,…)`) | S3 | Hygiene | **T1/T2** |
| **EP1** | Paint | **Indirect render** — `paint()` writes only the buffer; pixels appear when cursor leaves | S1 | Intent-mismatch | **T1/T2** |
| **EP2** | Paint | **Per-pixel restore** — `LCD_Restore_Area` does 100 single-pixel windowed SPI writes | S2 | Design-gap | **T1/T6** |
| **EP3** | Paint | **Cursor color argument ignored** — `LCD_courser` always draws black | S3 | Bug | **T2** |
| **EP4** | Paint | Prototype/definition **arg-name swap** (`paint(y,x)` proto vs `paint(x,y)` def) | S3 | Hygiene | **T1** |
| **EP5** | Paint | **Stray root `main.c`** (`//test`) shadows real entry point | S3 | Hygiene | **T1** |
| **EP6** | Paint | **Double `#include "main.h"`** | S3 | Hygiene | **T1** |
| **EP7** | Paint | **Dead `loadColorToArray` prototype** (declared, never defined) | S3 | Hygiene | **T1** |
| **EP8** | Paint | Missing features — undo, save/load, flood-fill, brush size, eraser, larger palette | — | Design-gap | **T5+** |
| **CM1** | Cross | Global/local **variable shadowing** (`old_x/old_y`, `deadzone`) in both games | S3 | Hygiene | **T1** |

> **The throughline:** every row maps to a named tier. The MVP intentionally stopped at
> "two games that each work standalone"; the console is the integrating feature that was
> always next.

---

## 2. Shared / cross-cutting issues (detail)

These are the issues that exist *because there are two firmwares and no shared spine*.
They are the strongest part of the story: each one is a concrete artifact of the
copy-paste-two-cartridges MVP, and each is closed by a specific platform tier.

### X1 — No shared HAL: two copy-pasted, divergent drivers `[Design-gap → T1]`

Each game carries its **own private copy** of the ST7735 driver (`LCD_Init`,
`LCD_CommandMode`/`LCD_DataMode`, `LCD_Fill`, address-window helpers), its **own** ADC
read loop, and its **own** GPIO bit-bang of CS/DC/RES. Verified: both `main.c` files
define byte-identical-in-spirit `LCD_Init()` (same `0x01/0x11/0x3A→0x05/0x21/0x29`
sequence, both annotated `//GEMINI`), `LcdOpen`/`Lcdclose`, and the same 8-sample ADC
averaging loop. There is **no shared header, no shared `.c`, no driver library** — the
two implementations have already *diverged* (see X2), which is exactly the failure mode
a single driver prevents.

- **Why it's the central problem:** a 3-game console cannot have each game own the
  hardware. The shell must own the panel and the ADC, expose them as services, and let
  cartridges call a *contract*, not re-implement HAL.
- **Confirmed from code:** yes — two independent driver copies, observable divergence.
- **Closed by T1 (Platform):** extract one `display`/`input`/`timing` service; both
  games (and Snake) call the same driver. Reconciling the two copies *forces* the
  endianness and geometry bugs (X2, X7) to be resolved in one place.

### X2 — Endianness disagreement between the two drivers `[Bug (latent) → T1]`

The two copy-pasted drivers serialize RGB565 **in opposite byte order**:

| Firmware | Pixel byte order (verified) | Where |
|----------|------------------------------|-------|
| `spaceShipGame` | `{color & 0xFF, color >> 8}` — **low byte first** | `LCD_Fill` L532, `DrawPixel` L563, `LCD_courser` L711 |
| `EmbeddedPaint` | `{color >> 8, color & 0xFF}` — **high byte first** | `LCD_Fill` L506, `LCD_OnSetup_color_choice` L531, `LCD_Restore_Area` L651 |

Each firmware is *internally* consistent enough to look right on its own panel (and
spaceShip's most-used color, `0xFFFF` white, is byte-order-immune, which is why the bug
hid). But the two cannot share a driver until one byte order wins. This is a **latent
bug** that becomes an **active bug** the moment the drivers are merged — and that merge
is T1. There's even a *third* inconsistency inside spaceShip itself: `PrintStars` builds
its star color as `{0xFFFF >> 8, 0xFF & 0xFF}` (high-first) while `LCD_Fill` uses
low-first — harmless only because the value is `0xFFFF`.

- **Confirmed from code:** yes, both directions cited above.
- **Closed by T1:** pick one byte order in the single shared driver (ST7735 in 16-bit
  mode wants high byte first); delete the other.

### X3 — No game-exit, no shell: each game owns the whole firmware `[Design-gap → T2/T3]`

Both `main()` functions are the classic embedded shape: `HAL_Init` → clocks → peripheral
init → `while(1)` that *never returns*. There is **no menu, no dispatcher, no way to
leave a game**. To play the other game today you reflash the board. Verified: neither
`main.c` has any concept of a host shell, a return code, or a teardown.

- **Why it's central:** a console *is* the shell. This is THE architectural pivot —
  turning two firmware images into two **cartridges** that a shell loads and unloads.
- **Closed by T2 (Cartridges)** + **T3 (Shell):** refactor each game into a lifecycle
  contract — `init()` / `update(input, dt)` / `render()` / `teardown()` returning
  control to a shell — and build the game-select dashboard + dispatcher that owns
  `main()`/`while(1)` and the hardware, and that the cartridge returns to on exit.

### X4 — `.ioc` drift: hand-edited C vs stale CubeMX config `[Hygiene/Risk → T1]`

The generated ADC init was **hand-edited** to make the 2-axis read work, but the `.ioc`
was never updated to match. Verified in **both** repos:

| Setting | Hand-edited `main.c` (ships) | Stale `.ioc` (CubeMX source) |
|---------|------------------------------|------------------------------|
| Conversions | `NbrOfConversion = 2` | `ADC1.NbrOfConversionFlag=1` |
| Sample time | `ADC_SAMPLETIME_144CYCLES` | `ADC1.SamplingTime…=ADC_SAMPLETIME_3CYCLES` |

A real embedded-workflow hazard: **anyone who opens the `.ioc` in CubeMX and regenerates
will silently clobber the working two-channel scan** (revert to 1 conversion, 3-cycle
sample) and the firmware will read only one axis. This is the kind of trap that costs an
afternoon.

- **Confirmed from code+config:** yes — exact mismatch shown above; `.ioc` also confirms
  `Mcu.Name=STM32F411R(C-E)Tx`, `APB1/APB2Freq_Value=16000000`, `SPI1.CalculateBaudRate=8.0 MBits/s`.
- **Closed by T1:** either reconcile the `.ioc` to the hand edits (regenerate once,
  cleanly) or commit to maintaining peripheral init by hand and document that the `.ioc`
  is reference-only. The platform extraction is the natural moment to do this.

### X5 — 16 MHz / no-PLL clock + no-DMA SPI: the performance ceiling `[Design-gap → T6]`

This is the headline **"I profiled it and found the bottleneck"** story, and it is fully
grounded in the shipping code:

- **Clock (verified, both repos):** `SystemClock_Config()` uses `RCC_OSCILLATORTYPE_HSI`,
  `RCC_PLL_NONE`, `FLASH_LATENCY_0`, all dividers `/1` → **SYSCLK = HCLK = APB1 = APB2 =
  16 MHz**. The STM32F411 ceiling is 100 MHz, so the board runs at **~16% of its rated
  speed**. The `.ioc` even *computes* `PLLCLKFreq_Value=96000000` — the PLL path was
  costed but left disabled.
- **SPI (verified):** `SPI_BAUDRATEPRESCALER_2` of the 16 MHz APB2 = **8 MHz SCK**, and
  every pixel is shipped by a **blocking `HAL_SPI_Transmit(..., HAL_MAX_DELAY)` of 2
  bytes** — there is **no DMA**, no double-buffer, no dirty-rect. A full 128×160 fill is
  20 480 individual blocking 2-byte transfers.

The single biggest, lowest-risk win is: **enable PLL (→100 MHz) + move the SPI panel
writes onto DMA.** That's roughly a 6× clock headroom *and* frees the CPU during pixel
streaming. It's the cleanest "measured before/after frame time" demonstration in the
whole project.

- **Confirmed from code:** yes — clock config and per-pixel blocking SPI both quoted.
- **Honest scope note:** today there is **no PLL, no DMA, no 100 MHz operation, no
  double-buffer.** Those are **T6** additions, not current capability.
- **Closed by T6 (Perf):** enable PLL, add DMA SPI, measure frame time before/after.

### X6 — Unseeded `rand()` `[Bug → T1]`

`spaceShipGame` calls `rand()` for star X positions and spawn timing (`rand() % 10`,
`rand() % 85`) but **never calls `srand()`**. Verified: no `srand` anywhere in the file.
Result: the obstacle pattern is **bit-identical on every power-up** — the "random" game
is deterministic. (EmbeddedPaint uses no RNG.)

- **Confirmed from code:** yes.
- **Closed by T1:** a single shared **seeded RNG** service (seed from a free-running
  timer / ADC noise at boot). This is also the service Snake depends on for fair food
  spawns — one fix, three consumers.

### X7 — Geometry / `#define` inconsistencies `[Bug → T1]`

The logical screen constants don't match the actual address windows:

- **Spaceship:** `#define LCD_WIDTH 80`, `LCD_HEIGHT 260`, but `LCD_Fill` programs a
  column window `0x00..0x7F` (128) and row window `0x00..0x9F` (160). So `LCD_Fill`
  loops `80*260 = 20 800` transmits into a `128*160 = 20 480`-pixel window — a
  **320-pixel overspill** past the addressed region (SS5). The `#define`s describe a
  portrait sub-window that the code doesn't actually use.
- **Paint:** `#define LCD_WIDTH 128`, `LCD_HEIGHT 160` — correct and matching its
  windows — but it's a *different* convention from spaceShip for the *same panel*.

Two firmwares for the same 128×160 ST7735 disagree on what "width" and "height" mean.
A shared driver must define this **once**.

- **Confirmed from code:** yes — `LCD_WIDTH/HEIGHT` defines vs `0x7F/0x9F` windows quoted.
- **Closed by T1:** single panel geometry in the shared driver; pick rotation/offsets
  once (`X_OFFSET 1`, `Y_OFFSET 26` already hint at the intended sub-window).

### X8 — Brittle 2-axis ADC read pattern `[Bug → T1]`

The joystick read does **one** `HAL_ADC_Start` then **two** back-to-back
`PollForConversion` + `GetValue` calls, relying entirely on the scan sequencer
delivering rank-1 (X) then rank-2 (Y) in order:

```c
HAL_ADC_Start(&hadc1);
HAL_ADC_PollForConversion(&hadc1, 5);  x_sum += HAL_ADC_GetValue(&hadc1);
HAL_ADC_PollForConversion(&hadc1, 5);  y_sum += HAL_ADC_GetValue(&hadc1);
```

If a conversion is missed (timeout = 5 ms) or the sequence slips, **the axes swap** with
no detection. There's no EOC-per-rank check, no per-channel addressing. It works, but
it's fragile.

- **Confirmed from code:** yes (identical pattern in both `main.c`, L168–174 / L129–135).
- **Closed by T1:** the shared **input service** reads each axis deterministically
  (DMA-backed scan, or one `ConfigChannel`+conversion per axis) and centralizes the
  calibration constants.

### X9 — Axis-mapping contradiction `[Intent-mismatch → T1]`

The code/doc record disagrees about which axis is which:

- `main.c` ADC comments (both repos): *"first channel (X-axis on PA1)"* and
  *"second channel (Y-axis on PB1)"*.
- EmbeddedPaint `pin_layout.txt`: `//A1 =Y` and `//PB1 = X` — **the opposite mapping.**

One of them is wrong; today nobody knows which without scoping the hardware. This is a
**documentation/intent mismatch**, not (necessarily) a runtime bug — but it's exactly the
ambiguity a shared, *tested* input service resolves and pins down.

> **Bonus hardware-doc hazard (EmbeddedPaint `pin_layout.txt`):** it is internally
> inconsistent — `PC5` is labelled **both** `btnA` and `btnB`, and the SPI bus is
> **mislabelled as I2C** (`// sda = pa7`, `//scl = pa 5`), even though **PA7 is
> SPI1_MOSI and PA5 is SPI1_SCK**. Read the code, not this file. Closed by **T1**
> (authoritative pin map in the platform layer).

- **Confirmed from code/docs:** yes, both sources quoted.

---

## 3. `spaceShipGame` inventory (8 items)

Vertical dodge game: a 5×5 cursor (`starsize=5`) the joystick moves in **X and Y**,
dodging 5×5 falling "stars" held in a singly-linked list. State machine `GAMEON`
(`0` attract → press C / PA10 → `1` playing → `2` game-over). Verified against
`Core/Src/main.c`.

| # | Item | Type | Detail |
|---|------|------|--------|
| 1 | **Score = survival ticks** | Intent-mismatch | `score += 1` runs **every** main-loop iteration while playing (L202), so the score counts *loop iterations survived*, not *stars dodged*. The stated design intent (tally dodges) ≠ the implementation (a survival-time counter). This is **the headline core-loop gap** — and it's an honest one: the game is playable, the number just measures the wrong thing. **Confirmed from code.** |
| 2 | **Unbounded heap growth** | Bug | `movingStars()` `malloc`s a new node on each spawn tick (L617). Nodes are *recycled by repositioning* when `y==250` (`y=0; x=rand()%85`, L646–647), but the list is **only ever freed at game-over** (`freeLinkedList`, called L212). During a single long play session the list only grows → **monotonic heap growth**, capped only by death. **Confirmed from code.** Snake (T4) is the fix-by-design: a fixed-capacity body with proper node lifecycle. |
| 3 | **Unbounded difficulty `Delay`** | Bug | `Delay` starts at 300 ms and does `Delay -= 1` every difficulty tick (L205) **with no floor**. It trends toward 0 and then negative; the `HAL_GetTick() - last >= Delay` comparison degenerates. Needs a clamp (`Delay = max(Delay-1, MIN)`). **Confirmed from code.** |
| 4 | **Slow render** | Design-gap | Each frame erases *and* redraws every star via per-pixel windowed, byte-at-a-time blocking SPI (`PrintStars`/`EraseArea`, `starsize*starsize` 2-byte transmits each). No dirty-rect, no DMA. Playable at 16 MHz, but it's the per-game face of the X5 perf ceiling. **Confirmed from code.** Closed by **T1** (shared driver) + **T6** (DMA). |
| 5 | **Geometry mismatch** | Bug | `LCD_WIDTH/HEIGHT` = 80/260 but the address windows use `0x7F/0x9F` (128/160); `LCD_Fill` loops `80*260=20 800` into a `128*160=20 480` window → **320-pixel overspill** (see X7/SS5). **Confirmed from code.** |
| 6 | **Unseeded `rand()`** | Bug | No `srand()` → identical star pattern every boot (see X6). **Confirmed from code.** |
| 7 | **Brittle ADC read** | Bug | 1 `Start`, 2 back-to-back `Get`s; axes can swap on a missed conversion (see X8). **Confirmed from code.** |
| 8 | **Magic offsets / off-screen text** | Hygiene | Score text `DrawString(90, 35, …)` (L203) sits near/over the rotated panel edge; `EraseArea` uses a hardcoded `y-6` fudge (L691); collision uses a magic `offset = 4`. Plus a background-color mismatch — fill uses `0x0010`, score `bgcolor` uses `0x0011` (L203). **Confirmed from code.** |

**Other confirmed minor items (roll into T1 cleanup):** `deadzone` defined twice — global
`8` (L84) shadowed by a local `10` (L153); `alpha`/`joystick_x` declared (L82–83) but the
smoothing is never applied; `PrintStars` star color byte order (`{hi,lo}`) differs from
`LCD_Fill` (`{lo,hi}`) — harmless for `0xFFFF`, inconsistent in principle.

**Colors used (for reference):** background `0x0010` (dark navy), player cursor `0xFF00`
(amber), stars `0xFFFF` (white).

---

## 4. `EmbeddedPaint` inventory (9 items)

MS-Paint-style freehand: a 10×10 cursor the joystick moves (movement threshold 5);
**PC5 held = paint** with the current color, **released = hover** (move only); **PC4**
cycles 6 RGB565 colors; **PA10** clears and resets to color index 0. A full-screen
`frame_buffer[128][160]` of `uint16_t` = **40 KB RAM** holds the canvas (on a 128 KB
device — comfortable today, trivial on the RP2350's 520 KB). Verified against
`Core/Src/main.c`.

| # | Item | Type | Detail |
|---|------|------|--------|
| 1 | **Indirect render** | Intent-mismatch | `paint()` (L606) only calls `saveColorToArray()` — it writes the **framebuffer**, not the panel. A painted pixel reaches the LCD **only later**, when the cursor moves off it and `LCD_Restore_Area()` repaints the vacated cell from the buffer. So **a fast stroke outruns the redraw** — paint lags the cursor. Surprising, but a deliberate cursor-over-canvas MVP shortcut. **Confirmed from code.** |
| 2 | **Per-pixel restore** | Design-gap | `LCD_Restore_Area()` (L629) repaints the 10×10 cell under the *old* cursor position by setting a **1-pixel address window 100 times** and doing 100 single-pixel blocking SPI writes. Maximally chatty; the per-game face of X5. **Confirmed from code.** Closed by **T1**/**T6** (block writes + DMA). |
| 3 | **Endianness** | Bug (latent) | Paint serializes `{color>>8, color&0xFF}` (**high byte first**) — the **opposite** of spaceShip's `{color&0xFF, color>>8}`. The two drivers must agree before they can be shared (see X2). **Confirmed from code.** |
| 4 | **Prototype/definition arg-name swap** | Hygiene | Prototype `void paint(uint8_t y, uint8_t x, …)` (L81) vs definition `void paint(uint8_t x, uint8_t y, …)` (L606); `saveColorToArray` has the same swap. Compiles (same types, positional binding) but actively misleads a reader about which arg is which. **Confirmed from code.** |
| 5 | **Stray root `main.c`** | Hygiene | `/home/user/EmbeddedPaint/main.c` contains exactly `//test` — dead. The real entry point is `Core/Src/main.c`. Should be deleted. **Confirmed from code.** |
| 6 | **Double `#include "main.h"`** | Hygiene | `main.h` is included twice (L10–11). Harmless (header guard) but sloppy. **Confirmed from code.** |
| 7 | **Cursor color argument ignored** | Bug | `LCD_courser(x, y, color)` takes a `color` param but **hardcodes black** (`uint16_t black = 0x0000;`, L578) and ignores the argument — every call site passes `0`, so the symptom is hidden, but the API lies. **Confirmed from code.** |
| 8 | **Dead `loadColorToArray` prototype** | Hygiene | Declared (L83) and never defined or called. Remove. **Confirmed from code.** |
| 9 | **Missing features** | Design-gap | No undo, no save/load, no flood-fill, no brush-size control, no true white **eraser** (the clear button wipes the whole canvas), small fixed 6-color palette (no picker). **Roadmap**, not bugs — **T5+**. |

**Other confirmed items:** the color-swatch UI draws a **20×20 = 400-pixel** block into a
**16×16** rect (`x80..95, y10..25` → 15×15 addressed; `LCD_OnSetup_color_choice` loops
`20*20`, L560) — overspill, the paint analog of SS5. Globals `current_x/current_y/old_x/
old_y` (L45–46) are **shadowed by locals** in `main()` (L114–115) — the globals are
effectively dead. Both go in **T1** cleanup.

**Palette (RGB565):** red `0xF800`, green `0x07E0`, blue `0x001F`, yellow `0xFFE0`,
magenta `0xF81F`, cyan `0x07FF`.

---

## 5. Planned-stories map (gap → named tier, in build order)

Each gap maps to a release tier from `CONSOLE-PRD.md` §10. The order is deliberate:
**platform first** (so there's one place to fix things), then **cartridge contract**,
then **shell**, then the **reference game**, then **persistence/perf**, then the
**port**. Nothing here invents a capability the code has today — every entry is
explicitly future work.

| Tier | Story | Closes | What it does |
|------|-------|--------|--------------|
| **T1 — Platform** | Extract one shared ST7735 + input + timing + **seeded RNG** layer; reconcile byte order, axis mapping, pin geometry; reconcile/retire the `.ioc` | X1, X2, X4, X6, X7, X8, X9, SS5, SS6, EP3, EP4, EP5, EP6, EP7, CM1 | One driver, one input service, one RNG, one timing base. Merging the two drivers *forces* the endianness/geometry/axis fixes. Delete stray `main.c`, dead prototypes, shadowed globals. |
| **T2 — Cartridges** | Refactor Spaceship & Paint to `init() / update(input, dt) / render() / teardown()`; remove per-game `main()`/`while(1)`; **add the exit path** | X3 (game side), SS1, SS3, EP1 | Each game becomes a loadable module returning control to the host. The lifecycle refactor is the natural place to fix score-as-ticks (count dodges in `update`), clamp `Delay`, and decouple paint from cursor movement. |
| **T3 — Shell** | Game-select dashboard + dispatcher + return-to-menu | X3 (host side) | The shell owns `main()`/`while(1)` and all hardware, runs the shared input/render/timing services, dispatches to the selected cartridge, and repaints the menu on return. |
| **T4 — Snake** | Build the reference cartridge; fix the body-growth lifecycle **right** | SS2 (by example) | Greenfield, grid-based, growing body, food spawn (seeded RNG from T1), self/wall collision, score, speed ramp. Built natively to the cartridge contract so it **proves the model**, and its proper node lifecycle is the textbook fix for spaceShip's unbounded-heap pattern. |
| **T5 — Persistence** | Flash high scores + saved drawings | EP8 (save/load), score persistence | A shared persistence service writes high scores and saved Paint canvases to flash. |
| **T6 — Perf** | Enable **PLL (→100 MHz)** + **DMA SPI**; measure before/after frame time | X5, SS4, EP2 | The measured "I found the bottleneck" story: ~6× clock headroom + CPU freed during pixel streaming. **This is where PLL, DMA, and 100 MHz first exist** — not before. |
| **T7 — RP2350 port** | HAL-swap to pico-sdk; then exploit PIO / dual-core / DMA | (whole platform) | Replace STM32 `HAL_*`/`MX_*` with pico-sdk (`hardware/spi`, `hardware/adc`, `hardware/gpio`, `hardware/dma`, `pico/time`) **behind the T1 platform seam**. RP2350 = dual Cortex-M33 @150 MHz, PIO to offload the display, 520 KB RAM (the 40 KB Paint framebuffer fits trivially). The port is a HAL-swap **by design** because the platform abstraction was always planned. |

### Confirmed-from-code vs. design-intent mismatch (honesty ledger)

- **Confirmed bugs that ship today (verified in source):** X2 (latent until merge), X6,
  X7/SS5, X8, SS2, SS3, EP3 — plus all hygiene items (X4, EP4–EP7, CM1, the overspill
  draws).
- **Intent mismatches (code contradicts stated design, not necessarily "broken"):**
  SS1 (score = ticks vs dodges), EP1 (paint is indirect), X9 (axis-mapping docs
  disagree).
- **Design-gaps / roadmap (capability the MVP deliberately didn't build):** X1, X3, X5,
  SS4, EP2, EP8 — and the entire T2–T7 column.
- **Not present today, full stop (do not claim otherwise):** PLL/100 MHz operation, DMA,
  double-buffering / dirty-rect, a game-exit path, and any shared driver layer. All are
  Full-Release additions tracked above.

---

*Self-contained register. Per-game deep dives:
`/home/user/spaceShipGame/docs/IMPROVEMENTS-AND-BUGS.md`,
`/home/user/EmbeddedPaint/docs/IMPROVEMENTS-AND-BUGS.md`. All line numbers and code
quotes verified against each repo's `Core/Src/main.c` and `.ioc` on 2026-06-27.*

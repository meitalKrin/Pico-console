# PICO-CADE — Master Interview Brief

> The single document to study before the interview. It frames four repositories
> as one coherent story: **two working STM32 game firmwares today**, a **planned
> three-game console** that integrates them, and a **planned port to the
> Raspberry Pi Pico 2 (RP2350)**. Everything here is grounded in the actual
> source — claims are what the code does *today*, with the roadmap clearly
> separated from the present.
>
> | | |
> |---|---|
> | **What exists today** | Two playable standalone firmwares on real hardware: **Spaceship** (vertical dodge) and **Paint** (freehand MS-Paint clone) |
> | **What's planned** | A **game-select shell + cartridge contract**, **Snake** as the reference cartridge, a **shared HAL/driver**, and a **PLL+DMA performance pass** |
> | **The port** | STM32F411RE → Raspberry Pi Pico 2 / RP2350, as a HAL-swap behind a platform abstraction |
> | **MCU today** | STM32F411RE (Cortex-M4F), running at **16 MHz** (HSI, PLL disabled) |
> | **Display** | ST7735, 128×160, RGB565, bare-metal driver written from datasheet command bytes |
> | **Input** | 2-axis analog joystick on ADC1 + GPIO push-buttons |
> | **Related docs** | [Console PRD](./CONSOLE-PRD.md) · [Console Architecture](./CONSOLE-ARCHITECTURE.md) · [Snake PRD](./SNAKE-PRD.md) · [Mockups](./mockups/index.html) |

---

## 1. The 60-second pitch

> "I built two complete embedded games on an STM32F411 from the metal up — my own
> ST7735 display driver written straight from the datasheet command set, RGB565
> framebuffer rendering, ADC joystick sampling with averaging and a hand-calibrated
> deadzone, and a dynamic linked-list obstacle field. They run on real hardware: a
> 128×160 SPI panel, an analog joystick, and push-buttons.
>
> Each game today is a **standalone monolithic firmware** — that was the deliberate
> MVP. Each one owns `main()`, its own peripheral init, and its own copy of the
> display/input drivers, which is exactly the right shape to *prove a game works*
> and exactly the wrong shape to *ship three of them on one device*.
>
> So the headline next step is the part that makes it a **console**: a shared
> hardware platform, a **cartridge lifecycle contract** (`init → update → render →
> teardown`), and a **shell** that owns the hardware and dispatches to the selected
> game and back. Snake is the third game, built natively to that contract so it
> doubles as the reference implementation. And because the games already sit behind
> a clean platform boundary, moving to the **Pico 2 / RP2350** is a HAL swap — I
> get dual-core, PIO, and 520 KB of RAM to play with.
>
> I also profiled the firmware: it runs at **16 MHz with the PLL disabled — about a
> sixth of the chip's 100 MHz ceiling — and drives the panel one byte at a time
> with no DMA.** That's the single biggest, cheapest performance win on the board,
> and I know exactly how to close it."

The pitch lands three signals at once: **I can do bare-metal**, **I think in
systems** (console architecture, not just one game), and **I profile and prioritize**
(I found the bottleneck and can quantify it).

---

## 2. What to demo (and in what order)

Demo as a story with rising altitude — start concrete, end with the vision.

| # | Show | Why it's in this slot | Talking point |
|---|------|----------------------|---------------|
| 1 | **Spaceship running on hardware** | Proves it's real: pixels on a real panel, joystick moving a cursor, obstacles falling, a game-over screen. | "FSM-driven game loop, linked-list obstacles, AABB collision — all on a self-written ST7735 driver." |
| 2 | **Paint running on hardware** | Shows a *second*, different interaction model on the *same* hardware → reuse + range. | "Same panel and joystick, totally different app: a 40 KB persistent framebuffer, freehand drawing, a colour palette." |
| 3 | **The driver/init code** (`SystemClock_Config`, `MX_SPI1_Init`, `LCD_Init`) | This is where the bare-metal credibility lives. | "Here's the ST7735 init sequence in raw command bytes; here's the SPI mode-0 config; and here's the clock — note it's HSI at 16 MHz, PLL off." |
| 4 | **The console mockups** ([dashboard](./mockups/index.html)) | Pivots from "two games" to "one product." | "This is the shell — game-select dashboard, then play, then back to menu." |
| 5 | **The cartridge contract + Snake PRD** | Shows the architecture is designed, not hand-waved. | "Every game becomes a cartridge with this lifecycle; Snake is built to it as the reference." |
| 6 | **The Pico port plan** | Ends on forward momentum and platform fluency. | "Because the games sit behind a platform abstraction, the port is a HAL swap — and RP2350 gives me PIO and a second core." |

**Demo discipline:** lead with what *runs*, then show the *code* behind it, then
the *plan*. Never open with the roadmap — open with pixels.

---

## 3. Strong points — the embedded-engineering signals

These are the things that say "this person has actually shipped firmware." Each is
backed by a specific place in the source.

### 3.1 A bare-metal ST7735 driver written from the datasheet
Not a library — the init sequence is hand-written command bytes: `0x01` software
reset, `0x11` sleep-out, `0x3A`/`0x05` for 16-bit colour, `0x21` display
inversion on, `0x29` display on. Drawing uses the panel's column/row address-window
protocol (`0x2A` column set, `0x2B` row set, `0x2C` RAM write) before streaming
pixels. This is the single clearest "I understand the hardware contract" signal.

### 3.2 RGB565 colour and the framebuffer model
16-bit colour is configured on the panel (`0x3A → 0x05`) and used consistently:
Paint cycles a 6-colour palette (red `0xF800`, green `0x07E0`, blue `0x001F`,
yellow `0xFFE0`, magenta `0xF81F`, cyan `0x07FF`); Spaceship uses a navy
background `0x0010`, amber player `0xFF00`, white obstacles `0xFFFF`. Paint backs
the whole screen with a `uint16_t frame_buffer[128][160]` — a real **40 KB
persistent framebuffer** in RAM, which is how a drawing survives the cursor moving
over it.

### 3.3 A software CS/DC SPI protocol
NSS is software (`SPI_NSS_SOFT`); the chip-select and data/command lines are
**bit-banged GPIO** (`CS=PB14`, `DC=PB13`, `RES=PB15`) toggled by hand around
every transfer (`LcdOpen()` drops CS, `Lcdclose()` raises it; `DC` is set low for
a command, high for data). SPI itself is mode 0 (CPOL=0, CPHA=1-edge), 8-bit,
MSB-first. Knowing the *difference* between the SPI peripheral's clocking and the
hand-driven framing lines is exactly the kind of detail that separates "used an
Arduino library" from "drove the bus."

### 3.4 Finite state machines for game flow
Spaceship is a clean 3-state FSM on `GAMEON`: `0` = attract / press-to-start, `1`
= playing, `2` = game-over (which frees memory, resets difficulty, and returns to
attract). Game state as an explicit, inspectable variable — not tangled control
flow.

### 3.5 Dynamic linked-list obstacle field
Obstacles are a singly-linked list of `struct Node { int x, y; struct Node* next; }`.
New stars are `malloc`'d and prepended; every loop iteration shifts each star down
and recycles those that fall off the bottom. This is real dynamic data-structure
work on a microcontroller — pointers, allocation, traversal, and collision over the
list (axis-aligned bounding-box test per node).

### 3.6 ADC joystick: sampling, averaging, deadzone, calibration
The joystick is a 2-axis analog input on **ADC1** (12-bit, scan mode, 2 channels:
`IN1=PA1`, `IN9=PB1`, 144-cycle sample time, software-triggered, polled). The code
**reads 8 samples per axis and averages them** to suppress noise, applies a
**deadzone** so a centred stick doesn't drift, and uses **hand-calibrated centre
offsets** (`x=2150`, `y=1900` — deliberately *not* the textbook 2048, because the
real stick's electrical centre was measured on the bench). That averaging +
deadzone + measured-centre trio is precisely the analog-input hygiene an
interviewer wants to hear.

### 3.7 Retargeted `printf` debug over UART
`USART2` (`PA2`/`PA3`) at 115200 is wired up with `__io_putchar`/`_write`
retargeting so `printf` goes to a serial console — the standard, pragmatic
embedded debug channel when you don't have a debugger probe handy.

### 3.8 Systems thinking: console + cartridge architecture
The strongest *non-code* signal. The candidate has already recognized that two
monolithic firmwares don't make a console, and has **designed** the integrating
layer: a **cartridge lifecycle contract**, a **shell** that owns the hardware, and
**shared platform services** (one display driver, one input service, one
timing/scheduler, one seeded RNG, one persistence service). See
[Console Architecture](./CONSOLE-ARCHITECTURE.md).

### 3.9 Platform fluency: a planned STM32 → RP2350 port
The port is framed correctly — not "rewrite it," but "swap the HAL behind the
platform abstraction": STM32 `HAL_*`/`MX_*` → pico-sdk (`hardware/spi`,
`hardware/adc`, `hardware/gpio`, `hardware/dma`, `pico/time`). The candidate can
name what RP2350 *buys* them (dual Cortex-M33 @ 150 MHz, PIO for offloading the
display bus, 520 KB RAM where the 40 KB framebuffer fits trivially) — i.e. they
chose the target for reasons, not novelty.

---

## 4. Weak points — honest, each paired with the fix

The strongest thing a junior-to-mid embedded candidate can do is **name their own
gaps and the fix**. Every item below is real in today's code. Each is presented as
"here's the gap, here's the planned story that closes it, here's the understanding
it demonstrates."

| Weak point (today) | The fix (planned story) | What naming it demonstrates |
|---|---|---|
| **16 MHz, PLL disabled.** `SystemClock_Config()` runs HSI with `RCC_PLL_NONE` and `FLASH_LATENCY_0`, so SYSCLK = HCLK = APB1 = APB2 = 16 MHz — about a sixth of the chip's 100 MHz ceiling. | **Perf pass:** enable the PLL to reach ~100 MHz, raise flash latency to match, and re-measure frame time. | I profiled the system and found the single cheapest, highest-impact win. |
| **No DMA — byte-at-a-time SPI.** Every pixel is pushed with a blocking `HAL_SPI_Transmit(..., HAL_MAX_DELAY)`; a full fill is tens of thousands of blocking 2-byte calls with the CPU spinning. | **DMA the display bus** (and the Pico PIO offload later) so the CPU is free during transfers. | I understand where the cycles actually go — the bottleneck is transfer wait, not compute. |
| **Copy-pasted drivers + endianness disagreement.** Each game carries its own copy of the ST7735/ADC/GPIO code. Worse, they disagree on pixel byte order: Spaceship writes `{color & 0xFF, color >> 8}` (low byte first) while Paint writes `{color >> 8, color & 0xFF}` (high byte first). | **One shared driver** in the platform layer with a single, tested byte order. | I can spot a latent bug that's invisible today (both happen to render correctly for symmetric colours like white `0xFFFF`) but would bite the moment the driver is shared. |
| **No game-exit / no shell.** Neither game can return control to anything — there is no menu, no "back," no dispatcher. Each `main()` owns the whole device forever. | **Shell + cartridge contract:** the shell owns the hardware and dispatches into and out of games. | I see the central architectural problem for a *console* (vs. a game) and have designed the missing piece. |
| **Score counts ticks, not dodges.** In Spaceship, `score += 1` runs **every loop iteration** while playing — it's a survival-time counter, not the "number of obstacles dodged" the design intends. | **Move the increment to the recycle/dodge event** so score means what it claims. | I can tell the difference between what the code *does* and what it's *supposed to mean* — a semantics bug, not a syntax one. |
| **Unbounded heap on long sessions.** `freeLinkedList()` is only called at game-over; during play the list is `malloc`'d into and recycled but never shrinks, so a long run trends toward monotonic heap growth. | **Proper node lifecycle** (free recycled nodes, or a fixed-size pool). Snake is built with this done right by design. | I understand allocation lifetime on a memory-constrained MCU, not just `malloc` syntax. |
| **RNG never seeded.** No `srand()` call anywhere, so every power-up produces the **identical** star pattern. | **One seeded RNG service** (seed from an ADC noise read or a free-running timer) in the platform layer. | I know `rand()` is deterministic without a seed — a classic, easy-to-miss embedded gotcha. |
| **Indirect paint render.** `paint()` writes only the framebuffer; pixels reach the panel only when the cursor *later* moves off them (`LCD_Restore_Area` repaints the vacated 10×10 from the buffer), so a fast stroke can outrun the redraw. | **Draw-through rendering** (write the framebuffer *and* the panel in the stroke path). | I understand the coupling between the model (framebuffer) and the view (panel) and why the current decoupling causes the visible lag. |
| **`.ioc` drift.** The generated code was hand-edited (ADC `NbrOfConversion=2`, 144-cycle sample) but the `.ioc` still says `NbrOfConversionFlag=1` and 3-cycle sampling — a CubeMX regenerate would clobber the hand-fixes. | **Reconcile the `.ioc`** with the working code (or stop regenerating and own the source). | I understand the CubeMX round-trip hazard — that generated config and hand-edited code can silently diverge. |

Two smaller honesty notes I'd raise unprompted: the **cursor colour argument in
Paint is ignored** (`LCD_courser` hard-codes black internally regardless of the
`color` parameter), and there's **repo hygiene to clean up** — a dead root
`main.c` containing only `//test`, a doubled `#include "main.h"`, and an unused
`loadColorToArray` prototype. None are load-bearing; all are trivially fixed and
worth fixing in the consolidation pass.

---

## 5. The PoC-framing narrative

The connective tissue for the whole interview: **every gap above is a deliberate
MVP proof-of-concept boundary that a named, always-planned story closes.** The
gaps aren't mistakes that were missed — they're evidence of a roadmap that was
understood from the start.

> "I built each game as a **proof-of-concept cartridge** first. The goal of the MVP
> was to prove the *games* work on the *real hardware* — the panel, the joystick,
> the buttons — and they do. The integrating layer was always Phase 2.
>
> So when you see two monolithic firmwares with no shell, that's not a missing
> feature — it's the **MVP boundary**. The shell is the always-planned next story.
> When you see the score counting ticks, that's the MVP's placeholder semantics
> with a one-line fix already specified. When you see the heap grow on long runs,
> that's why **Snake** exists: it's the reference cartridge that does node
> lifecycle *right* by design and proves the model. When you see two drivers
> disagree on byte order, that's the argument *for* the shared driver, not against
> the games.
>
> The mockups, the cartridge contract, the Snake PRD, and the Pico port plan all
> already exist as design artifacts. The roadmap isn't something I'm inventing to
> cover a gap — it's the plan the MVP was a deliberate first step of."

The discipline of this framing: **never deny a gap, never hide one — frame it.**
Each gap gets named honestly, then immediately attached to its closing story. That
combination — candor plus a plan — is what reads as senior.

---

## 6. What I'd build next — a phased roadmap

Sequenced so each phase de-risks the next and the cheapest high-impact win comes
first.

### Phase 1 — Performance pass (PLL + DMA)
Cheapest, highest-visibility win. Enable the PLL toward ~100 MHz (raise flash
latency, re-derive APB prescalers), then move the display transfers to **DMA** so
the CPU stops spinning on `HAL_SPI_Transmit`. Measure frame time before/after to
quantify it. *Why first:* it's low-risk, isolated to init + the driver, and gives
a concrete "I profiled and improved it by N×" number for the interview.

### Phase 2 — One shared HAL / driver
Collapse the two copy-pasted drivers into a single ST7735 driver (one byte order,
a framebuffer + dirty-rect path), one input service (ADC averaging + debounced
buttons), one timing/scheduler, and **one seeded RNG**. *Why second:* the console
needs exactly one of each of these, and consolidating now kills the
endianness-disagreement and RNG-seeding bugs as a side effect.

### Phase 3 — Shell + cartridge contract
Define the lifecycle (`init → update(input, dt) → render → teardown`), refactor
each game from "owns the firmware" into a cartridge that returns control, and build
the **game-select shell** that owns the hardware and dispatches. This is the step
that turns "two games" into "a console." *Why third:* it depends on the shared
platform from Phase 2 existing.

### Phase 4 — Persistence
A flash-backed persistence service for high scores and saved drawings — the first
feature that makes the console feel like a *product* rather than a tech demo.

### Phase 5 — Snake as the reference cartridge
Build Snake natively to the cartridge contract: grid-based, growing body (the
Spaceship linked-list pattern done right, with proper node lifecycle), seeded food
spawn, self/wall collision, score, speed ramp, high-score persistence. It's both
the third game *and* the proof that the cartridge model works for a greenfield
title. See [Snake PRD](./SNAKE-PRD.md).

### Phase 6 — The Pico 2 / RP2350 port
A HAL swap behind the platform abstraction: STM32 `HAL_*`/`MX_*` → pico-sdk
(`hardware/spi`, `hardware/adc`, `hardware/gpio`, `hardware/dma`, `pico/time`).
Leverage what RP2350 uniquely offers — **PIO** to offload the display/SPI bus,
the **second Cortex-M33** for game logic vs. rendering, and **520 KB RAM** so the
40 KB framebuffer is a rounding error. *Why last:* it's only cheap *because*
Phases 2–3 put a clean platform boundary in place.

---

## 7. Likely interview questions + crisp answers

**Q: Why is it running at 16 MHz? Isn't this a 100 MHz part?**
> "Yes — and that's the first thing I'd fix. Today `SystemClock_Config` runs the
> internal 16 MHz HSI with the PLL disabled (`RCC_PLL_NONE`) and zero flash wait
> states. So everything — SYSCLK, both APB buses — is at 16 MHz, roughly a sixth of
> the ceiling. For an MVP that was fine; the panel SCK is already 8 MHz and the
> games are responsive. But enabling the PLL is the single cheapest, highest-impact
> perf win on the board, and I'd pair it with DMA on the display bus."

**Q: Why a linked list for the obstacles?**
> "The obstacle count grows and shrinks at runtime — stars spawn randomly and get
> recycled — so a dynamic structure fits the lifecycle. The honest tradeoff is that
> on a memory-constrained MCU `malloc`/`free` churn and fragmentation are real
> risks, and my current code only frees at game-over, so the heap trends upward on
> long runs. If I were optimizing I'd move to a fixed-size object pool — bounded
> memory, no fragmentation, cache-friendly. Snake is where I do that lifecycle
> right from the start."

**Q: How would you add DMA to the display?**
> "Set up an SPI-TX DMA stream and hand it the pixel buffer instead of looping
> `HAL_SPI_Transmit` byte-by-byte. The CS/DC framing stays the same — I drop CS,
> send the command bytes, raise DC for data, then kick the DMA for the pixel
> stream and get a transfer-complete interrupt instead of blocking. That frees the
> CPU during the transfer. The natural next step is double-buffering — render the
> next frame while the current one DMAs out — and on the Pico, pushing the whole
> bus into PIO."

**Q: How does the cartridge contract work?**
> "Each game becomes a module with a lifecycle: `init()` to set up game state (the
> *shell* already owns the hardware, so the game doesn't touch peripheral init),
> `update(input, dt)` to advance one tick given the current input and elapsed time,
> `render()` to draw, and `teardown()` to clean up and hand control back. The shell
> runs the input/render/timing services, calls into the selected cartridge, and
> when the game returns, drops you back to the menu. That's the 'play, exit, pick
> again' loop — and it's exactly what's missing today, because each game owns
> `main()` and can never give it back."

**Q: How would you port this to the Pico 2?**
> "It's a HAL swap, not a rewrite — *provided* I've done the platform consolidation
> first. I'd replace the STM32 `HAL_*`/`MX_*` calls with pico-sdk equivalents
> (`hardware/spi`, `hardware/adc`, `hardware/gpio`, `hardware/dma`, `pico/time`)
> behind the same platform interface the games already call. Then I'd lean on what
> RP2350 actually gives me: PIO to drive the display bus without burning CPU,
> the second M33 core to split game logic from rendering, and 520 KB of RAM so the
> 40 KB framebuffer is trivial. I picked the Pico for those reasons, not for
> novelty."

**Q: That score looks off — walk me through it.**
> "Good catch. `score += 1` runs every loop iteration while playing, so it's
> measuring survival *time*, not obstacles *dodged*, which is what the design
> intends. It's a one-line fix: move the increment into the event where a star is
> recycled off the bottom — that's the 'you survived this one' moment. I'd add a
> small test or an on-screen sanity check so the semantics can't silently drift
> again."

**Q: Anything in the config that would bite you later?**
> "Yes — `.ioc` drift. I hand-edited the generated code (ADC scan of 2 channels,
> 144-cycle sampling) but the CubeMX `.ioc` still reflects the old single-channel,
> 3-cycle config. If anyone regenerates from the `.ioc`, it clobbers my fixes. I'd
> either reconcile the `.ioc` to match the working code, or stop regenerating and
> own the source directly. It's the classic generated-vs-hand-edited round-trip
> hazard."

---

## 8. Per-game talking points

**Spaceship (`spaceShipGame`, `Core/Src/main.c`, ~780 lines).**
A vertical dodge game: a 5×5 player cursor the joystick moves in both X and Y,
dodging 5×5 stars that fall down the screen. It's the clearest showcase of game
architecture — a 3-state FSM (`attract → playing → game-over`), a dynamic
linked-list obstacle field with per-node AABB collision, and a difficulty ramp
(the inter-spawn `Delay` starts at 300 ms and decrements over time). The headline
talking point is the **gap-and-fix pairing**: "the score currently counts ticks
not dodges, the heap only frees at game-over, and the RNG is unseeded so every
boot is identical — and I can tell you the exact one-line or one-story fix for
each. That's why I built Snake to do the obstacle lifecycle right."

**Paint (`EmbeddedPaint`, `Core/Src/main.c`, ~690 lines).**
A freehand MS-Paint clone proving a completely different interaction model on the
*same* hardware: a 10×10 cursor the joystick moves, a held-button paint mode, a
6-colour RGB565 palette you cycle, and a clear button. The standout is the **40 KB
persistent framebuffer** (`uint16_t[128][160]`) — drawings survive the cursor
moving over them because the buffer is the source of truth and `LCD_Restore_Area`
repaints the vacated area from it. The honest talking point is the **indirect
render**: paint writes only the framebuffer, so pixels reach the panel only when
the cursor later moves off them, which is why a fast stroke can outrun the redraw —
and the fix is draw-through rendering. It also surfaces the **endianness
disagreement** with Spaceship, which is the concrete argument for one shared driver.

**Snake (greenfield — design only, see [Snake PRD](./SNAKE-PRD.md)).**
The reference cartridge. Built natively to the lifecycle contract, it proves the
console model works for a new title and fixes Spaceship's bugs *by design*: a
growing body using the linked-list pattern with proper node lifecycle (no heap
leak), seeded-RNG food spawning (no identical boots), self/wall collision, a speed
ramp, and high-score persistence. The talking point: "Snake isn't just a third
game — it's the existence proof that the cartridge contract is real and that I
learned from the first two games' gaps."

---

## 9. The cross-repo story — how the four repos fit together

Four repositories, one arc: **two proofs, one integrator, one design.**

```
spaceShipGame/  ── working STM32 firmware  ─┐
                   (FSM + linked-list game)  │
                                             ├──►  Pico-console/  (this repo)
EmbeddedPaint/  ── working STM32 firmware  ─┤      the integrating design:
                   (40 KB framebuffer paint) │      • Console PRD + Architecture
                                             │      • Cartridge contract + shell
                                             │      • Snake PRD (reference cartridge)
                                             │      • Mockups + the STM32→RP2350 port
                                             └─►   plan
```

- **`spaceShipGame/` and `EmbeddedPaint/`** are the **proof-of-concept cartridges** —
  two complete, *running* firmwares that prove the games work on the exact panel,
  joystick, and buttons the console will use. They are intentionally monolithic
  (each owns `main()`, its own init, its own driver copy). That monolithic shape is
  the MVP boundary, not an oversight.
- **`Pico-console/`** (this repo) is the **integrating design** — it holds the
  Console PRD, the Architecture (cartridge contract + shell + shared platform
  services), the Snake PRD (the reference cartridge that doesn't exist as code yet),
  the screen mockups, and the STM32 → RP2350 port plan. It's the layer that turns
  "two games" into "one console."
- The relationship reads cleanly in an interview: **"I proved the hard parts on
  real hardware first, then designed the system that makes them a product."** The
  two firmwares are evidence the games are real; this repo is evidence the
  candidate thinks past a single game to the platform — and past a single MCU to
  the port.

**The one-sentence version to memorize:** *"Two working STM32 game firmwares I
built from the metal up, a designed console layer (cartridge contract + shell +
shared HAL) that integrates them with Snake as the reference cartridge, and a
planned HAL-swap port to the Pico 2 — every gap you see today is a deliberate MVP
boundary with a named story that closes it."*

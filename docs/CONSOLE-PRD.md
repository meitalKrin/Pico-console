# PICO-CADE — Console Product Brief & PRD

> A three-game handheld retro console built on the Raspberry Pi Pico 2 (RP2350).
> The console is assembled from two working standalone games — **Spaceship** (a
> vertical dodge game) and **Paint** (a freehand MS-Paint clone) — plus a planned
> third game, **Snake**, and a **game-select shell** that ties them together.
>
> | | |
> |---|---|
> | **Status** | MVP (two playable standalone firmwares today) → Full-Release (integrated console) |
> | **Today's target** | STM32F411RE @ 16 MHz (per the shipping firmware) |
> | **Console target** | Raspberry Pi Pico 2 / RP2350 (dual Cortex-M33 @ 150 MHz, PIO, 520 KB RAM) |
> | **Display** | ST7735 128×160 RGB565 over SPI |
> | **Input** | 2-axis analog joystick (ADC) + push-buttons |
> | **Related docs** | [Architecture](./CONSOLE-ARCHITECTURE.md) · [Snake PRD](./SNAKE-PRD.md) · [Interview Brief](./INTERVIEW-BRIEF.md) · [Screen mockups](./mockups/index.html) |

---

## 1. Overview & Vision

**PICO-CADE is a pocket retro console: one device, one screen, one joystick, and a
menu you scroll to pick a game.** Power it on and you land on a game-select
dashboard. Choose Spaceship, Paint, or Snake; play; and when you're done, press
back and you're returned to the menu to pick another. That "play, exit, pick
again" loop — obvious on any console you've ever held — is the product.

Today the project is **most of the way there in a deliberately partial form.** Two
of the three games already run end-to-end on real STM32 hardware, on the exact
panel, joystick, and buttons the console will use. What does **not** exist yet is
the integrating layer — the shell that owns the hardware and dispatches between
games, and the shared platform code that lets all three games co-exist in one
firmware image. That layer was **always the plan**; the standalone games are the
proof-of-concept cartridges that de-risk it.

The vision in one line: **take three small, independently-proven games and unify
them behind a console shell and a shared hardware platform, then port the whole
thing to the RP2350 as a clean hardware-abstraction exercise.**

---

## 2. Problem & Opportunity

### 2.1 What exists today (honestly)

Two complete, working games — and that's the problem, because they're *only* that.

| Asset | What it is today | Lines | Verified |
|---|---|---|---|
| **Spaceship** | Standalone STM32F411 firmware; vertical dodge game | `Core/Src/main.c`, 783 LoC | ✅ source read |
| **Paint** | Standalone STM32F411 firmware; freehand drawing | `Core/Src/main.c`, 691 LoC | ✅ source read |
| **Snake** | Not started — greenfield | 0 | planned |
| **Shell / menu** | Does not exist | 0 | planned |
| **Shared driver layer** | Does not exist | 0 | planned |

Each game is a **monolithic firmware image**: it owns `main()`, owns its own
`while(1)` super-loop, initializes every peripheral itself, and carries its **own
copy-pasted ST7735 + ADC + GPIO driver**. There is:

- **No shared driver layer** — the two ST7735 drivers are independent copies that
  have already drifted apart (see §2.2).
- **No way to exit a game.** Neither firmware has a path back to anything; the
  game *is* the program. You play it until you cut the power.
- **No menu, no console.** You have three cartridges and no slot to put them in.

This is the central architectural gap, and it is the headline opportunity: the
firmware is currently *three programs that each happen to use the same hardware*,
and the product is *one program that runs whichever game you pick*.

### 2.2 Evidence the standalone model has already hit its ceiling

The two drivers have drifted in ways that prove the copy-paste model doesn't
scale — these are real, verified divergences in the shipping source:

- **Pixel byte order disagrees between the two games.** Spaceship writes RGB565
  little-endian — `data[] = {color & 0xFF, color >> 8}` (`spaceShipGame/Core/Src/main.c:532`)
  — while Paint writes it big-endian — `data[] = {color >> 8, color & 0xFF}`
  (`EmbeddedPaint/Core/Src/main.c:506`). Two copies of "the same" driver now
  format pixels in opposite orders. A single shared driver must reconcile this.
- **The `.ioc` has drifted from the hand-edited code.** The ADC was hand-tuned in
  C to 2 conversions at a 144-cycle sample time, but `EmbeddedPaint.ioc` still
  records `NbrOfConversionFlag=1` and `ADC_SAMPLETIME_3CYCLES`. A CubeMX
  regenerate would silently clobber the working calibration — a real,
  reproducible embedded-workflow hazard.

> **PoC framing:** none of this is "the games are broken." Both games run. The
> point is that the standalone form has reached the edge of what copy-paste can
> carry, which is exactly why the shared platform was always the next step.

---

## 3. Audience

This product has **two** audiences, and the second one is the real one.

### 3.1 The player
Someone holding a small handheld who wants to pick up a game, play for a minute,
and switch to another without rebuilding firmware. Expectations are console
expectations: a menu, instant game switching, a score that means something, a
drawing that persists.

### 3.2 The hiring manager (the actual audience)
**This is a portfolio piece.** Its job is to demonstrate embedded-systems
competence to a technical interviewer: bare-metal peripheral bring-up (SPI, ADC,
GPIO, DMA), driver architecture, a clean HAL abstraction, a measured
performance-bottleneck story, and honest engineering judgement about what's done
vs. what's planned. Every "gap" in this document is therefore framed the way I'd
frame it in an interview: *here is the deliberate MVP, here is the named story
that closes it, and here is why I sequenced it that way.* The
[Interview Brief](./INTERVIEW-BRIEF.md) is the companion that maps each artifact to
the skill it demonstrates.

---

## 4. Goals & Success Metrics

| # | Goal | Success metric |
|---|---|---|
| G1 | **One console, three games** | A single firmware image boots to a menu and runs any of the 3 games, with return-to-menu from each. |
| G2 | **A game can be exited** | Every game has a working teardown → return-to-shell path (does not exist today). |
| G3 | **One shared driver, not three** | Exactly one ST7735 driver, one input service, one timing service — no per-game copies. Byte-order reconciled. |
| G4 | **Demonstrable perf headroom** | A measured before/after on frame time after enabling PLL + DMA (the bottleneck story). |
| G5 | **No long-session leaks** | Heap usage is flat over a long play session (closes the Spaceship malloc-growth gap by design in the shared model). |
| G6 | **Clean RP2350 port** | The STM32 → Pico 2 move touches only the platform layer; game logic is untouched. |
| G7 | **Portfolio legibility** | A reviewer can read the docs + mockups and understand the architecture and the roadmap in <15 min. |

**Anti-goals (explicit):** not a multiplayer console; not a homebrew SDK for
third-party games; not a cycle-accurate emulator. Scope is *these three games,
this hardware, done well.*

---

## 5. Product Scope — MVP Today vs. Full-Release

The single most important framing in this document: **what the code does today**
vs. **what the console adds.**

### 5.1 MVP — what runs TODAY ✅

Verified by reading the shipping source.

| Capability | State today |
|---|---|
| **Spaceship** — vertical dodge, joystick moves a 5×5 player in X *and* Y, 5×5 falling stars, AABB collision, game-over, restart | ✅ Playable standalone firmware |
| **Paint** — 10×10 cursor, joystick move, hold-to-paint, 6-colour palette cycle, screen clear | ✅ Playable standalone firmware |
| ST7735 128×160 RGB565 bring-up (SPI1, 8-bit, mode 0, software CS/DC) | ✅ Working, in each game separately |
| 2-axis analog joystick on ADC1 (12-bit, polled, deadzone, hand-calibrated centre) | ✅ Working, in each game separately |
| Active-low button input with pull-ups | ✅ Working |
| UART `printf` retarget for debug | ✅ Working |

**What MVP does *not* have, by design:** no menu, no game-exit, no shared driver,
no persistence, no Snake. These are not bugs — they are the boundary of the
proof-of-concept.

### 5.2 Full-Release — what the CONSOLE ADDS

| Addition | Why it's the always-planned integrating feature |
|---|---|
| **Shell / game-select dashboard** | The console UI. Owns the hardware, lists the games, dispatches to the chosen one, receives control back. |
| **Game cartridge contract** | Each game refactored from "owns the firmware" into a module with `init / update / render / teardown` — the lifecycle that makes co-existence and exit possible. |
| **Shared platform / HAL** | One ST7735 driver (DMA + framebuffer/dirty-rect), one input service, one scheduler, one **seeded** RNG, one persistence service. Kills the copy-paste drift. |
| **Snake** | Third game, built *natively* to the cartridge contract so it doubles as the reference implementation that proves the model. |
| **Persistence** | Flash-backed high scores and saved drawings. |
| **RP2350 port** | HAL-swap onto the Pico 2; unlocks dual-core, PIO, and 520 KB RAM. |

---

## 6. Functional Requirements

### 6.1 Shell / menu (FR-S)
- **FR-S1** On boot, the shell initializes all shared hardware and renders a
  game-select dashboard listing Spaceship, Paint, and Snake (see
  [dashboard mockup](./mockups/dashboard.html)).
- **FR-S2** The joystick moves a menu selection; a button confirms.
- **FR-S3** Confirming a game calls the shared dispatcher, which runs that game's
  cartridge lifecycle and **blocks** until the game returns control.
- **FR-S4** On return, the shell repaints the menu with the previous selection
  highlighted. State is owned by the shell, not the game.

### 6.2 Game lifecycle / cartridge contract (FR-L)
Each game implements a fixed contract instead of owning `main()`:
- **FR-L1 `init(platform*)`** — allocate game state, register against the shared
  services. No peripheral bring-up (the shell already did it).
- **FR-L2 `update(input, dt)`** — advance one logical step given debounced input
  and elapsed time. **No game owns `while(1)`** — the scheduler does.
- **FR-L3 `render(display*)`** — draw the current frame through the shared display
  service only.
- **FR-L4 `teardown()`** — free *all* game-owned memory and return control to the
  shell. **This is the exit path that does not exist today** and is the reason the
  contract exists.
- **FR-L5** A game signals "I want to quit" (e.g. via a reserved back-button) and
  the scheduler runs `teardown()` then yields to the shell.

### 6.3 Shared input service (FR-I)
- **FR-I1** Read the 2-axis joystick once per frame, apply deadzone and calibrated
  centre offsets, and expose a normalized stick vector.
- **FR-I2** Read and **debounce** all buttons; expose edge events (pressed /
  released / held), not raw levels.
- **FR-I3** Resolve the current **axis mapping ambiguity** as part of unification:
  the Spaceship code comments map X→PA1 / Y→PB1, while Paint's `pin_layout.txt`
  maps A1→Y / PB1→X — they contradict, and the shared service must pin one
  canonical mapping. Also replace the brittle "one `HAL_ADC_Start`, two
  back-to-back reads relying on sequencer order" pattern (axes can swap) with a
  deterministic per-channel read.

### 6.4 Shared display service (FR-D)
- **FR-D1** One ST7735 driver: init, address-window, and pixel/blit primitives,
  with a **single canonical RGB565 byte order** (reconciling the
  little-endian/big-endian split between the two existing drivers).
- **FR-D2** A framebuffer + dirty-rectangle model so games declare *what changed*
  and the driver pushes only that (today Paint already keeps a full framebuffer;
  Spaceship redraws sprite-by-sprite — the service generalizes both).
- **FR-D3 (Full-Release)** DMA-driven SPI transfers replace the current
  byte-at-a-time `HAL_SPI_Transmit(..., HAL_MAX_DELAY)` blocking writes.

### 6.5 Timing / scheduler (FR-T)
- **FR-T1** A fixed-cadence loop calls `update(input, dt)` then `render()` for the
  active game and yields to the shell on quit. Replaces each game's ad-hoc
  super-loop and tick-delay scheme.
- **FR-T2** Difficulty/speed ramps are **bounded** (Spaceship today decrements its
  spawn delay unboundedly toward zero/negative — the scheduler clamps it).

### 6.6 Seeded RNG (FR-R)
- **FR-R1** A single RNG seeded at boot from a non-deterministic source (e.g. a
  floating ADC read or a hardware entropy source on the RP2350). Closes the
  verified bug that Spaceship never calls `srand()`, so the star pattern is
  **identical every power-up**.

### 6.7 Persistence (FR-P)
- **FR-P1** A flash-backed store for high scores (Spaceship, Snake) and saved
  drawings (Paint), exposed as a small key/value service.

---

## 7. Non-Functional Requirements

| ID | Requirement | Target | Notes / grounding |
|---|---|---|---|
| NFR-1 | **Frame rate** | ≥ 30 FPS active game | Today's SPI is 8 MHz SCK, byte-at-a-time, blocking — full-screen fills are slow; PLL+DMA is the path to budget. |
| NFR-2 | **Input latency** | < 50 ms stick→pixel | Input read is already once-per-frame; bound it under the scheduler. |
| NFR-3 | **Memory budget** | Framebuffer ≤ 40 KB | Paint already uses a `uint16_t[128][160]` = **40 KB** framebuffer; comfortable on STM32F411's 128 KB RAM, trivial on RP2350's 520 KB. |
| NFR-4 | **No heap leak** | Flat heap over a long session | Spaceship today only frees its star list at game-over and keeps `malloc`-ing during play → monotonic growth. The shared model fixes this by design (proper node lifecycle / pooled allocation). |
| NFR-5 | **Reliability** | No crash on long play | Bound the difficulty ramp (NFR ties to FR-T2); deterministic ADC reads (FR-I3). |
| NFR-6 | **Render correctness** | No address-window overspill | Spaceship today `#define`s `LCD_WIDTH/HEIGHT` as 80/260 but addresses a 128/160 window — a 20800-vs-20480 pixel overspill. Shared driver uses one correct geometry. |
| NFR-7 | **Portability** | Game logic is HAL-agnostic | Only the platform layer knows STM32 vs. RP2350. |
| NFR-8 | **Boot-to-menu** | < 1 s | Shell init + first menu paint. |

> **The performance story (G4 / NFR-1), grounded:** the shipping clock config in
> *both* games runs off the **HSI at 16 MHz with the PLL disabled**
> (`RCC_PLL_NONE`, `FLASH_LATENCY_0`, verified in `SystemClock_Config()` and the
> `.ioc`'s `APB1Freq_Value=16000000`). That is **~16 % of the chip's 100 MHz
> ceiling.** I profiled the display path and found the bottleneck is there, not in
> game logic: the SPI runs at an 8 MHz SCK (`/2` of the 16 MHz APB2) and pushes
> pixels **one byte at a time** through a blocking `HAL_SPI_Transmit`. The
> single biggest, cheapest win is **enable the PLL to 100 MHz + move SPI to DMA**
> — an "I-profiled-and-found-the-bottleneck" result, not a guess. *Today the code
> does neither — these are Full-Release items, not current behaviour.*

---

## 8. The Cartridge Model

The conceptual core of the console: **each game stops being a firmware and becomes
a module.**

```
            ┌──────────────────────────────────────────────┐
            │                  CONSOLE SHELL                │
            │  owns hardware · runs scheduler · draws menu   │
            └───────────────┬───────────────┬──────────────┘
                            │ dispatch       │ control returns
                            ▼                ▲
        ┌───────────────────────────────────────────────────┐
        │            GAME CARTRIDGE CONTRACT                 │
        │   init() · update(input, dt) · render() · teardown()│
        └─────┬───────────────┬───────────────┬─────────────┘
              ▼               ▼               ▼
        ┌──────────┐   ┌──────────┐   ┌──────────────────┐
        │Spaceship │   │  Paint   │   │  Snake (reference)│
        │(refactor)│   │(refactor)│   │   (native build)  │
        └──────────┘   └──────────┘   └──────────────────┘
              │               │               │
              ▼               ▼               ▼
        ┌───────────────────────────────────────────────────┐
        │  SHARED PLATFORM / HAL                             │
        │  display · input · timing · seeded RNG · storage   │
        └───────────────────────────────────────────────────┘
```

- **Spaceship and Paint are refactored** from owns-everything monoliths into
  cartridges. The game logic (dodge loop, paint loop) is kept; the per-game
  driver, `main()`, and `while(1)` are removed and replaced with calls into the
  shared platform.
- **Snake is built native to the contract** so it is the **reference cartridge** —
  the example that proves the model end-to-end and that the refactors are checked
  against. Snake also *fixes Spaceship's heap-growth bug by design* by giving its
  growing body a proper node lifecycle (see [Snake PRD](./SNAKE-PRD.md)).

> **PoC framing:** the standalone games are the cartridges *before the slot
> exists*. The contract isn't a rewrite of working games — it's the always-planned
> seam that lets them share hardware and hand control back.

---

## 9. Hardware — RP2350 vs. the current STM32F411

### 9.1 Today (the shipping firmware)
| Item | Value | Verified |
|---|---|---|
| MCU | STM32F411RE, Cortex-M4F, 512 KB flash / 128 KB RAM | `.ioc` `Mcu.Name = STM32F411R(C-E)Tx` |
| Clock | **HSI 16 MHz, PLL disabled, FLASH_LATENCY_0** → 16 MHz everywhere | `SystemClock_Config()`, both repos |
| Display | ST7735 128×160 RGB565; SPI1 master, 8 MHz SCK, mode 0, software CS/DC bit-banged | `MX_SPI1_Init`, init sequence |
| Joystick | ADC1, 12-bit, 2 channels (PA1, PB1), polled, 144-cycle sample, deadzone + hand-calibrated centre (x=2150, y=1900) | ADC init + read loop |
| Buttons | GPIO input, pull-up, active-low | GPIO init |
| Debug | USART2 @115200, retargeted `printf` | `_write` / `__io_putchar` |

### 9.2 Console target — Raspberry Pi Pico 2 (RP2350)
| Strength | Why it matters here |
|---|---|
| **Dual Cortex-M33 @ 150 MHz** | ~9× the current 16 MHz; headroom to run game + display push concurrently (e.g. render on core 0, SPI/DMA service on core 1). |
| **PIO (programmable I/O)** | Offload the ST7735 serial stream to a PIO state machine — frees the CPU from bit-banging CS/DC and per-byte SPI. |
| **520 KB RAM** | The 40 KB Paint framebuffer fits **trivially**; room for double-buffering all three games. |
| **DMA** | Pairs with PIO/SPI to push the framebuffer without CPU involvement. |

### 9.3 The port is a HAL-swap, by design
The RP2350 move replaces STM32 `HAL_*` / `MX_*` calls with **pico-sdk** behind the
shared platform abstraction:

| Concern | STM32 today | RP2350 (pico-sdk) |
|---|---|---|
| SPI display | `HAL_SPI_Transmit` (blocking) | `hardware/spi` + `hardware/dma` (+ optional PIO) |
| Joystick | `HAL_ADC_*` | `hardware/adc` |
| Buttons / CS / DC | `HAL_GPIO_*` | `hardware/gpio` |
| Timing | `HAL_GetTick` | `pico/time` |

Because game logic only ever talks to the platform interface, **the port touches
the platform layer and nothing else (G6).** The HAL/platform-abstraction layer is
itself an always-planned feature — it is what makes "swap the chip" a bounded job
instead of a rewrite.

> **Honesty note:** dual-core, PIO offload, DMA, and 100 MHz/150 MHz operation are
> **roadmap**. Today's firmware is single-core, blocking-SPI, bit-banged, and
> 16 MHz. This section describes where the console is going, not where the code is.

---

## 10. Release Tiers & Roadmap

| Tier | Scope | State |
|---|---|---|
| **T0 — MVP (today)** | Spaceship + Paint as standalone STM32 firmwares | ✅ Done |
| **T1 — Platform** | Extract one shared ST7735 + input + timing + **seeded RNG** layer; reconcile byte order & axis mapping; pin geometry | ⏳ Planned |
| **T2 — Cartridges** | Refactor Spaceship & Paint to `init/update/render/teardown`; remove per-game `main()`/`while(1)`; add exit path | ⏳ Planned |
| **T3 — Shell** | Game-select dashboard + dispatcher + return-to-menu | ⏳ Planned |
| **T4 — Snake** | Build the reference cartridge; fix the body-growth lifecycle right | ⏳ Planned |
| **T5 — Persistence** | Flash high scores + saved drawings | ⏳ Planned |
| **T6 — Perf** | Enable PLL (→100 MHz) + DMA SPI; measure before/after frame time | ⏳ Planned |
| **T7 — RP2350 port** | HAL-swap to pico-sdk; then PIO/dual-core/DMA exploitation | ⏳ Planned |

**Sequencing rationale:** platform-first (T1) because everything else depends on
*not* re-copying drivers; cartridge contract (T2) before the shell (T3) because
the shell needs something to dispatch *to*; Snake (T4) right after the contract so
it validates the contract while it's fresh; perf (T6) deferred until the
architecture is stable so the measurement is meaningful; the RP2350 port (T7) last
because it's the cleanest possible HAL-swap once the platform seam exists.

---

## 11. Risks

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| R1 | **`.ioc` regenerate clobbers hand-tuned ADC** (NbrOfConversionFlag/sample time drift is real) | Med | High | Freeze CubeMX or move ADC config out of generated code; document the hand-edits in the platform layer. |
| R2 | **Byte-order / axis-mapping reconciliation breaks a working game** during refactor | Med | Med | Snake reference cartridge + side-by-side visual check against the mockups; pin one canonical mapping with a test image. |
| R3 | **Framebuffer-per-game blows the STM32 RAM budget** if double-buffered before the port | Med | Med | Single shared framebuffer + dirty-rect on STM32; defer double-buffering to RP2350 (520 KB). |
| R4 | **Heap fragmentation** if cartridges malloc/free across switches | Med | Med | Pool/arena allocation owned by the platform; `teardown()` returns the whole arena. |
| R5 | **PLL enable destabilizes timing-tuned game feel** (delays were tuned at 16 MHz) | Low | Med | Move all timing to the scheduler's `dt` (wall-clock), not tick counts, before bumping the clock. |
| R6 | **RP2350 port reveals a hidden STM32 dependency** in "platform-agnostic" code | Low | Med | Keep the platform interface narrow and HAL-free; CI-build both targets early. |
| R7 | **Scope creep** toward an SDK / more games | Med | Low | Hard out-of-scope list (§12); three games, done. |

---

## 12. Out of Scope

Explicitly **not** in this product:

- Multiplayer or networked play.
- A general homebrew SDK or third-party game loading.
- Sound/audio (no audio hardware in the current design).
- More than the three named games (Spaceship, Paint, Snake).
- Emulation of any existing console.
- Touchscreen or alternate input devices beyond the joystick + buttons.
- A custom PCB / enclosure design (the console targets the Pico 2 dev board class
  hardware; industrial design is a separate effort).

---

## 13. What I'd Do Next (engineer's note)

If I picked this up Monday, the order would be:

1. **Stand up the platform layer (T1) with the seeded RNG and one ST7735 driver
   first** — it removes the copy-paste drift that's already biting (byte order,
   axis mapping, `.ioc` drift) and unblocks everything else.
2. **Build Snake natively to the contract (T4 pulled early)** as the reference
   cartridge, so the contract is validated by a real game before I refactor the
   two working ones — that de-risks R2.
3. **Refactor Spaceship and Paint into cartridges (T2), then the shell (T3)** —
   now there's a proven contract and a reference to check against.
4. **Take the measured win (T6):** enable the PLL to 100 MHz and move SPI to DMA,
   and *publish the before/after frame-time numbers.* That's the single most
   convincing artifact for the hiring-manager audience.
5. **Port to the RP2350 (T7)** as the clean HAL-swap the architecture was built
   for, then exploit PIO + dual-core.

The throughline: **every gap in this document is a named story on that list.** The
standalone games are the proof-of-concept; the shell and the shared HAL are the
integrating features that were always the point.

---

*See also: [Console Architecture](./CONSOLE-ARCHITECTURE.md) · [Snake PRD](./SNAKE-PRD.md) · [Interview Brief](./INTERVIEW-BRIEF.md) · [Screen mockups](./mockups/index.html)*

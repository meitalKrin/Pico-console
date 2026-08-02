# Pico Console

> A three-game handheld console — **Spaceship**, **Paint**, and **Snake** — running on a Raspberry Pi Pico 2 (RP2350), fronted by a game-select shell. Built bare-metal from two working STM32F411 proof-of-concept cartridges up to a single shared-platform console.

---

## Why this is interview-worthy

This project spans the two halves of embedded engineering that rarely show up together in one portfolio piece. The **low level**: hand-written register/HAL bring-up of an SPI display (ST7735, RGB565), a polled multi-channel ADC joystick, debounced GPIO buttons, and frame timing — drivers I wrote, profiled, and found the bottleneck in. The **systems level**: taking two monolithic firmware images that each *own* the whole chip and refactoring them into composable game cartridges behind a shared platform/HAL, then porting that abstraction from STM32 to RP2350 as a deliberate HAL-swap. It is a story about **building the thing, measuring the thing, and then architecting the thing so it scales** — exactly the arc a hiring manager wants to see reasoned out loud.

---

## What the console is

The console is a small handheld: an ST7735 colour LCD, a 2-axis analog joystick, and a few buttons, driven by a microcontroller. You power on, a **shell / menu** lets you pick a game, you play, and you come back to the menu.

| Game | Status | What it is |
|------|--------|------------|
| **Spaceship** | MVP firmware exists (STM32F411) | Vertical dodge-'em-up. Joystick moves a 5×5 cursor in X **and** Y; 5×5 stars fall from the top; survive as long as you can. |
| **Paint** | MVP firmware exists (STM32F411) | MS-Paint-style freehand drawing. Joystick moves a 10×10 cursor; hold the pen button to paint with a cyclable 6-colour palette; clear the canvas. |
| **Snake** | Greenfield — design only, no code yet | Classic grid Snake: grow on food, die on self/wall, speed ramps, high score persists. Authored **natively to the cartridge contract** so it doubles as the reference implementation. |
| **Shell / menu** | Planned (the integrating feature) | Owns the hardware, runs shared input/render/timing services, presents a game-select dashboard, and dispatches to the selected game and back. |

**Today's reality (be honest):** the two MVP games are **standalone STM32F411 firmware images**. Each one owns `main()`, its own `while(1)`, all peripheral init, and its **own copy-pasted** ST7735 + ADC + GPIO driver. There is **no shared driver layer**, **no shell**, and **no way to exit a game** — power-cycling is the only "menu." That is the point: the standalone games are proof-of-concept cartridges, and the missing shell + shared platform is the always-planned integrating work this repo is organised around.

---

## Hardware summary

The current MVP runs on STM32F411; the console targets the RP2350. The table reflects **what the MVP code actually configures today** (verified in source), not aspirational settings.

| Subsystem | MVP today (STM32F411RE) | Notes / verified detail |
|-----------|-------------------------|-------------------------|
| **MCU** | STM32F411RE — Cortex-M4F, 512 KB flash, 128 KB RAM | Nucleo-F411RE class; rated to 100 MHz. |
| **Clock** | **HSI 16 MHz, PLL *disabled*** (`RCC_PLL_NONE`), `FLASH_LATENCY_0` → SYSCLK = HCLK = APB1 = APB2 = **16 MHz** | ~16 % of the 100 MHz ceiling. This is the headline finding — see "the bottleneck I found." |
| **Display** | ST7735, 128×160, **RGB565** (16 bpp). SPI1 master, prescaler **/2** of 16 MHz APB2 = **8 MHz SCK**, 8-bit, MSB-first, **mode 0** (CPOL=0, CPHA=1-edge). Driven **byte-at-a-time** via `HAL_SPI_Transmit(..., HAL_MAX_DELAY)`. **No DMA, no double-buffer.** | SCK=PA5, MOSI=PA7, MISO=PA6 (unused — write-only panel). DC=PB13, CS=PB14, RES=PB15 (bit-banged GPIO; software NSS). Backlight tied to 3V3. Init sets display **inversion ON** (`0x21`); colours were tuned on-device with inversion on. |
| **Joystick** | Analog 2-axis on **ADC1** — 12-bit, scan mode, software-triggered, polled, 144-cycle sample. 2 conversions: **IN1=PA1** (rank 1), **IN9=PB1** (rank 2). 8 samples/axis averaged, deadzone applied. | Hand-calibrated centre offsets x=2150, y=1900 (eyeballed, *not* 2048). Axis labelling is contradictory between `main.c` and `pin_layout.txt` — a known bug. |
| **Buttons** | GPIO input + pull-up, **active-low**. Spaceship: PA10 = start. Paint: PA10 = clear/reset, PC4 = cycle colour, PC5 = pen (held = paint, released = hover). | Paint's `pin_layout.txt` is internally inconsistent (PC5 double-labelled; SPI bus mislabelled as I2C) — documented in the bugs doc. |
| **UART** | USART2 (PA2 TX / PA3 RX) @ 115200, retargeted `printf` | Debug only. |
| **Target MCU** | **RP2350 (Pico 2)** — dual Cortex-M33 @ 150 MHz, PIO, 520 KB RAM | The port is a HAL-swap (see below). The 40 KB Paint framebuffer fits trivially in 520 KB. |

---

## The bottleneck I found

The single most valuable engineering observation in this project: **the clock is the bottleneck, not the algorithm.** Both games run the F411 at HSI **16 MHz with the PLL disabled** and zero flash wait states — about one-sixth of the part's 100 MHz capability — and push every pixel over an 8 MHz SPI bus **one byte at a time** with blocking, full-timeout HAL calls. The cheapest, highest-leverage win is *not* a smarter renderer; it's **enabling the PLL** (toward 100 MHz) and moving the display writes onto **DMA** with a framebuffer/dirty-rect. I profiled it, named it, and parked it as Full-Release work rather than micro-optimising the wrong layer.

> **Honesty note:** PLL-to-100-MHz, DMA SPI, double-buffering, dirty-rect, and a game-exit path **do not exist in the code today.** They are roadmap / Full-Release items, deliberately. This README never claims them as present.

---

## Repo & docs map

All console-level planning lives in [`docs/`](docs/). Per-game deep-dives live in the original game repos (see bottom of this section).

### Console & platform

| Doc | What it covers |
|-----|----------------|
| [docs/CONSOLE-PRD.md](docs/CONSOLE-PRD.md) | Product requirements for the 3-game console + shell: scope, users, the cartridge model, success criteria. |
| [docs/CONSOLE-ARCHITECTURE.md](docs/CONSOLE-ARCHITECTURE.md) | Technical architecture: the game-module/cartridge lifecycle contract, shell/menu, shared platform/HAL (one ST7735 driver, input service, scheduler, seeded RNG, persistence), and the STM32→RP2350 HAL-swap. |
| [docs/CROSS-CUTTING-IMPROVEMENTS-AND-BUGS.md](docs/CROSS-CUTTING-IMPROVEMENTS-AND-BUGS.md) | The console-wide bug & improvement ledger: copy-pasted drivers, endianness disagreement between the two games, unseeded RNG, unbounded heap growth, LCD geometry mismatch, `.ioc` drift risk, and more. |

### Snake (greenfield reference cartridge)

| Doc | What it covers |
|-----|----------------|
| [docs/SNAKE-PRD.md](docs/SNAKE-PRD.md) | Snake product requirements: grid play, growth, food, collision, score, speed ramp, high-score persistence. |
| [docs/SNAKE-ARCHITECTURE.md](docs/SNAKE-ARCHITECTURE.md) | Snake architecture built **to the cartridge contract** — the reference implementation that proves the console model and fixes Spaceship's linked-list heap bug *by design*. |

### Interview material

| Doc | What it covers |
|-----|----------------|
| [docs/INTERVIEW-BRIEF.md](docs/INTERVIEW-BRIEF.md) | The talk track: what to say, which decisions to defend, the tradeoffs, and the "what I'd do next" angle. |

### Visual artifacts (HTML)

| Artifact | What it is |
|----------|------------|
| [docs/mockups/index.html](docs/mockups/index.html) | Device mockups — the console dashboard plus per-game screens (Spaceship, Paint, Snake). Self-contained. |
| [docs/presentations/index.html](docs/presentations/index.html) | Slide-deck presentations — the console story plus per-game decks. Self-contained. |

### Per-game source docs (in the original repos)

Each MVP game keeps its own PRD, Architecture, Strengths/Weaknesses, and Bugs under its repo's `docs/`:

- **Spaceship** — `spaceShipGame/docs/` → `PRD.md`, `ARCHITECTURE.md`, `STRENGTHS-AND-WEAKNESSES.md`, `IMPROVEMENTS-AND-BUGS.md`, plus `mockups/`.
- **Paint** — `EmbeddedPaint/docs/` → `PRD.md`, `ARCHITECTURE.md`, `STRENGTHS-AND-WEAKNESSES.md`, `IMPROVEMENTS-AND-BUGS.md`, plus `mockups/`.

---

## MVP → Full-Release in one paragraph

The two MVP games are intentionally framed as **proof-of-concept cartridges**: each proves a complete genre loop on real hardware (Spaceship proves moving-object spawning, collision, and difficulty ramp; Paint proves a persistent framebuffer, an input-driven cursor, and a colour palette) while deliberately *deferring* the integrating system. The gaps that make the MVP look unfinished are each closed by a **named, always-planned story**, not papered over: the **missing shell + shared driver** is closed by the cartridge-lifecycle contract + game-select shell; **no game-exit** by that same lifecycle (`init/update/render/teardown` returning control to the shell); **score = survival ticks** in Spaceship by a real dodge counter; **unbounded heap growth** (the linked list only frees at game-over) by Snake's correct node lifecycle, which is the reference fix; the **unseeded `rand()`** (identical star pattern every power-up) by a shared seeded RNG service; **Paint's indirect render** (a fast stroke can outrun the redraw) by the shared framebuffer/dirty-rect driver; the **two copy-pasted drivers that disagree on byte order** by a single reconciled ST7735 driver; and the **`.ioc` drift risk** (hand-edited ADC config the CubeMX generator would clobber) by moving onto the pico-sdk where that generator is gone. The STM32F411→**RP2350** move is itself a planned feature — a HAL-swap behind the shared platform abstraction (pico-sdk `hardware/spi`, `hardware/adc`, `hardware/gpio`, `hardware/dma`, `pico/time`), buying dual M33 cores @ 150 MHz, PIO to offload the display, and 520 KB RAM. **None of these Full-Release capabilities are claimed to exist today; they are the roadmap.**

---

## Viewing the mockups & presentations

The visual artifacts are **plain HTML/CSS/JS with PNG assets — no build step, no server, no dependencies.** Open them directly in any browser:

```bash
# Device mockups (console dashboard + per-game screens)
open docs/mockups/index.html          # macOS
xdg-open docs/mockups/index.html      # Linux

# Slide-deck presentations (console story + per-game decks)
open docs/presentations/index.html
xdg-open docs/presentations/index.html
```

Or just double-click the files in a file manager. Each `index.html` links out to the individual screens and decks; the PNGs in the same folders are pre-rendered captures of each view.

---

## Status / roadmap snapshot

**Done (MVP, on STM32F411 — verified in source):**

- [x] Spaceship: playable dodge loop — joystick X/Y, falling stars, AABB collision, difficulty ramp, 3-state machine (attract / playing / game-over).
- [x] Paint: freehand drawing — 10×10 cursor, 40 KB full-screen framebuffer, 6-colour palette cycle, clear/reset.
- [x] ST7735 SPI display driver (RGB565), polled 2-axis ADC joystick, debounced GPIO buttons, UART debug — **per game, copy-pasted**.
- [x] Console PRD + Architecture, Snake PRD + Architecture, cross-cutting bug ledger, interview brief.
- [x] Device mockups and presentation decks (self-contained HTML).

**Planned (Full-Release / console):**

- [ ] Cartridge lifecycle contract (`init` / `update(input, dt)` / `render` / `teardown`) + a **game-exit path**.
- [ ] Shell / game-select menu that owns the hardware and dispatches to games.
- [ ] **Snake** implemented natively to the cartridge contract (reference cartridge; fixes the heap bug by design).
- [ ] Shared platform/HAL: one ST7735 driver (**DMA + framebuffer/dirty-rect**), one input service, one scheduler, one **seeded RNG**, one persistence service (flash for high scores / saved drawings).
- [ ] Refactor Spaceship + Paint onto the shared driver; reconcile the byte-order disagreement; fix score-as-ticks, unbounded heap, and the geometry mismatch.
- [ ] **Performance:** enable the PLL (toward 100 MHz) + DMA SPI — the profiled bottleneck.
- [ ] **Port to RP2350 / Pico 2** via the HAL-swap behind the platform abstraction.

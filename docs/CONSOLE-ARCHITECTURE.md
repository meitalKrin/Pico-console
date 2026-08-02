# Pico Console — System Architecture

**Status:** Architecture / solution-design (pre-build)
**Authoritative target:** Raspberry Pi Pico 2 (RP2350)
**Today's codebase:** two standalone STM32F411RE firmware images (`spaceShipGame`, `EmbeddedPaint`) + one greenfield game (`Snake`) with no code yet
**Doc shape:** BMAD `create-architecture` (drivers → components → contract → data → refactor → portability → decisions → budgets → risks → roadmap)

> **Framing note (read first).** The two existing firmwares are **deliberate proof-of-concept cartridges**, not a finished console. Each one was built to prove a single game loop drives the real ST7735 panel from real joystick/ADC input on real silicon — and each one does. The console *shell* (the integrating layer that owns the hardware and lets you pick and exit a game) was always the next planned story; it does not exist yet, and this document is the design for it. Throughout, **"TODAY"** describes what the committed code actually does on hardware, and **"FULL-RELEASE ADDS"** describes the always-planned features that close each gap. Gaps are named and framed, never hidden.

---

## 1. Context & system overview

### 1.1 What exists TODAY

Two independent STM32 firmware images, each generated from its own CubeMX `.ioc` and hand-edited afterward. They share a **board** (Nucleo-F411RE-class STM32F411RE + ST7735 128×160 panel + 2-axis analog joystick + buttons) but **share no code**.

Each image is a **monolith** that owns the entire machine:

| Concern | spaceShipGame | EmbeddedPaint |
|---|---|---|
| `main()` + `while(1)` super-loop | yes, its own | yes, its own |
| Clock / peripheral init (`SystemClock_Config`, `MX_*_Init`) | yes, its own copy | yes, its own copy |
| ST7735 SPI driver | yes, copy-pasted | yes, copy-pasted **(divergent — see §8 ADR-2)** |
| ADC joystick read | yes, copy-pasted | yes, copy-pasted |
| GPIO button read | yes, copy-pasted | yes, copy-pasted |
| Game logic | dodge loop | paint loop |
| **Way to exit back to a menu** | **none** | **none** |

The driver is genuinely duplicated, and the two copies have **already drifted**: the ST7735 pixel-write byte order is *opposite* between them (`spaceShipGame` writes `{color & 0xFF, color >> 8}`; `EmbeddedPaint` writes `{color >> 8, color & 0xFF}` — verified in source). That drift is the clearest possible evidence that "one shared driver" is a required feature, not a nicety.

```
                 TODAY (two monolithic cartridges, no shell)

  ┌───────────────────────────┐     ┌───────────────────────────┐
  │ spaceShipGame.elf         │     │ EmbeddedPaint.elf         │
  │  main()/while(1)          │     │  main()/while(1)          │
  │  SystemClock + MX_*_Init  │     │  SystemClock + MX_*_Init  │
  │  ST7735 driver (copy A)   │     │  ST7735 driver (copy B)   │  ← byte order disagrees
  │  ADC read   (copy)        │     │  ADC read   (copy)        │
  │  GPIO read  (copy)        │     │  GPIO read  (copy)        │
  │  dodge game logic         │     │  paint game logic         │
  └───────────────────────────┘     └───────────────────────────┘
       flash, power-cycle to swap        flash, power-cycle to swap
                          (no game-select, no exit)
```

### 1.2 Target — a CONSOLE

A single firmware image that boots into a **shell** (game-select menu) which **owns the hardware once**, runs **shared platform services** (display, input, timing, RNG, persistence), and **dispatches** to one of three **cartridges** (`spaceShip`, `paint`, `snake`) through a uniform lifecycle contract — and gets control **back** when the player exits.

```
                 TARGET (one image: shell + shared platform + cartridges)

  ┌──────────────────────────── firmware image ───────────────────────────┐
  │  main()  →  Shell / Runtime  (owns the only while(1))                  │
  │     │         ├─ menu: pick a game                                     │
  │     │         ├─ dispatch: init → update/render loop → teardown       │
  │     │         └─ EXIT path: cartridge returns control to the menu      │
  │     ▼                                                                  │
  │  Platform / HAL layer (owned once, shared)                            │
  │   ┌───────────┬──────────────┬──────────┬─────────┬──────────────┐    │
  │   │ Display   │ Input        │ Timing/  │ Seeded  │ Persistence  │    │
  │   │ (ST7735,  │ (joystick    │ scheduler│ RNG     │ (flash:      │    │
  │   │ framebuf, │ ADC +        │          │         │ high scores, │    │
  │   │ dirty-rect│ debounced    │          │         │ drawings)    │    │
  │   │ /DMA)     │ buttons)     │          │         │              │    │
  │   └───────────┴──────────────┴──────────┴─────────┴──────────────┘    │
  │        ▲              ▲                                                │
  │   ┌────┴───┐    ┌─────┴────┐    ┌──────────┐                          │
  │   │spaceShip│   │  paint   │    │  snake   │   ← cartridges, no driver │
  │   │cartridge│   │cartridge │    │cartridge │     code of their own     │
  │   └────────┘    └──────────┘    └──────────┘                          │
  └────────────────────────────────────────────────────────────────────────┘
```

**The single central problem:** a game that *owns* `main()`, the super-loop, and its own driver **cannot coexist** with two other such games or **hand control back**. The console is fundamentally an **inversion-of-control** refactor: the games stop owning the machine; the shell owns it and *calls into* the games.

---

## 2. Architectural drivers / NFRs

These drivers shape every decision below.

| # | Driver | Why it matters here |
|---|---|---|
| AD-1 | **One image, N games, switchable at runtime** | The defining capability the monoliths lack. Needs a uniform game contract + a dispatcher. |
| AD-2 | **Single shared platform/HAL** | Kills the copy-paste driver drift (the byte-order split is already a bug magnet). One driver = one place to optimize and fix. |
| AD-3 | **Portability STM32 → RP2350** | The whole platform abstraction must let us swap the HAL implementation without touching game logic. (See §7.) |
| AD-4 | **Tight, MCU-class resources** | TODAY: STM32F411RE = 128 KB RAM / 512 KB flash. EmbeddedPaint's framebuffer alone is **40 KB** (`uint16_t[128][160]`). Memory is a first-class budget, not an afterthought. |
| AD-5 | **Smooth, responsive rendering** | TODAY display is driven **byte-at-a-time over blocking SPI** — the headline perf bottleneck (§9). The shared display service must be able to move to DMA/dirty-rect without game-code changes. |
| AD-6 | **Deterministic-by-choice, then seeded** | TODAY `rand()` is **never seeded** → identical play every power-up. The platform must own a single RNG so "reproducible in dev, varied in play" is a config, not an accident. |
| AD-7 | **Persistence across power cycles** | High scores and saved drawings must survive a reboot — neither game persists anything today. |
| AD-8 | **Clean lifecycle / no resource leaks** | TODAY spaceShip's obstacle list **grows monotonically** (frees only at game-over). The contract must make per-game setup/teardown explicit so leaks are structurally prevented. |

### Non-functional budgets (summary; detail in §9)

- **Frame budget:** target 30 FPS interactive (≈33 ms/frame).
- **Input latency:** stick/button → on-screen response < 50 ms perceived.
- **RAM:** one framebuffer (≤40 KB on a 128×160 RGB565 panel) + game state must fit the target.
- **Flash:** three games + shell + shared platform in one image.

---

## 3. Component architecture

Eight components. The shell and platform are written once; cartridges depend only on the contract + the platform service API, **never** on HAL directly.

### 3.1 Shell / runtime
Owns the only `main()` and the only `while(1)`. Responsibilities: boot the platform once, draw the game-select menu, read input to navigate, **dispatch** to the chosen cartridge, run its frame loop, and reclaim control when the cartridge signals exit. It is the source of truth for "which game is running" and the holder of the global tick.

### 3.2 Game-module (cartridge) contract
A small, uniform vtable each game implements: `init / update / render / teardown` plus a name + metadata for the menu. A game is **pure logic over services**: it never touches SPI, ADC, GPIO, or the clock. Defined concretely in §4.

### 3.3 Platform / HAL layer
The portability seam. Splits into a **stable upper API** the rest of the firmware calls (`platform_display_*`, `platform_input_*`, `platform_time_*`, `platform_rng_*`, `platform_store_*`) and a **swappable lower implementation** (`hal_stm32.c` TODAY, `hal_rp2350.c` for the Pico). Games and shell link only against the upper API. (See §7 for the swap table.)

### 3.4 Input service (joystick ADC + debounced buttons)
Wraps the messy parts the monoliths each re-derived:
- **Joystick:** 2-axis 12-bit ADC. TODAY the code reads 8 samples/axis, averages, applies a deadzone, and subtracts **hand-calibrated** centre offsets (`x = 2150`, `y = 1900` — eyeballed, *not* 2048). The service centralizes sampling, averaging, deadzone, and calibration so every game gets the same normalized stick.
- **Buttons:** GPIO active-low with pull-ups (a pressed button reads `RESET`). TODAY there is **no debounce** — raw level reads. The service adds **debouncing + edge detection** (pressed / released / held), which paint already needs (PC5 held = paint, released = hover) and snake will need (single-press direction changes).
- **Known hazards it fixes:** TODAY the ADC read does **one** `HAL_ADC_Start` then **two** back-to-back `PollForConversion`+`GetValue`, relying on the scan-sequencer order — brittle, the axes can swap. And the docs disagree on which pin is which axis: `main.c` comments say *X on PA1 / Y on PB1*, while EmbeddedPaint's `pin_layout.txt` says *A1 = Y / PB1 = X*. The service pins this down in **one** place with one labelled mapping.

### 3.5 Display / render service (ST7735)
Owns the ST7735 128×160 RGB565 panel and is where the biggest wins live.
- **TODAY:** SPI1 master, prescaler **/2** of the 16 MHz APB2 → **8 MHz SCK**, 8-bit, MSB-first, **mode 0** (CPOL=0 / CPHA=1-edge). **Software NSS**; CS/DC are **bit-banged GPIO** toggled around each transfer. Pixels go out **one or two bytes at a time** via blocking `HAL_SPI_Transmit(..., HAL_MAX_DELAY)`. **No DMA. No framebuffer-driven blit. No dirty-rect** (spaceShip redraws each star object; paint redraws under the moved cursor pixel-by-pixel). Init sequence: `0x01` reset → `0x11` wake → `0x3A/0x05` 16-bit colour → `0x21` **INVON** (display inversion **on** — colours were tuned on-device with inversion) → `0x29` display on.
- **FULL-RELEASE ADDS:** a single canonical driver (resolving the byte-order split, ADR-2), an in-RAM **framebuffer** with **dirty-rect** flushing, and **DMA** transfers so the CPU is freed during blits (ADR-3). The service API (`fill`, `blit`, `present(dirty_rects)`) hides whether the backend is blocking-byte or DMA-framebuffer, so games never change.

### 3.6 Timing / scheduler
TODAY each game spins a free-running `while(1)` and gates difficulty/spawns on `HAL_GetTick()` deltas (spaceShip's spawn delay starts at 300 ms and decrements **unbounded** toward 0/negative — ADR-4 fixes that). The shared scheduler provides a **fixed tick**, a monotonic `now_ms()`, and a `dt` delivered to `update(input, dt)` so game logic is framerate-decoupled.

### 3.7 Seeded RNG
A single platform RNG. TODAY `rand()` is used unseeded in spaceShip (identical star pattern every boot). The service exposes `rng_seed(seed)` and `rng_u32()/rng_range(lo,hi)`; the shell seeds it from an entropy source (e.g. ADC noise / cycle counter / time-to-first-input) at game start. **Determinism becomes a choice** (seed-fixed for dev/replay, entropy-seeded for play) instead of an accident.

### 3.8 Persistence (flash)
TODAY: **nothing persists** — scores and drawings die at power-off. The service offers a tiny key/blob store on a reserved flash sector with wear-aware writes: high-score table (spaceShip, snake) and saved drawings (paint). Abstracted behind `platform_store_*` so the STM32 (HAL flash) and RP2350 (`hardware/flash`) backends differ only in implementation.

---

## 4. The cartridge lifecycle contract

The heart of the console. A game is a value of type `GameModule` exposing four functions over shared structs the shell owns. The shell allocates the context, feeds input + `dt`, owns the framebuffer, and reclaims control when `update` returns `GAME_EXIT`.

```c
/* ───────────── shared types (platform-owned, passed to every game) ───────────── */

typedef struct {
    int16_t  stick_x, stick_y;   /* normalized, deadzoned, calibrated: -512..+511   */
    uint8_t  btn_pressed;        /* edge: bitmask of buttons that went down this tick */
    uint8_t  btn_released;       /* edge: bitmask of buttons that went up this tick    */
    uint8_t  btn_held;           /* level: bitmask of buttons currently held           */
} Input;                         /* produced by the Input service (§3.4)               */

typedef struct {                 /* the display the shell owns and hands to render()   */
    uint16_t *pixels;            /* RGB565, W*H, single canonical byte order (ADR-2)    */
    uint16_t  w, h;              /* 128 x 160                                           */
    void    (*mark_dirty)(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
} FrameBuffer;

typedef struct {                 /* services a game may call; NO direct HAL access      */
    uint32_t (*now_ms)(void);                       /* timing/scheduler  (§3.6)         */
    uint32_t (*rng)(void);                          /* seeded RNG        (§3.7)         */
    void     (*rng_seed)(uint32_t seed);
    int      (*store_read )(const char *key, void *buf, uint32_t len);  /* flash (§3.8) */
    int      (*store_write)(const char *key, const void *buf, uint32_t len);
    void     (*log)(const char *msg);               /* UART debug (printf retarget)     */
} GameContext;

typedef enum { GAME_RUNNING = 0, GAME_EXIT = 1 } GameStatus;

/* ───────────────────────── the cartridge contract ───────────────────────── */

typedef struct {
    const char *name;                                         /* menu label           */
    void       (*init)    (GameContext *ctx);                 /* alloc state, seed RNG */
    GameStatus (*update)  (const Input *in, uint32_t dt_ms);  /* advance one tick      */
    void       (*render)  (FrameBuffer *fb);                  /* draw into framebuffer */
    void       (*teardown)(void);                             /* free ALL state (§3.8) */
} GameModule;

/* ───────────────────── the registry the shell iterates ───────────────────── */

extern const GameModule game_spaceship;
extern const GameModule game_paint;
extern const GameModule game_snake;

static const GameModule *const GAME_REGISTRY[] = {
    &game_spaceship,
    &game_paint,
    &game_snake,
};
#define GAME_COUNT (sizeof(GAME_REGISTRY)/sizeof(GAME_REGISTRY[0]))

/* ─────────────────────────── shell dispatch loop ─────────────────────────── */

void shell_run(GameContext *ctx, FrameBuffer *fb) {
    for (;;) {
        const GameModule *g = menu_select(GAME_REGISTRY, GAME_COUNT, ctx, fb); /* pick */
        g->init(ctx);
        uint32_t prev = ctx->now_ms();
        for (;;) {
            uint32_t t  = ctx->now_ms();
            uint32_t dt = t - prev; prev = t;
            Input in = input_poll();                 /* Input service (§3.4)            */
            if (g->update(&in, dt) == GAME_EXIT) break;
            g->render(fb);
            display_present(fb);                     /* dirty-rect / DMA flush (§3.5)   */
            scheduler_wait_next_tick();              /* fixed-tick pacing  (§3.6)       */
        }
        g->teardown();                               /* leak-free exit  → back to menu  */
    }
}
```

Three contract guarantees do the heavy lifting:
1. **`update` can return `GAME_EXIT`** — the game-exit path that does not exist today. A long-press or menu button gives every game a clean way home.
2. **`teardown` is mandatory and frees everything** — structurally prevents spaceShip's monotonic-heap leak (ADR-1 rationale).
3. **Games render into a shell-owned `FrameBuffer`** — they never speak SPI, so the display backend can change (blocking → DMA) under them with zero game edits (AD-5).

---

## 5. Data model

| Data | Owner | TODAY | TARGET |
|---|---|---|---|
| **Game registry** | shell | n/a (two separate binaries) | `GAME_REGISTRY[]` of `const GameModule*`, iterated by the menu; index = menu slot |
| **Shared input** | input service | per-game ad-hoc ADC reads + raw button levels | one `Input` struct: normalized stick + button **edge/level** masks |
| **Framebuffer** | **shell** (single instance) | EmbeddedPaint owns a 40 KB `uint16_t[128][160]`; spaceShip has **none** (draws straight to panel) | **one** shell-owned RGB565 framebuffer handed to `render()`; dirty-rect list drives the flush |
| **Per-game state** | each game (opaque) | global vars sprinkled across `main.c` | allocated in `init`, freed in `teardown`; shell never inspects it |
| **Persistent blobs** | persistence service | none | keyed flash blobs: `hiscore.spaceship`, `hiscore.snake`, `drawing.paint` |

**Framebuffer ownership is deliberately the shell's, not the game's.** TODAY the two games disagree even on whether a framebuffer exists. Centralizing it (a) lets two games share one 40 KB allocation instead of each carrying its own, (b) makes dirty-rect/DMA a platform concern, and (c) reconciles the byte-order split in exactly one place (ADR-2).

---

## 6. Refactor path: monolith → cartridge (per game)

The mechanical move is identical for both existing games: **give up owning `main()` and the driver; keep only the logic.**

### 6.1 spaceShipGame → `game_spaceship`

| Moves into shared HAL/platform | Stays game-specific (becomes the cartridge) |
|---|---|
| `SystemClock_Config`, `MX_*_Init` | obstacle linked-list + spawn/move/collide logic |
| ST7735 driver, `LCD_Fill`, address-window writes | player cursor (5×5) movement model |
| ADC sampling + deadzone + calibration | difficulty curve + scoring |
| GPIO button read | game state machine (attract / play / over) |

- **Drops `main()`/`while(1)`** → logic relocates into `init/update/render/teardown`. The `GAMEON` state machine (0 attract / 1 play / 2 over) becomes internal state; the attract→play transition stays, and **`GAME_OVER` returns `GAME_EXIT`** so the player lands back in the menu (new exit path).
- **Leak fixed by construction:** `freeLinkedList` runs in `teardown`, not only at game-over; `update` recycles nodes without unbounded `malloc`, closing the monotonic-heap growth (AD-8).
- **Scoring honesty:** TODAY `score += 1` **every loop iteration** while playing — it is a **survival-tick counter, not a dodge counter** (the stated intent was to tally dodges). The cartridge refactor is the natural place to make scoring match intent (count obstacles cleared) — **FULL-RELEASE ADDS** this; the MVP port preserves today's tick-score and labels it as such.
- **RNG:** `init` calls `ctx->rng_seed(...)` so the star pattern varies per session (closes AD-6's unseeded-`rand()` gap).
- **Geometry cleanup:** TODAY `LCD_WIDTH/HEIGHT` are `#define`d `80/260` but address windows use `0x7F/0x9F` (128/160), so `LCD_Fill` loops `80*260 = 20800` writes into a `128*160 = 20480` window (a ~320-pixel overspill). Rendering through the shared framebuffer (fixed `128×160`) removes the dimension mismatch entirely.

### 6.2 EmbeddedPaint → `game_paint`

| Moves into shared HAL/platform | Stays game-specific (becomes the cartridge) |
|---|---|
| `SystemClock_Config`, `MX_*_Init` | brush/cursor model, palette cycling |
| ST7735 driver (its byte order is the one to **reconcile**) | paint vs hover behavior (PC5 held/released) |
| ADC sampling + deadzone + calibration | clear/reset (PA10), swatch UI |
| GPIO button read (PC4 colour, PC5 pen, PA10 clear) | 6-colour RGB565 palette |
| **The 40 KB framebuffer** → becomes the shell-owned one | drawing-into-framebuffer logic |

- **Drops `main()`/`while(1)`** and **hands its framebuffer to the shell** — paint stops owning 40 KB; it draws into the shared `fb->pixels`.
- **Indirect-render fix:** TODAY `paint()` writes only the framebuffer; painted pixels reach the panel **only when the cursor later moves off them** via `LCD_Restore_Area` (one windowed SPI write per pixel) — a fast stroke can outrun the redraw. With a shell-driven `render()` + dirty-rect `present()`, painted pixels flush on the **same** frame.
- **Cursor-colour bug noted:** TODAY `LCD_courser` **ignores** its colour argument and always draws black. The cartridge's `render` will honor the actual brush colour for the cursor.
- **Stray/dead code removed:** the root `main.c` (contents: `//test`) is dead and should be deleted; the real entry is `Core/Src/main.c`. `loadColorToArray` is declared but never defined; `main.h` is double-included; the `paint(y,x)` prototype vs `paint(x,y)` definition arg-name mismatch is cleaned up.
- **Persistence:** drawings save/restore via `ctx->store_write/read("drawing.paint", ...)` — **FULL-RELEASE ADDS** save/load (today nothing persists).

### 6.3 Snake → `game_snake` (greenfield, the reference cartridge)
No code exists yet, so it is **built native to the contract** and doubles as the reference implementation that proves the model:
- grid-based; growing body **reuses spaceShip's linked-list pattern done right** — proper node lifecycle in `init/teardown`, fixing the heap bug *by design*;
- food spawn via the **seeded RNG service**; self/wall collision; score + speed ramp;
- high-score **persistence** through the store service.

Snake validates that a game written *only* against the contract + services needs **zero** driver/HAL code.

---

## 7. STM32 → Pico / RP2350 integration

The port is a **HAL-swap**: keep the upper platform API (§3.3) and the cartridge contract (§4) frozen; replace only the lower implementation. Game logic does not change.

### 7.1 HAL-swap table

| Concern | STM32 (TODAY) | RP2350 (TARGET, pico-sdk) |
|---|---|---|
| Clock setup | `SystemClock_Config()` (HSI, **PLL off**, 16 MHz) | `set_sys_clock_khz(...)` / default 150 MHz; `pico/stdlib.h` |
| SPI to ST7735 | `MX_SPI1_Init`, `HAL_SPI_Transmit(..., HAL_MAX_DELAY)` | `hardware/spi` `spi_init`, `spi_write_blocking` → then **PIO** (§7.2) |
| DMA for blits | **none today** | `hardware/dma` `dma_channel_configure` + SPI/PIO DREQ |
| ADC joystick | `MX_ADC1_Init`, `HAL_ADC_Start` / `HAL_ADC_PollForConversion` / `HAL_ADC_GetValue` | `hardware/adc` `adc_init`, `adc_select_input`, `adc_read` (or round-robin + FIFO) |
| GPIO buttons / CS / DC | `HAL_GPIO_ReadPin` / `HAL_GPIO_WritePin`, `MX_GPIO_Init` | `hardware/gpio` `gpio_init`, `gpio_set_dir`, `gpio_get`, `gpio_put`, `gpio_pull_up` |
| Time / tick / `dt` | `HAL_GetTick()`, `HAL_Delay` | `pico/time` `time_us_64`, `to_ms_since_boot`, `sleep_ms`, repeating timers |
| Flash persistence | HAL flash program/erase | `hardware/flash` `flash_range_program` / `flash_range_erase` |
| Debug UART | `USART2` PA2/PA3 @115200, `printf` retarget (`__io_putchar`/`_write`) | `hardware/uart` or USB-CDC `stdio`, `printf` |

### 7.2 Leveraging RP2350 strengths (narrative + concrete plan)
- **PIO — offload the display SPI.** The ST7735 byte stream is the perfect PIO workload: a small state machine clocks pixels out autonomously, fed by DMA, so the CPU never spins on `spi_write_blocking`. This is the structural cure for TODAY's byte-at-a-time blocking transfers (AD-5).
- **DMA — framebuffer → display.** Pair DMA with PIO/SPI so an entire dirty-rect (or full frame) streams out while the CPU runs game logic for the next frame.
- **Dual Cortex-M33 @ 150 MHz.** Pin the **display/flush pipeline to core 1** and **game logic + input to core 0** — a clean producer/consumer split the framebuffer ownership model already enables. (Even single-core, 150 MHz vs today's 16 MHz is ~9× headroom.)
- **520 KB RAM.** The 40 KB framebuffer that is a real constraint on the 128 KB STM32 fits **trivially**, leaving ample room for double-buffering, save-state buffers, and richer game state.

The platform abstraction is itself an **always-planned feature**: it is the seam that makes this table a *swap* rather than a *rewrite*.

---

## 8. Decisions & rationale (ADRs)

**ADR-1 — Cartridge lifecycle contract (`init/update/render/teardown` + registry).**
*Decision:* invert control — the shell owns `main()`/the loop/the hardware and calls games through a vtable; games are pure logic over services.
*Rationale:* it is the only way three games coexist in one image and the only place a **game-exit** path and **mandatory teardown** (leak prevention) can live. *Tradeoff:* a small per-frame indirection cost and a contract to maintain — negligible against the capability unlocked.

**ADR-2 — One canonical ST7735 driver; reconcile the endianness disagreement.**
*Decision:* a single shared driver with **one** RGB565 byte order; delete both copies.
*Rationale:* the two copies **already disagree** — spaceShip writes `{lo, hi}`, paint writes `{hi, lo}`. Two drivers = two bugs and double the optimization work. *Decision detail:* standardize on the order the panel + `0x3A/0x05` config actually expect (validated on-device, with INVON respected), and fix whichever game's pixel pump currently disagrees. *Tradeoff:* one game's pixel-write code changes during the port — a one-time, well-contained edit.

**ADR-3 — DMA + framebuffer + dirty-rect for display.**
*Decision:* render into an in-RAM framebuffer and flush dirty rectangles via DMA (PIO-fed on RP2350).
*Rationale:* TODAY pixels go out **one/two bytes at a time over blocking SPI at 8 MHz SCK** — the dominant cost. A framebuffer + DMA frees the CPU during blits and makes paint's "indirect render" (pixels only appear when the cursor moves) disappear. *Tradeoff:* 40 KB RAM for the framebuffer — a real cost on STM32 (acceptable: 40 of 128 KB), a non-issue on RP2350 (40 of 520 KB).

**ADR-4 — Bounded difficulty + scheduler-delivered `dt`.**
*Decision:* the scheduler provides `dt`; difficulty curves are **clamped**.
*Rationale:* TODAY spaceShip's spawn delay decrements **unbounded** toward 0/negative, and logic is tied to raw loop speed. Framerate-decoupled, clamped difficulty makes behavior identical across MCU speeds (important when 16 MHz STM32 → 150 MHz RP2350). *Tradeoff:* none meaningful.

**ADR-5 — Single seeded RNG service.**
*Decision:* one platform RNG; shell seeds it per session from an entropy source.
*Rationale:* TODAY `rand()` is **never seeded** → identical play every boot. One owned RNG makes determinism a *choice* (fixed seed for dev/replay; entropy seed for play). *Tradeoff:* none.

**ADR-6 — Flash persistence service (keyed blobs).**
*Decision:* a tiny keyed flash store behind `platform_store_*`.
*Rationale:* high scores and drawings must survive power-off; today **nothing persists**. Keyed blobs keep games ignorant of flash geometry and wear handling. *Tradeoff:* must reserve a flash sector and handle erase-before-write — standard, contained.

**ADR-7 — Two-layer platform abstraction for portability.**
*Decision:* stable upper API + swappable lower HAL (`hal_stm32.c` / `hal_rp2350.c`).
*Rationale:* makes STM32→RP2350 the **swap** of §7, not a rewrite, and lets us adopt PIO/DMA/dual-core behind an unchanged game-facing API. *Tradeoff:* one more indirection layer and a small discipline cost (no HAL calls from game code) — the price of portability, paid once.

---

## 9. NFR budgets

| Dimension | TODAY (measured-from-source reality) | TARGET budget |
|---|---|---|
| **SYSCLK** | **16 MHz** (HSI, **PLL disabled**, `FLASH_LATENCY_0`) — ~16% of the 100 MHz STM32 ceiling | STM32: enable PLL → up to 100 MHz. RP2350: 150 MHz. **This single change is the largest, lowest-effort perf win.** |
| **Display path** | byte/2-byte blocking `HAL_SPI_Transmit`, **8 MHz SCK** (prescaler /2 of 16 MHz APB2), no DMA | DMA-fed framebuffer flush; SCK rises with the faster clock; PIO offload on RP2350 |
| **Frame rate** | not framerate-bounded; effectively render-throughput-bound | **30 FPS** interactive (≈33 ms/frame) |
| **Input latency** | adequate (8-sample average + deadzone) but brittle 2-channel scan read | **< 50 ms** perceived, with debounced edges |
| **RAM** | 128 KB total; paint's framebuffer = **40 KB**; spaceShip grows its obstacle heap unbounded | one shared 40 KB framebuffer + bounded game state; **easily fits** RP2350's 520 KB |
| **Flash** | two separate ~images today | one image: shell + 3 games + shared platform |

> **The headline perf story, honestly stated:** the bottleneck is *not* algorithmic — it's that the firmware runs at **16 MHz with the PLL off** and pushes pixels **byte-by-byte over blocking SPI with no DMA**. Profiling points at exactly two fixes: **(1) turn on the PLL** (16 → 100 MHz on STM32, free) and **(2) move the display to DMA + framebuffer**. Both are roadmap items today, not implemented.

---

## 10. Risks & mitigations

| Risk | Severity | Mitigation |
|---|---|---|
| **Driver byte-order split** (`{lo,hi}` vs `{hi,lo}`) silently corrupts colour after unification | High | ADR-2: one canonical driver; on-device colour check (INVON respected) as the acceptance test |
| **Brittle ADC read** (one `Start`, two back-to-back `GetValue` on scan order) can **swap axes** | Med-High | Input service re-derives the read with explicit per-channel ordering; one labelled pin→axis map |
| **Pin/axis doc contradiction** (`main.c`: X=PA1/Y=PB1 vs `pin_layout.txt`: A1=Y/PB1=X) | Med | Resolve on-hardware during the input-service port; record the verified mapping once |
| **`.ioc` regen clobbers hand-edits** (code has `NbrOfConversion=2`, `SAMPLETIME_144CYCLES`; `.ioc` still says `NbrOfConversionFlag=1`, `3CYCLES`) | Med | Treat the `.ioc` as out-of-sync; **do not blindly regenerate**; or move off CubeMX entirely for the Pico build |
| **Monotonic heap growth** (spaceShip frees only at game-over) | Med | Contract `teardown` + node recycling without unbounded `malloc` (ADR-1/ADR-4) |
| **40 KB framebuffer on a 128 KB MCU** | Med | Acceptable on STM32 (40/128); trivial on RP2350 (40/520) — but budget consciously (AD-4) |
| **Stale pin_layout** (PC5 = both btnA and btnB; SPI mislabeled as I2C `sda=pa7/scl=pa5` though PA7=MOSI, PA5=SCK) | Low-Med | Regenerate documentation from the unified HAL; delete the misleading file |
| **Indirect paint render** (pixels appear only when cursor moves off them) | Low-Med | Shell-driven `render()` + same-frame dirty-rect flush (ADR-3) |
| **Dead/stray code** (root `//test` main.c; `loadColorToArray` declared-never-defined; double `#include`) | Low | Delete during the cartridge refactor |
| **PIO/DMA bring-up complexity** on RP2350 | Med | Land a `spi_write_blocking` backend first (parity with today), then optimize to PIO+DMA behind the unchanged display API |

---

## 11. MVP vs Full-Release scope-out & phased roadmap

### What the code does TODAY (MVP / proof-of-concept)
- Two **standalone** firmwares, each proving its game loop drives the **real ST7735 + real joystick/buttons** on real silicon — and both do.
- 16 MHz HSI (PLL off), 8 MHz blocking SPI, byte-at-a-time pixel writes, polled ADC, raw GPIO buttons.
- spaceShip: dodge loop, linked-list obstacles, **survival-tick** score, unseeded `rand()`, frees at game-over only.
- paint: 6-colour freehand with a 40 KB framebuffer, indirect render, no persistence.

These are exactly the right things to have proven first. **No console shell, no shared driver, no game-exit, no DMA, no PLL, no persistence, no seeded RNG exist today** — and that is by design: each is a named, always-planned story below.

### What Full-Release ADDS (the always-planned stories that close each gap)

| Phase | Adds | Closes |
|---|---|---|
| **P0 — Platform seam** | Two-layer HAL (upper API + `hal_stm32.c`); fold both games' copy-pasted init/driver/ADC/GPIO into it | AD-2, ADR-2/-7 |
| **P1 — Shell + contract** | Shell/runtime owning `main()`/loop/hardware; `GameModule` contract; registry; **game-select menu + exit path** | AD-1, ADR-1 |
| **P2 — Port the two games** | spaceShip & paint refactored into cartridges (drop `main()`/driver); leak fix; cursor-colour fix; geometry fix; delete dead code | §6, AD-8 |
| **P3 — Shared services** | Seeded RNG; framebuffer + dirty-rect; **flash persistence** (high scores, drawings); debounced input edges; scheduler `dt` + bounded difficulty | AD-5/-6/-7, ADR-3/-4/-5/-6 |
| **P4 — Snake** | Greenfield Snake built native to the contract — the **reference cartridge** proving zero game-side HAL | §6.3 |
| **P5 — Perf** | **Enable the PLL** (16→100 MHz STM32, the cheap headline win); DMA display path | §9 |
| **P6 — RP2350 port** | HAL-swap to pico-sdk (§7 table); then **PIO** display offload, **DMA**, **dual-core** split, exploit 520 KB RAM | §7, AD-3 |

### What I'd do next (and why)
1. **P5's PLL flip first as a spike** — it's a one-line clock change for ~6× the headroom and turns the perf claim from theory into a measured before/after. Cheapest possible proof I profiled the right bottleneck.
2. **P0+P1 together** — the platform seam and the shell are co-dependent; landing them as a pair (with a `spi_write_blocking`-equivalent backend = parity with today) is the lowest-risk way to get a switchable, exitable console on screen.
3. **Snake (P4) early as the contract's acceptance test** — a brand-new game that needs *zero* driver code is the strongest possible evidence the cartridge model holds before I pay to port the two legacy games.
4. **Then RP2350 (P6)** — by then the HAL is the only thing that changes, so the port is the §7 swap table, not a rewrite — exactly what the abstraction was built to buy.

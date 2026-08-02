# SNAKE — Game Architecture

**Status:** Architecture / solution-design (pre-build) — greenfield, **0 lines of code today**
**Role:** the **reference cartridge** — the first game *born* to the console's lifecycle contract
**Authoritative target:** Raspberry Pi Pico 2 (RP2350) · today's reference clock: STM32F411RE @ 16 MHz
**Doc shape:** BMAD `create-architecture` (context → contract → data → RNG → render → timing → persistence → decisions → proof → portability → roadmap)
**Related:** [Snake PRD](./SNAKE-PRD.md) · [Console PRD](./CONSOLE-PRD.md) · [Console Architecture](./CONSOLE-ARCHITECTURE.md) · [Snake mockup](./mockups/snake.html)

> **Framing note (read first).** Snake has **no code yet** — every "TODAY" below describes the *target platform's* current reality (the committed STM32 firmwares and the proposed shell), and every "FULL-RELEASE ADDS" describes the always-planned extension. The two existing firmwares (`spaceShipGame`, `EmbeddedPaint`) are **deliberate proof-of-concept cartridges** that each prove a game loop drives the real ST7735 panel from real joystick/ADC input on real silicon — and each one does. The console *shell* and the *shared platform* that Snake depends on are the always-planned integrating layer; they are designed in [Console Architecture](./CONSOLE-ARCHITECTURE.md) and consumed here. Snake is the first artifact written on the *other* side of that line. Gaps are named and framed throughout, never hidden.

---

## 1. Context — greenfield, and the reference cartridge

### 1.1 Why a greenfield game gets an architecture doc

Snake is the **smallest game that proves the biggest idea.** Spaceship and Paint are **monolithic firmwares**: each owns `main()`, its own `while(1)` super-loop, every peripheral init, *and its own copy-pasted ST7735 + ADC + GPIO driver* — and the two driver copies have **already drifted** (the RGB565 pixel-write byte order is *opposite* between them: Spaceship writes `{lo, hi}`, Paint writes `{hi, lo}` — verified in source). There is **no shared driver layer and no way to exit a game.** That is *the* central architectural problem of building a 3-game console out of them.

Snake is built **native** to the solution: it implements `init / update(input, dt) / render / teardown` against the shared platform from line one, never owning `main()`, the loop, or a single peripheral. Because it is greenfield, it is the **clean room** in which the cartridge contract is validated *before* we risk breaking two working games to fit them into it. It doubles as the **reference implementation** the two refactors are checked against.

### 1.2 What Snake depends on (all owned by the shell/platform, never by Snake)

| Dependency | Owner | Snake's relationship |
|---|---|---|
| `main()` + the only `while(1)` | Shell / runtime | Snake never owns either; the shell *calls into* Snake |
| ST7735 display driver (one canonical, framebuffer-backed) | Display service | Snake writes a framebuffer; never speaks SPI |
| Joystick ADC + debounced buttons | Input service | Snake reads a normalized `Input` struct; never touches ADC/GPIO |
| Timing / scheduler (`dt`, fixed tick) | Scheduler | Snake's logic is framerate-decoupled |
| Seeded RNG | RNG service | Snake's food spawn is fair *and* reproducible |
| Flash key/value store | Persistence service | Snake's high score survives power-off |

Snake's entire surface area is **pure game logic over those services.** Owning any of them would recreate the very monolith problem Snake exists to disprove.

### 1.3 Greenfield is the de-risking move, not a shortcut

Building the reference *first* means the contract is forced to be concrete by a real consumer before either legacy game is touched. Contract churn surfaces **here, cheaply** — in a 0-to-1 game with nothing to break — instead of mid-refactor in Spaceship or Paint. That sequencing (build the reference, then refactor against it) is exactly why Snake is pulled forward right after the platform layer exists.

---

## 2. Implementing the lifecycle contract

The console contract (defined in [Console Architecture §4](./CONSOLE-ARCHITECTURE.md)) is a four-function vtable plus a name, over shell-owned shared structs. Snake is a value of that type. The shell allocates the context, feeds `Input` + `dt`, owns the framebuffer, and reclaims control when `update` returns `GAME_EXIT`.

| Contract function | Snake's responsibility | Critical invariant |
|---|---|---|
| `init(ctx)` | Allocate the **bounded** body store, seed the RNG via `ctx->rng_seed`, read the saved high score via `ctx->store_read`, place the starting snake + first food, set attract state. | All allocation happens **here**, once — never per-tick (the anti-leak rule). |
| `update(in, dt)` | Consume one buffered turn, advance one cell on a tick boundary, branch **grow vs move**, run collision, update score/speed. Return `GAME_EXIT` on the back-button. | Logic is **pure over the grid** — no rendering, no hardware, no `now_ms` side effects beyond reading the clock. |
| `render(fb)` | Draw **only the changed cells** into the shell's framebuffer (head, vacated tail, eaten/ new food, HUD digits). | Touches a handful of 8×8 cells per frame — never a full-screen redraw. |
| `teardown()` | Free **all** body/game state and return control to the menu, leaving the heap exactly as it was found. | Mandatory; the structural fix for Spaceship's leak (§3.4). |

### 2.1 Why `update`/`render` are split

Spaceship and Paint interleave logic and SPI writes inside one `while(1)` — Paint even has an *indirect render* where painted pixels only reach the panel when the cursor later moves off them (a fast stroke can outrun the redraw). Snake separates **advance the world** (`update`, pure, testable host-side with a fake platform) from **show the world** (`render`, the only place a framebuffer is touched). This separation is what lets the determinism tests (§4) run with no hardware at all, and what lets the display backend change (blocking → DMA) under Snake with zero game edits.

### 2.2 State machine (internal to Snake)

```
   ATTRACT ──(Button C)──▶ PLAYING ──(wall/self hit)──▶ GAME_OVER
      ▲                       │                              │
      │                  (back-btn)                     (Button C = restart)
      │                       │                              │
      └───────────────────────┴──────────────────────────────┘
                  back-button at any point → update() returns GAME_EXIT → teardown()
```

The **back-button → `GAME_EXIT`** edge is the game-exit path that *does not exist anywhere today* — Spaceship's only "reset" is its internal `GAMEON` state machine; there is no exit at all. Snake makes exit a first-class, contract-level transition.

---

## 3. Data model — grid + churn-free body

This is the heart of the architecture and the most concrete payoff.

### 3.1 The grid

| Property | Value | Source |
|---|---|---|
| Cell size | 8 px square | PRD §5 |
| Columns | 128 ÷ 8 = **16** | ST7735 width 128 |
| HUD strip | top 24 px reserved | PRD §5 |
| Rows (play) | (160 − 24) ÷ 8 = **17** | ST7735 height 160 |
| **Board capacity** | 16 × 17 = **272 cells** | bounds the body store |

A cell is a tiny `{ uint8_t col, row }`. The grid is *implicit* — there is no 272-entry occupancy array required for the core loop (collision is an O(length) body scan), though an optional occupancy bitset (272 bits = 34 bytes) makes self-collision and food-rejection O(1); that micro-optimization is a Full-Release nicety, not MVP.

### 3.2 The body: a fixed-capacity ring buffer of cells

The snake body is an **ordered, growing sequence** — the same abstract shape as Spaceship's obstacle list — but implemented with a **correct, bounded lifecycle**.

**Primary choice: a fixed-capacity ring buffer**, capacity = board size (272 cells). `head` and `tail` indices walk a `Cell[272]` array modulo capacity. `length` tracks live segments.

| Operation | Ring-buffer mechanics | Cost | Allocation |
|---|---|---|---|
| **Move** (non-eat tick) | push new head (advance `head`, write cell), pop old tail (advance `tail`) | O(1) | **zero** |
| **Grow** (ate food) | push new head only (`length++`), tail stays | O(1) | **zero** |
| **Self-collision** | scan live segments for the new head cell (or O(1) via optional bitset) | O(length) | zero |

The decisive property: **the array is allocated exactly once in `init`, sized to the maximum the board can ever hold, and never reallocated.** Overflow is *impossible by construction* — the snake can never be longer than 272 cells because that is the whole board. Heap churn during play is **exactly zero**.

### 3.3 Alternative considered: a proper linked list

A singly-linked list with a **node pool** (free-list of pre-allocated nodes) is the closer mirror of Spaceship's structure and is kept as a fallback behind the same `body` interface. It also achieves correct lifecycle — `move` returns the tail node to the pool and takes a node from it for the head (net-zero), `grow` takes from the pool without returning. The trade-off:

| | Ring buffer (chosen) | Linked list + pool |
|---|---|---|
| Allocation during play | **zero** | zero (pool churn only) |
| Cache locality | excellent (contiguous) | poor (pointer-chasing) |
| Per-segment overhead | 2 bytes (col,row) | 2 bytes + a `next` pointer |
| Resemblance to Spaceship's bug | conceptual | structural (same data shape) |
| Overflow safety | impossible by construction | needs pool-exhaustion handling |

The ring buffer wins on every embedded axis (no fragmentation, no pointers, contiguous, overflow-proof) and is the **strongest version of the fix.** The body interface (`body_push_head`, `body_pop_tail`, `body_contains`, `body_len`) hides the choice, so the linked-list variant can be swapped in without touching game logic — and so that the *contrast* with Spaceship's list is legible in the same codebase.

### 3.4 The explicit contrast with Spaceship's heap bug

This fix is grounded in the **actual Spaceship source** (`spaceShipGame/Core/Src/main.c`):

- `struct Node { int x; int y; struct Node* next; }` (line 72) — a singly-linked list.
- `createStar()` does `malloc(sizeof(struct Node))` on every spawn (line 617) and **prepends** it.
- Nodes that scroll off are **recycled by repositioning** (`temp->y == 0 → temp->x = rand() % 85`, line 647), **not freed**.
- `freeLinkedList()` is called **only at game-over** (line 212).
- Net effect: the list **never shrinks during play**, so the heap **grows monotonically** on a long session. (`srand()` count in the file: **0** — also never seeded, so every power-up is the identical star pattern.)

| | Spaceship (verified TODAY) | Snake (by design) |
|---|---|---|
| Per-spawn / per-tick allocation | `malloc` per spawn, never matched by a `free` during play | **none** — fixed array sized once |
| Shrink during play | never (recycle-but-keep) | tail genuinely vacates each non-eat move |
| Free timing | only at game-over | `teardown` frees everything; nothing leaks across enter/exit |
| Bound on growth | unbounded heap | hard-bounded at 272 cells (the board) |

> **PoC framing.** Spaceship's leak is **not** "the game is broken" — it runs fine for a normal session. It is the **edge of the copy-paste, monolith-per-game model**, which is exactly the model the console replaces. Snake demonstrates the *right* way to do the exact data structure Spaceship got wrong — same problem (an ordered growing collection on a tiny MCU), correct solution (bounded, churn-free lifecycle) — so the contrast is a teaching artifact, not a criticism. When Spaceship is later refactored into a cartridge, its star list adopts this same bounded discipline, closing its leak as a side effect.

---

## 4. RNG — the shared seeded service

Food placement is where "deterministic-fair" becomes concrete code.

- **Snake never calls `rand()` directly.** It uses `ctx->rng()` / `ctx->rng_seed()` from the shared RNG service. `init` seeds it once at boot from a non-deterministic source (a floating/unconnected ADC channel on STM32, or the RP2350 hardware entropy source).
- **This closes a verified bug.** Spaceship uses `rand()` but **never calls `srand()`** (confirmed: 0 occurrences in its `main.c`), so its star pattern is *identical every single power-up*. Snake gets real per-power-cycle variety **and** per-seed reproducibility — both, from one owned service. Determinism becomes a *choice* (fixed seed for dev/replay, entropy seed for play), not an accident.
- **Rejection sampling, never on the snake.** A candidate food cell is drawn uniformly from the 272-cell grid and **rejected if occupied by any body segment**; redraw until free. Cheap until the snake is enormous, at which point a free-cell scan is the bounded fallback. Food never lands under the snake — a fairness invariant true *by construction*.
- **One food at a time.** Eating spawns the next; the board state stays trivially checkable.

> **Why seeded-and-reproducible is the right call, not just a bug fix:** it makes the game *testable*. A fixed seed plus a scripted input sequence yields a deterministic board, so a regression test can assert exact post-conditions (length, head position, score, food cell) at tick K — turning "is the game fair?" into an executable check rather than a vibe.

---

## 5. Rendering — dirty-cell, not full-screen

Snake renders through the **shared display service**, writing into the shell-owned framebuffer; it never speaks SPI and never assumes a backend.

### 5.1 Dirty-cell strategy

The genius of Snake's render is that **a move changes only a tiny, fixed set of cells**:

| Event | Cells that change | Cells to redraw |
|---|---|---|
| Move (non-eat) | new head cell, vacated tail cell | **2** (+ optionally the segment behind the head if a corner) |
| Grow (eat) | new head cell, eaten food cell, new food cell | **3** |
| Score / HI change | HUD digit cells only | a few 8×8 glyph cells |

So Snake marks **only changed grid cells dirty** (`fb->mark_dirty(x, y, 8, 8)`) and the display service flushes just those rectangles. There is **no full-screen redraw per frame** — the polar opposite of Spaceship (which erases/redraws every star object every loop) and of Paint's per-pixel restore. This is why Snake hits the ≥30 FPS budget naturally: its per-frame pixel count is a handful of 8×8 blocks regardless of snake length.

### 5.2 Why this rides on the shared driver, not a Snake driver

The display service owns the **one canonical ST7735 driver** that resolves the byte-order split (Spaceship `{lo,hi}` vs Paint `{hi,lo}`) and the INVON-on colour tuning. Snake writes RGB565 values into `fb->pixels` and trusts the service to get the byte order right on the wire. **TODAY** the panel is driven byte-at-a-time over blocking `HAL_SPI_Transmit` at 8 MHz SCK with no DMA; **FULL-RELEASE ADDS** a DMA-fed framebuffer flush (PIO-offloaded on RP2350). Because Snake only marks dirty cells and writes pixels, *that backend change happens entirely under Snake with zero game edits.*

### 5.3 Palette (from the mockup)

Drawn to match [`mockups/snake.html`](./mockups/snake.html): dark board, faint grid, body green, head light-green, food red, white HUD text — all RGB565 constants the cartridge owns as data (so "skins" in Full-Release is a pure parameterization, not a rewrite).

---

## 6. Timing & speed ramp — the shared scheduler

Snake's logic is **framerate-decoupled** because the scheduler delivers `dt` to `update(in, dt)`; Snake accumulates `dt` and advances the world on a **tick boundary**, not on raw loop speed.

| Mechanic | Behavior | Rationale |
|---|---|---|
| **Tick cadence** | Snake holds a `tick_interval_ms`; `update` accumulates `dt` and steps the snake when the accumulator crosses the interval. | Decouples game speed from CPU speed — identical behavior whether the clock is 16 MHz STM32 or 150 MHz RP2350. |
| **Speed ramp** | `tick_interval_ms` shortens at length thresholds (e.g. start ~6 cells/sec, step down every N food). | Self-balancing: a longer snake is faster *and* harder to route — pressure rises with no separate level system. |
| **Bounded floor** | The interval is **clamped to a minimum** (fastest playable). It never reaches zero. | A *deliberate fix* for a verified Spaceship bug: its difficulty does `Delay -= 1` every difficulty tick with **no lower bound** (`Delay` starts at 300, trends toward 0/negative — confirmed in source). Snake's ramp is monotone-decreasing **with a clamp** — fair, not impossible. |

> **PoC framing.** Spaceship's unbounded difficulty is the same "edge of the proof-of-concept" story: it works for a normal-length session, but the curve was never clamped because clamping wasn't what the PoC needed to prove. Snake's clamped, `dt`-driven ramp is the always-planned correct version, expressible only once a shared scheduler exists.

---

## 7. Persistence — high score via the shared flash service

| Aspect | Design |
|---|---|
| **What persists** | One best-score integer for Snake, via the shared persistence service's keyed store: `ctx->store_write("hiscore.snake", ...)`. |
| **When written** | Only on a *new* high score at game-over — minimize flash wear, not every run. |
| **When read** | Once in `init`, to populate the HUD **HI** field. |
| **Backend** | STM32: an internal-flash page via HAL flash. RP2350: a reserved QSPI-flash region via `hardware/flash`. The service hides flash geometry and wear handling behind `store_read`/`store_write` — Snake is ignorant of both. |

> **Honesty note — there is no persistence in any code today.** Both shipping firmwares lose all state at power-off. Flash-backed high scores are a named **FULL-RELEASE** item owned by the shared persistence service (see [Console Architecture §3.8](./CONSOLE-ARCHITECTURE.md)), **not** something Snake reimplements alone. In the MVP, `store_read` returning "no key" simply yields `HI = 0`; the game is fully playable without persistence, the score just doesn't survive a reboot until the service lands.

---

## 8. Decisions & rationale (ADRs)

**ADR-S1 — Body = fixed-capacity ring buffer sized to the board (272 cells).**
*Decision:* allocate the body once in `init` as a `Cell[272]` ring; move = push-head/pop-tail, grow = push-head; never reallocate.
*Rationale:* head-push/tail-pop *is* the game, and a board-sized ring makes it O(1) with **zero heap churn** and **overflow impossible by construction**. It is the strongest possible fix for Spaceship's monotonic-heap bug, in the same data-structure family, so the contrast is legible.
*Tradeoff:* 272 × 2 bytes = 544 bytes reserved up front even when the snake is short — trivial against the 40 KB framebuffer; a non-issue.

**ADR-S2 — Body behind a small interface; linked-list+pool kept as a swappable alternate.**
*Decision:* game logic calls `body_push_head/pop_tail/contains/len`; the ring is the default implementation, a pooled linked list is an interchangeable one.
*Rationale:* lets us demonstrate "the *same* growing-sequence shape Spaceship got wrong, done right" in *both* the contiguous and the linked form, and keeps the choice a localized decision. *Tradeoff:* one indirection through an interface — negligible, and it buys testability and the teaching contrast.

**ADR-S3 — Seeded RNG only; never `rand()` directly.**
*Decision:* food spawn draws from `ctx->rng()`, seeded once in `init`.
*Rationale:* closes Spaceship's unseeded-`rand()` bug (verified: 0 `srand` calls) and makes determinism a *choice* — reproducible per seed (testable), varied per power-up (playable). *Tradeoff:* none.

**ADR-S4 — Dirty-cell rendering, never a full-frame redraw.**
*Decision:* mark only changed 8×8 cells dirty; let the display service flush just those.
*Rationale:* a Snake move touches ~2–3 cells regardless of length, so the render is naturally tiny and hits ≥30 FPS without DMA — and rides the shared driver, so a future DMA/framebuffer backend needs zero Snake edits. *Tradeoff:* must track the vacated-tail and corner cells precisely; a small bookkeeping cost, well worth it.

**ADR-S5 — Scheduler-delivered `dt` with a clamped speed floor.**
*Decision:* step on a `dt`-accumulated tick boundary; clamp `tick_interval_ms` to a minimum.
*Rationale:* framerate-decoupling makes behavior identical across 16 MHz STM32 and 150 MHz RP2350; the clamp fixes Spaceship's unbounded `Delay -= 1`. *Tradeoff:* none meaningful.

**ADR-S6 — Reject the 180° reversal; queue exactly one buffered turn.**
*Decision:* a turn directly into the snake's own neck is ignored; at most one pending direction change is queued and applied at the next tick boundary.
*Rationale:* 180° reversal is an instant self-collision that feels like a misfire — rejecting it is both classic-correct and a fairness rule. One buffered turn makes a fast "down-then-right" corner register reliably at speed without letting an input flood reverse the snake. The crisp-controls pillar, directly. *Tradeoff:* a player who mashes two turns in one tick loses the second — the right call (a single-turn-per-tick game should not let inputs stack arbitrarily).

**ADR-S7 — All allocation in `init`, all release in `teardown`; nothing per-tick.**
*Decision:* the body store and any buffers are allocated once in `init` and freed in `teardown`; `update` never allocates.
*Rationale:* structurally guarantees a flat heap across a long session *and* across N enter/exit cycles — the explicit anti-regression against Spaceship's free-only-at-game-over pattern. *Tradeoff:* none.

---

## 9. How Snake proves the cartridge model

Snake is not "just the third game" — it has a **structural job** in the console. It is the executable proof that a game written *only* against the contract + services needs **zero** driver/HAL code.

| Proof obligation | How Snake discharges it |
|---|---|
| **The contract is real and sufficient** | Snake runs entirely through `init/update/render/teardown` with **no** game-owned `main()`, `while(1)`, or direct peripheral access — verifiable by inspection. If the contract were missing something, Snake (the first native consumer) would expose it *before* either legacy game is refactored. |
| **The exit path works** | Back-button → `update` returns `GAME_EXIT` → `teardown` frees all state → control returns to the shell menu — the path that exists *nowhere* today. |
| **The heap stays flat** | ADR-S1/S7 make heap usage flat across a long run and across N enter/exit cycles — the direct anti-regression to Spaceship's monotonic growth. |
| **Services are enough** | Snake consumes the shared RNG, scheduler, display, input, and persistence services and owns *none* of them — proving the platform API is a complete game-facing surface. |
| **The template the refactors aim at** | When Spaceship and Paint are refactored into cartridges, Snake is the worked example they are shaped to match — including bringing Spaceship's star list onto the same bounded body discipline. |

This is why the build order pulls Snake forward right after the platform layer exists: **build the reference, then refactor the legacy games against it.** The cheapest possible way to de-risk two game refactors is to first prove the target shape with a game that has nothing to lose.

---

## 10. Pico / RP2350 notes

Snake is **target-agnostic by construction** — it touches only the platform service API, so the STM32 → RP2350 move is a HAL-swap *below* Snake, never a Snake change (see [Console Architecture §7](./CONSOLE-ARCHITECTURE.md) for the full swap table). Concretely:

| Concern | STM32 reference (TODAY) | RP2350 (TARGET, pico-sdk) | Effect on Snake |
|---|---|---|---|
| Clock | HSI 16 MHz, **PLL off** | 150 MHz default | none (logic is `dt`-decoupled, ADR-S5) |
| RNG seed entropy | floating/unconnected ADC read | hardware entropy source | none (via `ctx->rng_seed`) |
| Display flush | byte-at-a-time blocking SPI @ 8 MHz | **PIO** state machine + **DMA** | none (Snake only marks dirty cells) |
| Persistence | internal-flash page (HAL) | reserved QSPI region (`hardware/flash`) | none (via `store_read/write`) |
| Timing | `HAL_GetTick()` | `pico/time` `time_us_64` | none (via scheduler `dt`) |

**RP2350 headroom Snake never strains:** 520 KB RAM (the 40 KB framebuffer + Snake's 544-byte body fit trivially, leaving room for double-buffering and a full occupancy bitset), dual Cortex-M33 @ 150 MHz (the display flush could pin to core 1 while Snake's logic runs on core 0 — a clean producer/consumer split the framebuffer-ownership model already enables), and PIO to offload the display byte stream. None of these are needed for Snake to be smooth; they are headroom the reference design leaves on the table.

> **Honesty note.** DMA, PIO, dual-core, the PLL flip, and 150 MHz operation are **FULL-RELEASE / roadmap** items — *none* exist in code today (today's reference firmware runs at 16 MHz with the PLL off, blocking byte-at-a-time SPI, no DMA). Snake's value is precisely that it can be written *now* against the abstraction and inherit every one of those wins later for free.

---

## 11. MVP vs Full-Release

### 11.1 MVP — the first Snake build (the reference cartridge)

*Snake has no code today, so "MVP" means the first shippable build, not current behavior.*

| Capability | In MVP? |
|---|---|
| Grid movement on an 8 px grid (16 × 17 play area) | ✅ |
| Eat food → grow one cell | ✅ |
| 4-direction joystick steering (dominant-axis quantize) + 180°-reversal reject + one buffered turn | ✅ |
| Self + wall collision → game over | ✅ |
| Score = food eaten (a *meaningful* counter); on-screen SCORE + HI | ✅ |
| Bounded speed ramp (clamped minimum tick) | ✅ |
| **Seeded** RNG food spawn with on-snake rejection | ✅ |
| Built to the `init/update/render/teardown` contract, with a working **exit-to-shell** path | ✅ (the reference job) |
| **Churn-free body lifecycle** (ring buffer; bounded; zero per-tick alloc) | ✅ (the heap-bug fix) |
| Dirty-cell rendering through the shared display service | ✅ |
| Flash high-score persistence | ❌ — Full-Release, via the shared service (§7); MVP shows `HI = 0` until then |

**MVP non-goals (by design):** no levels, no obstacles/maze, no wrap-around, no second player, no skins, no audio (no audio hardware exists). Out so the MVP stays the *smallest thing that proves the contract and the body lifecycle.*

### 11.2 Full-Release — what later versions ADD

| Addition | What it is | Why deferred |
|---|---|---|
| **Flash high-score persistence** | `hiscore.snake` survives power-off | Owned by the shared persistence service, which is itself a Full-Release platform item |
| **Levels** | Stages with rising start-speed / target-length | Depth on a proven loop — needs the MVP loop solid first |
| **Obstacles / maze** | Static wall cells inside the play area | Reuses the wall-collision rule against arbitrary cells — cheap once the grid/collision core is proven |
| **Wrap-around mode** | Edges teleport the head instead of killing | A toggle on the wall-collision rule (off in MVP for classic difficulty) |
| **2-player** | Two snakes, two sticks; last alive wins | New input + snake-vs-snake collision; natural on RP2350's dual core |
| **Skins** | Alternate RGB565 palettes | Pure parameterization once render is data-driven |

> Every Full-Release item is a *named* extension of an MVP system — wrap toggles one rule, obstacles reuse one collision check, skins parameterize the palette. That additions *clip on* is the test of a good MVP boundary.

### 11.3 What I'd do next (engineer's note)

1. **Nail the body structure first** (ring buffer, capacity = board size) behind a tiny `body` interface, and write the **flat-heap test before anything else** so the anti-regression — the headline "reference fix" — is locked from line one.
2. **Implement the pure logic against the contract with a fake platform** — `update(input, dt)` over an in-memory grid, no hardware. With a seeded RNG and scripted input, assert exact board states *host-side* before touching the panel. The determinism payoff doing real work.
3. **Wire to the shared display + input services** and render the [mockup](./mockups/snake.html) for real — HUD strip, green snake, light-green head, red food, faint grid.
4. **Prove the exit path** — enter Snake from the shell, play, back-button → `teardown()` → menu, in a loop, watching the heap stay flat. That single test demonstrates the *entire* cartridge contract working.
5. **Then layer Full-Release width** — persistence, wrap, obstacles, skins — each clipping onto one existing system.

---

## 12. Appendix — Snake implementing the contract (pseudocode)

```c
/* Snake as a cartridge. It owns NO main(), NO while(1), NO peripheral.
 * It speaks only the shell-owned Input / FrameBuffer / GameContext (see
 * Console Architecture §4). All allocation in init(); all release in teardown().
 */

#define COLS 16
#define ROWS 17
#define CAP  (COLS * ROWS)          /* 272 — board size bounds the body  */
#define HUD_PX 24
#define CELL 8

typedef struct { uint8_t col, row; } Cell;
typedef enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT } Dir;

/* Fixed-capacity ring buffer — allocated ONCE, never realloc'd, overflow impossible. */
static struct {
    Cell    ring[CAP];              /* contiguous; zero heap churn during play         */
    uint16_t head, tail, len;       /* indices walk mod CAP                            */
    Dir      dir, queued;           /* one buffered turn (ADR-S6)                      */
    Cell     food;
    uint32_t tick_ms, acc_ms;       /* dt-accumulated cadence (ADR-S5)                 */
    uint32_t score, hi;
    enum { ATTRACT, PLAYING, OVER } phase;
} S;

static GameContext *G;              /* services only; NEVER raw HAL                    */

/* ── body interface (ADR-S2): ring is the default impl ───────────────────────── */
static void  body_push_head(Cell c){ S.head=(S.head+1)%CAP; S.ring[S.head]=c; S.len++; }
static void  body_pop_tail (void)  { S.tail=(S.tail+1)%CAP; S.len--; }
static bool  body_contains (Cell c){
    for (uint16_t i=0,j=(S.tail+1)%CAP; i<S.len; i++,j=(j+1)%CAP)
        if (S.ring[j].col==c.col && S.ring[j].row==c.row) return true;
    return false;
}

static void spawn_food(void){       /* seeded RNG + on-snake rejection (ADR-S3)       */
    Cell c;
    do { c.col = G->rng() % COLS; c.row = G->rng() % ROWS; } while (body_contains(c));
    S.food = c;
}

/* ── init: ALL allocation here, seed RNG, load HI, place snake (ADR-S7) ────────── */
static void snake_init(GameContext *ctx){
    G = ctx;
    G->rng_seed(/* entropy: floating ADC / RP2350 HW source */ entropy_seed());
    G->store_read("hiscore.snake", &S.hi, sizeof S.hi);     /* 0 if no key (MVP)     */
    S.head = S.tail = 0; S.len = 1; S.ring[0] = (Cell){8, 8};
    S.dir = S.queued = DIR_RIGHT; S.tick_ms = 160; S.acc_ms = 0;
    S.score = 0; S.phase = ATTRACT;
    spawn_food();
}

/* ── update: pure logic over the grid; returns GAME_EXIT for the shell ─────────── */
static GameStatus snake_update(const Input *in, uint32_t dt_ms){
    if (in->btn_pressed & BTN_BACK) return GAME_EXIT;        /* the exit path (§2.2)  */
    if (S.phase != PLAYING){
        if (in->btn_pressed & BTN_C){ if(S.phase==OVER) snake_init(G); S.phase=PLAYING; }
        return GAME_RUNNING;
    }

    /* dominant-axis quantize; reject 180° reversal; queue ONE turn (ADR-S6) */
    Dir want = quantize_stick(in->stick_x, in->stick_y, S.dir);
    if (!is_reverse(want, S.dir)) S.queued = want;

    S.acc_ms += dt_ms;                                       /* dt-paced tick (ADR-S5)*/
    if (S.acc_ms < S.tick_ms) return GAME_RUNNING;
    S.acc_ms -= S.tick_ms;
    S.dir = S.queued;

    Cell nh = step(S.ring[S.head], S.dir);                  /* prospective new head  */

    if (off_board(nh)) { S.phase = OVER; on_game_over(); return GAME_RUNNING; }

    bool ate = (nh.col==S.food.col && nh.row==S.food.row);
    if (!ate) body_pop_tail();                               /* MOVE: tail vacates    */
    if (body_contains(nh)) { S.phase = OVER; on_game_over(); return GAME_RUNNING; }
    body_push_head(nh);                                      /* GROW or MOVE: 0 alloc */

    if (ate){
        S.score++; spawn_food();
        if (S.tick_ms > MIN_TICK_MS) S.tick_ms -= RAMP_STEP; /* clamped floor         */
    }
    return GAME_RUNNING;
}

/* ── render: dirty-cell only — touches ~2-3 cells per move (ADR-S4) ────────────── */
static void snake_render(FrameBuffer *fb){
    draw_cell(fb, S.ring[S.head], COL_HEAD);                 /* new head              */
    draw_cell(fb, S.food,         COL_FOOD);
    /* the vacated tail cell was cleared on pop; HUD digits redraw only on change    */
    draw_hud_if_changed(fb, S.score, S.hi);
    /* each draw_cell calls fb->mark_dirty(x,y,8,8); the shared service flushes those */
}

/* ── teardown: free ALL state — flat heap across enter/exit (ADR-S7) ───────────── */
static void snake_teardown(void){
    /* ring is static/once-allocated; nothing leaks. If pooled-list impl: free pool. */
    G = 0;
}

const GameModule game_snake = {
    .name = "SNAKE",
    .init = snake_init, .update = snake_update,
    .render = snake_render, .teardown = snake_teardown,
};
```

*Note: the snippet is illustrative pseudocode — `S` is shown `static` for clarity; in the real build the body store and game state are allocated in `init` and freed in `teardown` per ADR-S7 so the heap is verifiably flat across N enter/exit cycles.*

---

*See also: [Snake PRD](./SNAKE-PRD.md) · [Console PRD](./CONSOLE-PRD.md) · [Console Architecture](./CONSOLE-ARCHITECTURE.md) · [Snake mockup](./mockups/snake.html)*

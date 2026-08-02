# SNAKE — Game Design Document & PRD

> The third PICO-CADE game and the **reference cartridge** for the console.
> Greenfield (no firmware yet), built *natively* to the game-module lifecycle
> contract so it proves the cartridge model end-to-end — and gives its growing
> body a **proper node lifecycle** that fixes Spaceship's verified
> unbounded-heap bug *by design*.
>
> | | |
> |---|---|
> | **Status** | Greenfield — design only, 0 lines of code today |
> | **Role** | Reference implementation of the [cartridge contract](./CONSOLE-PRD.md#8-the-cartridge-model) |
> | **Genre** | Classic grid-based Snake (single-player) |
> | **Display** | ST7735 128×160 RGB565 over SPI — **8 px grid → 16 cols × 17 rows** play area |
> | **Input** | 2-axis analog joystick → 4-direction steering + one button (start/pause) |
> | **Console target** | Raspberry Pi Pico 2 / RP2350 (today's reference clock: STM32F411 @ 16 MHz) |
> | **Related docs** | [Console PRD](./CONSOLE-PRD.md) · [Snake Architecture](./SNAKE-ARCHITECTURE.md) · [Console Architecture](./CONSOLE-ARCHITECTURE.md) · [Snake mockup](./mockups/snake.html) |

---

## 1. Vision & Pillars

**Snake is the small game that proves the big idea.** It is the one game on
PICO-CADE that is *born* as a cartridge: it never owns `main()`, never owns the
`while(1)`, never touches a peripheral directly. It implements the shared
`init / update(input, dt) / render / teardown` contract from line one. Because
it is greenfield, it has no monolith to refactor and no legacy driver to
reconcile — so it is the **clean room** in which the contract is validated
before Spaceship and Paint are refactored against it.

It is also classic Snake done right: a green snake on a dark grid, eat the food,
grow one cell, speed up as you get longer, don't hit your own tail or the wall.
That is a game everyone recognizes in two seconds, which makes it the perfect
demo on a handheld.

### Design pillars

| Pillar | What it means | How it shows up |
|---|---|---|
| **Classic** | Instantly legible Snake. No tutorial, no surprises. | One green snake, one red food, a score and a high score. Two-second comprehension. |
| **Crisp** | Tight, grid-locked, responsive controls. Movement snaps to cells; input is debounced and buffered, never dropped. | 8 px grid, fixed tick cadence, one queued turn so a fast double-tap never eats an input. |
| **Deterministic-fair** | Every death is the player's fault, never the engine's. Randomness is **seeded** and reproducible; food never spawns on the snake. | Shared **seeded RNG** (fixes Spaceship's unseeded-`rand()` bug); food-spawn rejects occupied cells; bounded speed ramp. |

> **Why these three:** they are the pillars I can *defend in an interview*.
> "Classic" sets the scope tight. "Crisp" is the one thing a grid game must get
> right (input feel). "Deterministic-fair" is the engineering pillar — it is
> where Snake quietly corrects three measured weaknesses in the existing
> firmware (unseeded RNG, unbounded difficulty, monotonic heap growth) and turns
> "fair" from a feeling into a property I can point at in the code.

---

## 2. Core Loop

The loop is the whole game; everything else is dressing.

```
        ┌──────────────────────────────────────────────┐
        │  read joystick → resolve to ONE of 4 dirs     │
        │  (ignore 180° reversal into own neck)          │
        └───────────────────────┬──────────────────────┘
                                ▼
        ┌──────────────────────────────────────────────┐
        │  on each tick: advance head one cell forward   │
        └───────────────────────┬──────────────────────┘
              ┌──────────────────┴───────────────────┐
        ate food?                              didn't eat?
              ▼                                       ▼
   ┌────────────────────┐              ┌────────────────────────┐
   │ GROW: keep tail,    │              │ MOVE: push new head,    │
   │ push new head.      │              │ pop old tail (length    │
   │ +score, spawn food, │              │ constant).              │
   │ maybe speed up.     │              └───────────┬────────────┘
   └─────────┬──────────┘                          │
             └──────────────┬───────────────────────┘
                            ▼
        ┌──────────────────────────────────────────────┐
        │  collision check: new head ∈ wall OR body?    │
        │     yes → LOSE (game over)   no → continue     │
        └──────────────────────────────────────────────┘
```

The "grow vs. move" branch is the entire mechanical heart of Snake, and it maps
**one-to-one** onto the body data structure (§9): *move* = push-head + pop-tail
(net zero allocation); *grow* = push-head only (no pop). This is exactly where
the data-structure decision pays off — see §9 and the
[Snake Architecture](./SNAKE-ARCHITECTURE.md) for the ring-buffer/linked-list
contract.

---

## 3. Controls

The console exposes the **shared input service** (one debounced read per frame),
not raw hardware. Snake consumes its abstraction:

| Input | Action | Notes |
|---|---|---|
| **Joystick — left / right / up / down** | Steer the snake. The analog stick is quantized to the **single dominant axis** (whichever of \|x\|, \|y\| is larger past the deadzone) and mapped to one of four discrete directions. | Diagonals are deliberately *not* a thing — Snake is 4-way only. |
| **Button C** | Start (from attract), and Pause / resume (during play). | Matches the [mockup](./mockups/snake.html): a single accent button labelled **C**. |
| **Button C (on game-over)** | Restart. | Returns to a fresh run; high score persists. |
| **Reserved back-button** | Quit → `teardown()` → return to the shell menu. | This is the **cartridge exit path** the console contract provides and the standalone firmwares lack. |

### Control decisions, with rationale

- **Quantize-to-dominant-axis, not raw thresholds.** The existing firmware reads
  two ADC axes with a brittle "one `HAL_ADC_Start`, two back-to-back reads that
  rely on the scan-sequencer order" pattern — and the two repos even *contradict
  each other* on which physical pin is X vs. Y (Spaceship's comments say X→PA1 /
  Y→PB1; Paint's `pin_layout.txt` says A1→Y / PB1→X). Snake never sees that mess:
  it asks the shared input service for a **normalized stick vector** with one
  canonical axis mapping (the unification job lives in the platform layer, see
  [Console PRD §6.3](./CONSOLE-PRD.md#63-shared-input-service-fr-i)) and picks the
  dominant axis. The ambiguity is resolved *once*, in the service, not per game.
- **Reject the 180° reversal.** A turn directly back into the snake's own neck is
  ignored (it would be an instant self-collision and feels like a misfire, not a
  death). This is a classic-Snake rule and a fairness rule both.
- **One buffered turn.** At most one pending direction change is queued between
  ticks, applied at the next tick boundary. This makes a quick "down-then-right"
  corner reliably register at speed without letting an input flood reverse the
  snake. Crisp-pillar, directly.

---

## 4. Progression & Difficulty

Snake's only difficulty axis is **speed**, and length *is* the difficulty meter:
the longer you are, the faster you go and the more of the board you occupy.

| Mechanic | Behavior | Rationale |
|---|---|---|
| **Speed ramp** | The tick interval shortens as length grows — e.g. start ~6 cells/sec, step the cadence down at length thresholds (every N food eaten). | Self-balancing: a longer snake is both faster *and* harder to route, so the player feels the pressure rise without a separate level system. |
| **Bounded floor** | The tick interval is **clamped to a minimum** (a fastest-playable speed). It never reaches zero. | This is a *deliberate fix* for a verified Spaceship bug: its difficulty does `Delay -= 1` every difficulty tick with **no lower bound**, trending toward 0/negative. Snake's ramp is monotone-decreasing **with a clamp** — fair, not impossible. |
| **Deterministic tuning** | Speed thresholds are fixed constants, and food placement is seeded, so a given seed yields a reproducible run. | Makes the game *tunable and testable* — I can replay a seed to balance the curve, and a regression test can assert "this seed → this board state at tick K." |

> **Decision — speed-only, no levels in the MVP.** A level/stage system, mazes,
> and obstacles are real Snake variants, but they are *width*, not *depth*, of
> the core loop. The MVP proves the loop and the cartridge contract; the variants
> are Full-Release (§7) where they earn their place. Right-sizing the design to
> what the proof needs.

---

## 5. HUD & Score

A thin HUD strip across the top, play area below — exactly as rendered in the
[mockup](./mockups/snake.html).

| Element | Value | Source |
|---|---|---|
| **SCORE** | Current run score. | `+1 per food eaten` — a *meaningful* counter (see decision below). |
| **HI** | Best score this power-cycle, persisted to flash. | Persistence service (§8 / Full-Release). |
| **Play area** | 8 px grid below the HUD strip (top 24 px reserved for HUD). | 128÷8 = **16 columns**; (160−24)÷8 = **17 rows** of play. |

### The score decision (and why it matters)

Snake scores **one point per food eaten** — score equals food count equals
`length − initial length`. That sounds trivial, but it is a *deliberate
correction* of the headline core-loop gap in Spaceship, where `score += 1` runs
**every main-loop iteration** while playing — making the "score" a
survival-tick counter, not a measure of skill, and diverging from the stated
design intent (tally dodges). Snake's score is tied to a **player action**
(eating), so the number on screen means what the player thinks it means. It is a
small thing that signals the larger discipline: *the displayed metric must match
the intent.*

---

## 6. Food Spawn — Seeded RNG

Food placement is where "deterministic-fair" becomes concrete code.

- **FR — seeded RNG.** Food cells come from the **shared seeded RNG**, seeded
  once at boot from a non-deterministic source (a floating/unconnected ADC read
  on STM32, or the RP2350 hardware entropy source). This directly closes the
  verified Spaceship bug: it **never calls `srand()`**, so its star pattern is
  *identical every single power-up*. Snake gets real variety per power-cycle and
  *reproducibility per seed* — both, from one service.
- **FR — rejection sampling, never on the snake.** A candidate cell is drawn
  uniformly from the grid and **rejected if occupied by any body segment**;
  redraw until free. With a 16×17 = 272-cell board this is cheap until the snake
  is enormous, at which point a free-cell scan is the bounded fallback. Food
  never lands under the snake — a fairness invariant, testable by construction.
- **FR — one food at a time.** Exactly one food exists; eating it spawns the
  next. Simple, classic, and keeps the board state trivially checkable.

> **Why seeded-and-reproducible is the right call, not just a bug fix:** it makes
> the game *testable*. A fixed seed plus a scripted input sequence produces a
> deterministic board, so the test suite can assert exact post-conditions
> (length, head position, score) — turning "is the game fair?" into an
> executable check rather than a vibe.

---

## 7. Collision & Win/Lose Rules

Snake has exactly two ways the run ends, and both are **the player's fault** —
that is the deterministic-fair pillar enforced in the rules.

| Rule | Definition | Outcome |
|---|---|---|
| **Wall collision** | After advancing, the new head cell is outside the play grid (col < 0, col ≥ 16, row < 0, or row ≥ 17 below the HUD). | **LOSE** — game over. (MVP is *no wrap-around*; wrap is a Full-Release mode, §8.) |
| **Self collision** | The new head cell equals any existing body cell. | **LOSE** — game over. |
| **Order-of-evaluation** | Collision is checked **against the post-move body**. On a non-eat tick the tail vacates *before* the check, so chasing your own tail into the cell it just left is **legal** — classic-correct behavior. | continue |
| **Food eaten** | The new head cell equals the food cell. | **GROW** (+1 length, +1 score), spawn next food, possibly speed up. |

### Win condition

- **MVP: there is no "win" — it's an endless survival-score chase.** The implicit
  win is the high score. This is the honest, classic framing.
- **Theoretical board-fill win (handled, not pursued):** if the snake ever fills
  every free cell (272-cell board), there is no legal move — the engine treats a
  full board as a **victory** rather than a forced self-collision, so a perfect
  run ends in a win screen, not a crash. Edge case covered by design even though
  it's astronomically rare on this board.

### Game-over flow

On a losing collision: freeze the board, show a game-over overlay with the final
score and (if beaten) a "NEW HI!" flag, and wait for **Button C** to restart or
the **back-button** to `teardown()` to the shell. No power-cycle required to play
again — another gap the standalone firmwares have (Spaceship's only reset path is
its internal state machine; there's no exit *at all*).

---

## 8. High-Score Persistence (Flash)

| Aspect | Design |
|---|---|
| **What persists** | A single best-score integer for Snake (and, console-wide, Spaceship's best and Paint's saved drawings) via the shared **persistence service** — a small key/value store backed by on-chip flash. |
| **When written** | Only on a *new* high score at game-over (minimize flash wear), not every run. |
| **Read** | Once at `init()`, to populate the **HI** field of the HUD. |
| **STM32 today vs. RP2350** | Persistence is a **Full-Release** feature on either target — neither standalone firmware persists anything today. On STM32F411 it's an internal-flash page; on the RP2350 it's a reserved region of the QSPI flash via the pico-sdk flash API. |

> **Honesty note:** there is **no persistence in any code today** — both shipping
> firmwares lose all state on power-off. Flash-backed high scores are a named
> Full-Release item, owned by the shared persistence service in the
> [Console PRD §6.7](./CONSOLE-PRD.md#67-persistence-fr-p), not something Snake
> reimplements alone.

---

## 9. The Reference-Cartridge Angle (why Snake exists where it does)

This is the section a hiring manager should read first. Snake is not "just the
third game" — it has a **structural job** in the console.

### 9.1 Snake validates the cartridge contract

Spaceship and Paint are **monolithic firmwares**: each owns `main()`, its own
`while(1)`, all peripheral init, and its **own copy-pasted ST7735 + ADC + GPIO
driver**. There is no shared layer and **no way to exit a game** — that is the
central architectural problem of building a 3-game console out of them (see
[Console PRD §2](./CONSOLE-PRD.md#2-problem--opportunity)).

Snake is built **native** to the solution: it implements
`init / update(input, dt) / render / teardown` against the shared platform from
the start. Because it is greenfield, it is the **clean reference** the two
refactors are checked against — proving the contract is real *before* I risk
breaking two working games to fit them into it. That sequencing (build the
reference, then refactor against it) is the de-risking move; it's why I'd pull
Snake forward right after the platform layer exists (see
[Console PRD §13](./CONSOLE-PRD.md#13-what-id-do-next-engineers-note)).

### 9.2 Snake fixes Spaceship's heap bug *by design*

This is the most concrete payoff, and it is grounded in the actual Spaceship
source:

- **The verified bug.** Spaceship's falling stars are a singly-linked list. On
  each spawn it `malloc`s a brand-new node (`createStar` →
  `malloc(sizeof(struct Node))`, `spaceShipGame/Core/Src/main.c:617`) and
  **prepends** it; nodes that scroll off are *recycled by repositioning*
  (`temp->y == 0 → temp->x = rand()%85`, line 647), not freed; and
  `freeLinkedList()` is called **only at game-over** (line 212). Net effect: the
  list **never shrinks during play** and the heap **grows monotonically** on a
  long session. (It's also never `srand()`-seeded, so every power-up is the same
  pattern.)
- **Snake's body needs the *same* shape — a growing ordered sequence — but done
  right.** Snake reuses the linked-list/sequence *pattern* with a **correct node
  lifecycle**: every tick either (a) **moves** — push one head node, pop one tail
  node → **net-zero allocation**, or (b) **grows** — push head only. There is no
  per-tick `malloc` leak and no "recycle but never shrink" growth: length tracks
  the game state exactly. The preferred concrete structure is a **fixed-capacity
  ring buffer of cells** (capacity = board size), which makes head-push/tail-pop
  O(1) with **zero heap churn at all** — the strongest version of the fix. The
  linked-list option remains available behind the same body interface; the
  decision and the trade-off live in
  [Snake Architecture](./SNAKE-ARCHITECTURE.md).
- **Why this is the reference fix.** Snake demonstrates the *right* way to do the
  exact data-structure Spaceship got wrong, in the same codebase, so the contrast
  is legible: same problem (an ordered growing collection on a tiny MCU), correct
  solution (bounded, churn-free lifecycle). When Spaceship is later refactored
  into a cartridge, its star list is brought onto the *same* bounded body/pool
  discipline — closing its leak as a side effect of adopting the shared model.

> **PoC framing.** None of the Spaceship issues are "the game is broken" — it
> runs. They are the **edge of the copy-paste, monolith-per-game model**, which
> is exactly the model the console replaces. Snake is the first artifact written
> on the *other* side of that line, and it's deliberately the smallest one, so
> the cost of proving the contract is low.

---

## 10. MVP vs. Full-Release

The single most important framing: **what the first Snake build must do** vs.
**what later versions add.** Snake has *no code today*, so "MVP" here means the
first shippable build, not current behavior.

### 10.1 MVP — the first Snake build (the reference cartridge)

| Capability | In MVP? |
|---|---|
| Grid movement on an 8 px grid (16×17 play area) | ✅ |
| Eat food → grow one cell | ✅ |
| 4-direction joystick steering (dominant-axis quantize) + 180°-reversal reject + one buffered turn | ✅ |
| Self + wall collision → game over | ✅ |
| Score = food eaten; on-screen SCORE + HI | ✅ |
| Bounded speed ramp (clamped minimum tick) | ✅ |
| **Seeded** RNG food spawn with on-snake rejection | ✅ |
| Built to the `init/update/render/teardown` cartridge contract, with a working **exit-to-shell** path | ✅ (the reference job) |
| **Churn-free body lifecycle** (ring buffer / proper linked-list) | ✅ (the heap-bug fix) |
| Flash high-score persistence | ❌ via the shared service — Full-Release (§8) |

**MVP non-goals (by design):** no levels, no obstacles/maze, no wrap-around mode,
no second player, no skins. These are deliberately out so the MVP stays the
*smallest thing that proves the contract and the body lifecycle.*

### 10.2 Full-Release — what later Snake versions add

| Addition | What it is | Why it's deferred |
|---|---|---|
| **Levels** | A sequence of stages with rising start-speed / target-length goals. | Depth on top of a proven loop; needs the MVP loop solid first. |
| **Obstacles / maze mode** | Static wall cells inside the play area to route around. | Reuses the wall-collision rule against arbitrary cells — cheap *after* the grid/collision core is proven. |
| **Wrap-around mode** | Edges teleport the head to the opposite side instead of killing it. | A toggle on the wall-collision rule (off in MVP for classic-difficulty). |
| **2-player** | Two snakes, two sticks (or shared-screen turns); last alive wins. | Genuinely new input + collision-between-snakes logic; squarely a stretch goal, and the RP2350's dual-core makes it natural later. |
| **Skins** | Alternate snake/food/board palettes (RGB565 swatch sets). | Pure cosmetic; trivial once render is data-driven. |

> Every Full-Release item is a *named* extension of an MVP system, not a rewrite
> — wrap-around toggles one rule, obstacles reuse one collision check, skins
> parameterize the palette. That is the test of a good MVP boundary: the
> additions clip on.

---

## 11. Success Metrics

| # | Metric | Target |
|---|---|---|
| SM1 | **Contract proven** | Snake runs entirely through `init/update/render/teardown` with **no** game-owned `main()`, `while(1)`, or direct peripheral access — verified by inspection. |
| SM2 | **Exit works** | Back-button → `teardown()` frees all game memory and returns to the shell menu, repeatably, with no leak across enter/exit cycles. |
| SM3 | **Flat heap** | Heap usage is **flat** across a long play session and across N enter/exit cycles (the explicit anti-regression vs. Spaceship's monotonic growth). |
| SM4 | **Determinism** | A fixed seed + scripted input sequence reproduces an identical board state — asserted by a regression test. |
| SM5 | **Fairness invariants** | Food never spawns on the snake; speed never exceeds the clamped floor; a 180° input never reverses the snake — each covered by a test. |
| SM6 | **Feel** | At max speed the queued-turn buffer registers a corner reliably (no dropped turns in a scripted fast-corner test). |
| SM7 | **Frame budget** | Renders within the console's ≥30 FPS budget (Snake's dirty-cell render touches only head + vacated tail per move — naturally tiny). |

---

## 12. Risks

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| R1 | **Body data-structure chosen wrong** (re-introduces churn or a fixed-cap overflow) | Low | High | Ring buffer sized to board capacity (overflow is impossible by construction); body interface hides the choice; SM3 heap test gates it. See [Snake Architecture](./SNAKE-ARCHITECTURE.md). |
| R2 | **Axis-mapping ambiguity bites Snake** (the X/Y contradiction between the two existing repos) | Med | Med | Snake consumes the *resolved* canonical mapping from the shared input service; the ambiguity is fixed once in the platform, not per game. |
| R3 | **Input feel poor at high speed** (turns dropped or reversed) | Med | Med | One-buffered-turn + 180°-reject, tested by SM6; tie the tick to scheduler `dt`, not a raw delay count. |
| R4 | **RNG seed is actually deterministic** (floating-ADC entropy weak on STM32) | Med | Low | Mix several entropy sources at boot; on RP2350 use the hardware entropy source; reproducibility-per-seed is a *feature*, only the boot seed must vary. |
| R5 | **Cartridge contract still in flux** when Snake is built | Med | Med | That's *the point* — Snake is the reference that *forces* the contract to be concrete; build it against the contract early so churn surfaces here, cheaply, not in the two refactors. |
| R6 | **Flash-wear from over-frequent high-score writes** | Low | Low | Write only on a *new* high score at game-over; the persistence service owns wear-leveling. |

---

## 13. Out of Scope

Explicitly **not** part of Snake (MVP or Full-Release):

- **Audio / sound effects** — no audio hardware in the console design.
- **Networked / online multiplayer** — local 2-player is the ceiling, and only in
  Full-Release.
- **Diagonal movement** — Snake is 4-way by definition; a dominant-axis quantize
  is the design, not a limitation to fix.
- **A general game SDK / user-authored levels** — the console is three named
  games, done well (see [Console PRD §12](./CONSOLE-PRD.md#12-out-of-scope)).
- **Reimplementing platform services** — Snake does **not** own a display driver,
  an input reader, a scheduler, an RNG, or a persistence store. Those are the
  shared platform's job; Snake only *consumes* them. Owning any of them would
  recreate the very monolith problem Snake exists to disprove.

---

## 14. What I'd Do Next (engineer's note)

If I were building Snake tomorrow, the order would be:

1. **Nail the body structure first** (ring buffer of cells, capacity = board
   size) behind a tiny `body` interface — because head-push/tail-pop *is* the
   game, and getting the churn-free lifecycle right here is the whole "reference
   fix" claim. Write the **flat-heap test (SM3)** before anything else so the
   anti-regression is locked in from line one.
2. **Implement the pure logic against the contract with a fake platform** —
   `update(input, dt)` over an in-memory grid, no hardware. With a **seeded RNG**
   and scripted input I can assert exact board states (SM4) in a host-side test
   *before touching the panel*. This is the determinism payoff doing real work.
3. **Wire it to the shared display + input services** and render the
   [mockup](./mockups/snake.html) for real — HUD strip, green snake, light-green
   head, red food, faint grid.
4. **Prove the exit path (SM2)** — enter Snake from the shell, play, back-button →
   `teardown()` → menu, in a loop, watching the heap stay flat. That single test
   demonstrates the *entire* cartridge contract working.
5. **Then layer Full-Release width** — wrap-around toggle, obstacles, skins —
   each clipping onto one existing system.

The throughline: **Snake is the smallest possible thing that proves the console's
biggest idea.** It validates the cartridge lifecycle, demonstrates the correct
fix for Spaceship's heap bug in the same data-structure family, and does it as a
clean greenfield build with no monolith to fight — which is exactly why it's the
*reference* cartridge and exactly why I'd build it first among the games.

---

*See also: [Console PRD](./CONSOLE-PRD.md) · [Snake Architecture](./SNAKE-ARCHITECTURE.md) · [Console Architecture](./CONSOLE-ARCHITECTURE.md) · [Snake mockup](./mockups/snake.html) · [Snake presentation](./presentations/snake.html)*

<div align="center">

# Pico Console

**A bare-metal handheld game console** — Snake, Spaceship, and Paint — running on a Raspberry Pi Pico 2 (RP2350), behind a menu shell navigated with a joystick and four buttons.

The display driver, input handling, and all three games are hand-written against the datasheet and the Pico SDK — no vendor display library, no game engine.

![Platform](https://img.shields.io/badge/platform-RP2350-blue)
![Board](https://img.shields.io/badge/board-Pico%202-blue)
![Language](https://img.shields.io/badge/language-C-orange)
![SDK](https://img.shields.io/badge/Pico%20SDK-2.3.0-lightgrey)

<img width="600" alt="Pico Console hardware" src="https://github.com/user-attachments/assets/475b43eb-73dd-4f93-8f8d-a98633743ad3" />

https://github.com/user-attachments/assets/f15f0d57-6506-4bb8-8c79-b11a607ec438

</div>

---

### Contents
[What it is](#what-it-is) · [Hardware](#hardware) · [Building](#building) · [Performance notes](#performance-notes)

---

## What it is

Power on, the menu shows three games, the joystick moves the selection, a button confirms. Each game owns the screen until you exit back to the menu. Under the hood: a single hand-written SPI display driver and input layer shared by the menu and all three games, so adding a fourth game means writing game logic, not another copy of the display driver.

| Game | What it is |
|---|---|
| **Snake** | Grid movement, food, self-collision, growing tail. Tail history is a ring buffer (see [Performance notes](#performance-notes)). |
| **Spaceship** | Vertical dodge — joystick moves the ship, obstacles fall, survive as long as you can. |
| **Paint** | Freehand drawing — joystick moves a cursor, hold to draw, cycle a color palette. |

---

## Hardware

| | |
|---|---|
| **MCU** | RP2350 (Pico 2 / Pico 2 W), dual Cortex-M33 |
| **Board** | `pico2_w` (set in `CMakeLists.txt` via `PICO_BOARD`) |
| **Display** | ST7789, RGB565, 240×240, driven over SPI1 |
| **Input** | 2-axis joystick (4 direction GPIOs + center press) + 4 push-buttons |
| **SPI** | `spi1`, requested clock 62.5 MHz, mode 0 (CPOL=0, CPHA=0), MSB-first |

<details>
<summary><b>Full pinout</b></summary>
<br>

| Signal | GPIO |
|---|---|
| LCD MOSI (DIN) | 11 |
| LCD SCK (CLK) | 10 |
| LCD CS | 9 |
| LCD DC | 8 |
| LCD RST | 12 |
| LCD Backlight | 13 |
| Button A | 15 |
| Button B | 17 |
| Button X | 19 |
| Button Y | 21 |
| Joystick Up | 2 |
| Joystick Down | 18 |
| Joystick Left | 16 |
| Joystick Right | 20 |
| Joystick Center | 3 |

<div align="center">
<img width="450" alt="Pinout diagram" src="https://github.com/user-attachments/assets/fb7354e2-8dc3-4bb2-af67-f721f983e081" />
</div>

</details>

---

## Building

Requires the [Raspberry Pi Pico VS Code extension](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico) (pulls in Pico SDK 2.3.0 and the ARM toolchain automatically), or a manual toolchain setup:

```bash
git clone https://github.com/meitalKrin/Pico-console
cd Pico-console
cmake -B build -S . -G Ninja
cmake --build build
```

Flash the resulting `build/pico_console.uf2` to the Pico 2 in BOOTSEL mode. Debug output (`printf`) is available over USB serial.

---

## Performance notes

Two specific inefficiencies were found and fixed, each measured, each its own commit ([#1](../../pull/1), [#2](../../pull/2)).

### `draw_char` — batched the display window instead of resetting it per pixel

Each `set_window` call costs a full column-address + row-address + RAM-write command sequence (~11 bytes across several SPI transactions) before a single pixel of color is sent. `draw_char` was calling it for **every pixel** of every glyph — drawing one character could cost dozens of window resets for a handful of pixels. Fixed by computing the glyph's bounding box once, setting the window a single time, and streaming every pixel of the glyph in one held chip-select session — the same pattern `fill_rect` already used correctly.

<div align="center">

| Full `"MAIN MENU"` redraw, scale 2 | Time |
|---|---|
| Before | **1066 µs** |
| After | **<!-- TODO(Mei): fill in --> µs** |

</div>

<details>
<summary>Raw serial monitor output</summary>
<br>

<img width="244" alt="before" src="https://github.com/user-attachments/assets/fd80d7a3-fd1d-4f54-93a6-af2ce05ca226" />
<img width="299" alt="after" src="https://github.com/user-attachments/assets/198954ea-84e8-4ba5-8c25-5c5d8b1e919c" />

</details>

### Snake — ring buffer instead of a per-frame array shift

Snake's tail history shifted a 200-element array by one every single frame, regardless of how long the snake actually was — O(200) writes per frame even for a 5-segment snake. Replaced with a ring buffer: a `head` index advances by one (wrapping modulo the buffer size) each frame, and every position read is translated relative to `head` — **O(1) per frame instead of O(200)**.

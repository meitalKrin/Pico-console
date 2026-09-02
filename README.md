<div align="center">

# 🎮 Pico Console

**A bare-metal handheld game console running Snake, Spaceship, and Paint on a Raspberry Pi Pico 2.**

*Driven entirely by a hand-written SPI display driver and input layer—no vendor display libraries, no game engines.*

</div>

---

<p align="center">
  <img width="48%" alt="Hardware View" src="https://github.com/user-attachments/assets/475b43eb-73dd-4f93-8f8d-a98633743ad3" />
  <img width="48%" alt="Schematics/Wiring" src="https://github.com/user-attachments/assets/fb7354e2-8dc3-4bb2-af67-f721f983e081" />
</p>

---

## ⚡ What It Is

Power on, the menu shows three games, the joystick moves the selection, a button confirms. Each game owns the screen until you exit back to the menu. 

**Under the hood:** A single hand-written SPI display driver and input layer shared by the menu and all three games, meaning adding a fourth game means writing game logic, not another copy of the display driver.

| Game | What it is |
| :--- | :--- |
| **Snake** | Grid movement, food, self-collision, growing tail (optimized via a ring buffer). |
| **Spaceship** | Vertical dodge — joystick moves the ship, obstacles fall, survive as long as you can. |
| **Paint** | Freehand drawing — joystick moves a cursor, hold to draw, cycle a color palette. |

---

## 🛠️ Hardware Specs

| Component | Specification |
| :--- | :--- |
| **MCU** | RP2350 (Pico 2 / Pico 2 W), dual Cortex-M33 |
| **Board** | `pico2_w` (set in `CMakeLists.txt` via `PICO_BOARD`) |
| **Display** | ST7789, RGB565, 240×240, driven over SPI1 |
| **Input** | 2-axis joystick (4 direction GPIOs + center press) + 4 push-buttons |
| **SPI** | `spi1`, requested clock 62.5 MHz, mode 0 (`CPOL=0`, `CPHA=0`), MSB-first |

---

## 🔌 Pinout Configuration

| Signal | GPIO |
| :--- | :--- |
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

---

## 🚀 Building & Flashing

Requires the Raspberry Pi Pico VS Code extension (pulls in Pico SDK 2.3.0 and the ARM toolchain automatically), or a manual toolchain setup:

```bash
git clone [https://github.com/meitalKrin/Pico-console](https://github.com/meitalKrin/Pico-console)
cd Pico-console
cmake -B build -S . -G Ninja
cmake --build build

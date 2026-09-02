<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Pico Console - Bare-Metal Handheld Game Console</title>
    <style>
        :root {
            --bg-color: #0d1117;
            --card-bg: #161b22;
            --border-color: #30363d;
            --text-main: #c9d1d9;
            --text-muted: #8b949e;
            --accent: #58a6ff;
            --accent-glow: rgba(88, 166, 255, 0.15);
            --code-bg: #1f6feb33;
        }

        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            background-color: var(--bg-color);
            color: var(--text-main);
            line-height: 1.6;
            margin: 0;
            padding: 2rem;
        }

        .container {
            max-width: 900px;
            margin: 0 auto;
        }

        header {
            text-align: center;
            padding-bottom: 2rem;
            border-bottom: 1px solid var(--border-color);
            margin-bottom: 2rem;
        }

        h1 {
            font-size: 2.5rem;
            color: #ffffff;
            margin-bottom: 0.5rem;
        }

        h1 span {
            color: var(--accent);
        }

        .tagline {
            font-size: 1.1rem;
            color: var(--text-muted);
        }

        .media-grid {
            display: grid;
            grid-template-columns: 1fr;
            gap: 1.5rem;
            margin: 2rem 0;
            text-align: center;
        }

        @media (min-width: 768px) {
            .media-grid {
                grid-template-columns: 1fr 1fr;
            }
        }

        img {
            max-width: 100%;
            height: auto;
            border-radius: 8px;
            border: 1px solid var(--border-color);
            box-shadow: 0 4px 12px rgba(0,0,0,0.5);
        }

        section {
            background-color: var(--card-bg);
            border: 1px solid var(--border-color);
            border-radius: 8px;
            padding: 1.5rem 2rem;
            margin-bottom: 1.5rem;
        }

        h2 {
            font-size: 1.4rem;
            color: #ffffff;
            border-bottom: 1px solid var(--border-color);
            padding-bottom: 0.5rem;
            margin-top: 0;
        }

        p {
            margin: 0.8rem 0;
        }

        table {
            width: 100%;
            border-collapse: collapse;
            margin: 1rem 0;
        }

        th, td {
            text-align: left;
            padding: 0.75rem;
            border-bottom: 1px solid var(--border-color);
        }

        th {
            color: #ffffff;
            background-color: rgba(255, 255, 255, 0.03);
        }

        code {
            font-family: ui-monospace, SFMono-Regular, SF Mono, Menlo, Consolas, Liberation Mono, monospace;
            background-color: rgba(110, 118, 129, 0.4);
            padding: 0.2rem 0.4rem;
            border-radius: 6px;
            font-size: 85%;
        }

        pre {
            background-color: #010409;
            border: 1px solid var(--border-color);
            border-radius: 6px;
            padding: 1rem;
            overflow-x: auto;
        }

        pre code {
            background-color: transparent;
            padding: 0;
        }

        .badge {
            display: inline-block;
            padding: 0.25rem 0.6rem;
            font-size: 85%;
            font-weight: 600;
            line-height: 1;
            color: #1f6feb;
            background-color: var(--code-bg);
            border-radius: 20px;
            margin-bottom: 1rem;
        }
    </style>
</head>
<body>

<div class="container">
    
    <header>
        <span class="badge">Raspberry Pi Pico 2 / RP2350</span>
        <h1>🎮 Pico <span>Console</span></h1>
        <p class="tagline">A bare-metal handheld game console running Snake, Spaceship, and Paint.</p>
    </header>

    <div class="media-grid">
        <div>
            <img width="1491" height="780" alt="Pico Console Hardware View" src="https://github.com/user-attachments/assets/475b43eb-73dd-4f93-8f8d-a98633743ad3" />
        </div>
        <div>
            <img width="1719" height="1866" alt="Schematics/Wiring" src="https://github.com/user-attachments/assets/fb7354e2-8dc3-4bb2-af67-f721f983e081" />
        </div>
    </div>

    <section>
        <h2>⚡ What It Is</h2>
        <p>Power on, the menu shows three games, the joystick moves the selection, a button confirms. Each game owns the screen until you exit back to the menu.</p>
        <p><strong>Under the hood:</strong> A single hand-written SPI display driver and input layer shared by the menu and all three games, so adding a fourth game means writing game logic, not another copy of the display driver.</p>
        
        <table>
            <thead>
                <tr>
                    <th>Game</th>
                    <th>What it is</th>
                </tr>
            </thead>
            <tbody>
                <tr>
                    <td><strong>Snake</strong></td>
                    <td>Grid movement, food, self-collision, growing tail. Tail history utilizes an optimized ring buffer.</td>
                </tr>
                <tr>
                    <td><strong>Spaceship</strong></td>
                    <td>Vertical dodge — joystick moves the ship, obstacles fall, survive as long as you can.</td>
                </tr>
                <tr>
                    <td><strong>Paint</strong></td>
                    <td>Freehand drawing — joystick moves a cursor, hold to draw, cycle a color palette.</td>
                </tr>
            </tbody>
        </table>
    </section>

    <section>
        <h2>🛠️ Hardware Specs</h2>
        <table>
            <thead>
                <tr>
                    <th>Component</th>
                    <th>Specification</th>
                </tr>
            </thead>
            <tbody>
                <tr>
                    <td><strong>MCU</strong></td>
                    <td>RP2350 (Pico 2 / Pico 2 W), dual Cortex-M33</td>
                </tr>
                <tr>
                    <td><strong>Board</strong></td>
                    <td><code>pico2_w</code> (set in CMakeLists.txt via PICO_BOARD)</td>
                </tr>
                <tr>
                    <td><strong>Display</strong></td>
                    <td>ST7789, RGB565, 240×240, driven over SPI1</td>
                </tr>
                <tr>
                    <td><strong>Input</strong></td>
                    <td>2-axis joystick (4 direction GPIOs + center press) + 4 push-buttons</td>
                </tr>
                <tr>
                    <td><strong>SPI</strong></td>
                    <td>spi1, requested clock 62.5 MHz, mode 0 (CPOL=0, CPHA=0), MSB-first</td>
                </tr>
            </tbody>
        </table>
    </section>

    <section>
        <h2>🔌 Pinout Configuration</h2>
        <table>
            <thead>
                <tr>
                    <th>Signal</th>
                    <th>GPIO</th>
                </tr>
            </thead>
            <tbody>
                <tr><td>LCD MOSI (DIN)</td><td>11</td></tr>
                <tr><td>LCD SCK (CLK)</td><td>10</td></tr>
                <tr><td>LCD CS</td><td>9</td></tr>
                <tr><td>LCD DC</td><td>8</td></tr>
                <tr><td>LCD RST</td><td>12</td></tr>
                <tr><td>LCD Backlight</td><td>13</td></tr>
                <tr><td>Button A</td><td>15</td></tr>
                <tr><td>Button B</td><td>17</td></tr>
                <tr><td>Button X</td><td>19</td></tr>
                <tr><td>Button Y</td><td>21</td></tr>
                <tr><td>Joystick Up</td><td>2</td></tr>
                <tr><td>Joystick Down</td><td>18</td></tr>
                <tr><td>Joystick Left</td><td>16</td></tr>
                <tr><td>Joystick Right</td><td>20</td></tr>
                <tr><td>Joystick Center</td><td>3</td></tr>
            </tbody>
        </table>
    </section>

    <section>
        <h2>🚀 Building & Flashing</h2>
        <p>Requires the Raspberry Pi Pico VS Code extension (pulls in Pico SDK 2.3.0 and the ARM toolchain automatically), or a manual toolchain setup:</p>
        <pre><code>git clone https://github.com/meitalKrin/Pico-console
cd Pico-console
cmake -B build -S . -G Ninja
cmake --build build</code></pre>
        <p>Flash the resulting <code>build/pico_console.uf2</code> to the Pico 2 in <strong>BOOTSEL mode</strong>. Debug output (<code>printf</code>) is available over USB serial.</p>
    </section>

    <section>
        <h2>⚡ Performance Notes</h2>
        <p>Two specific inefficiencies were found and fixed, each measured and documented in individual commits (#1, #2):</p>
        
        <ul>
            <li>
                <strong>Display Driver Optimization:</strong> <code>draw_char</code> was resetting the display's addressing window on every single pixel. Each <code>set_window</code> call costs a full column-address + row-address + RAM-write command sequence (~11 bytes across several SPI transactions) before a single pixel of color is sent. 
                <br><em>Fix:</em> Computed the character's bounding box once, set the window a single time, and streamed every pixel of the glyph in one held chip-select session (matching <code>fill_rect</code>'s behavior).
                <div style="margin: 1rem 0; display: flex; gap: 1rem; align-items: center; justify-content: center;">
                    <div>Before: <br><img width="244" height="148" alt="Before optimization" src="https://github.com/user-attachments/assets/fd80d7a3-fd1d-4f54-93a6-af2ce05ca226" /></div>
                    <div>After: <br><img width="299" height="121" alt="After optimization" src="https://github.com/user-attachments/assets/198954ea-84e8-4ba5-8c25-5c5d8b1e919c" /></div>
                </div>
            </li>
            <li>
                <strong>Snake Tail Ring Buffer:</strong> Snake's tail history shifted a 200-element array by one every single frame regardless of the snake's actual length—O(200) writes per frame even for a 5-segment snake. 
                <br><em>Fix:</em> Replaced with a ring buffer. A head index advances by one (wrapping modulo buffer size) each frame, and position reads translate relative to head—achieving <strong>O(1)</strong> complexity per frame.
            </li>
        </ul>
    </section>

</div>

</body>
</html>

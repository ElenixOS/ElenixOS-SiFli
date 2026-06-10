# ElenixOS-SiFli

Board support package for running [ElenixOS](https://github.com/ElenixOS/ElenixOS) on SiFli chipsets. Integrates the ElenixOS smartwatch OS with RT-Thread via the SiFli SDK, adding hardware drivers and platform initialization for a round/square LCD smartwatch.

## Hardware

| Component | Detail |
|-----------|--------|
| Chip | SiFli SF32LB58 |
| Display | TFT CO5300, 390x450, round/square, DSI interface |
| Input | Crown encoder (GPIO 28/30/150) + side button (GPIO 152) |
| Memory | PSRAM image cache, SRAM metadata cache |
| Storage | SD/Flash via RT-Thread DFS + FAT |

## Prerequisites

Follow the [SiFli Getting Started Guide](https://docs.sifli.com/projects/sdk/latest/en/sf32lb52x/quickstart/get-started.html) to install the SDK, toolchain, and SCons.

The recommended way is installing the **SiFli-SDK-CodeKit** VSCode extension from the [marketplace](https://marketplace.visualstudio.com/items?itemName=SiFli.SiFli-SDK-CodeKit).

Hardware documentation: [https://wiki.sifli.com/en/](https://wiki.sifli.com/en/)

## Building & Flashing (VSCode Plugin — Recommended)

Install [SiFli-SDK-CodeKit](https://marketplace.visualstudio.com/items?itemName=SiFli.SiFli-SDK-CodeKit) from the VSCode marketplace. The plugin provides a GUI for the full workflow: SDK management, environment setup, project configuration, building, and UART flashing.

## Manual Build & Flash

### Build

```bash
git submodule update --init --recursive
cd project
scons
```

Output: `build_*/main.bin` / `main.elf`

### Flash

```bash
cd project/build_sf32lb58-lcd_a128r32n1_a1_dsi_hcpu
./uart_download.sh        # macOS / Linux
uart_download.bat         # Windows
```

For alternative flashing methods see the [SiFli Impeller Tool Guide](https://wiki.sifli.com/en/tools/%E7%83%A7%E5%BD%95%E5%B7%A5%E5%85%B7.html).

## Project Structure

```
├── project/          SCons build entry + Kconfig
├── src/              Board port: main(), device drivers, RT-Thread adaptation
│   ├── main.c        Entry point (BUBBLE_DEMO or ElenixOS boot)
│   ├── bubble_demo.c LVGL bubble_grid demo
│   ├── devices/      Display, battery, power, RTC, vibrator, sensor
│   └── port/         RT-Thread critical section port
├── third_party/
│   ├── SConscript    ElenixOS + JerryScript + LVGL bridge build
│   └── ElenixOS/     Smartwatch OS submodule
├── tools/            YMODEM upload utility
└── resources/        Pre-compiled icon binaries
```

## Bubble Demo

A standalone LVGL bubble-grid demo that skips ElenixOS initialization. 20 colorful animated bubbles demonstrate the `eos_bubble_grid` physics (inertia, spring, snap).

Disabled by default. Enable by adding `-DBUBBLE_DEMO` to the build defines or uncomment `#define BUBBLE_DEMO` in `src/main.c`.

## Dependencies

- **RT-Thread** — RTOS (via SiFli SDK)
- **LVGL v9** — GUI framework (via SiFli SDK)
- **ElenixOS** — Smartwatch UI framework (submodule)
- **JerryScript** — JavaScript engine (embedded in ElenixOS)
- **cJSON** — JSON parsing (via SiFli SDK)
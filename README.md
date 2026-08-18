# platform-nes
***

`platform-nes` is a platform-agnostic library for creating games that run on both the Famicom and Nintendo
Entertainment System as well as other targets without CPU emulation. This is achieved by emulating parts of the PPU
and abstracting interrupt technology while compiling all code natively to a target platform. This is made convenient 
to the user with a variety of functions and macros to ensure compatability with [llvm-mos](https://llvm-mos.org/) and 
modern hardware using [SDL3](https://wiki.libsdl.org/SDL3/FrontPage). In its current position, `platform-nes` is feature
having offered audio, video, interrupt and other such translation layer technologies required to natively compile a game
for both the 6502 and modern CPU targets. However, it is far from being completely finished with its long term goals 
which are listed further down. 

`platform-nes` may require the user to include additional technologies for DRM and content obfuscation, the entire
project is designed in c++23 with LTO oriented design with a demo ready to demonstrate some 
of the existing functions all of which are documented at the [docsite](https://platform-nes.readthedocs.io/en/latest/).
Which will demonstrate how you can easily begin using this project.

> At this point in time, `platform-nes` recognises the following as dependancies:
> 
> **NES**:
>   1. [Famistudio](https://github.com/BleuBleu/FamiStudio) Engine (ca65)
>   
> **SDL3**:
>   1. SDL3
>      1. libusb (linux only)
> 
> **3ds/dsi/ds/wiiu/3ds/switch**
>   1. devKitPro 


## Current Targets:
- NES/Famicom
- GameCube
- Wii
- 3ds
- Wii U
- Switch
- Windows
- Mac
- Linux
- Web Assembly

## Supported Cartridges
- NROM
- VRC1


## Planned Updates:

### 8x16 Sprites

At this point in time, only `8x8` sprites are implemented and non-NES targets do not contain an `8x16` emulation.

## Other Controller Types

At this point the only supported controllers are NTSC NES Controllers.

### SaveFiles

It's eventually planned to support cartridges that have the ability to save memory to either batter backed SRAM, EEPROM
or in the case of more modern targets save files with paths managed by the developer.

### Character ROM Bank Switching

Currently, the pattern tables are not modified post-reset where they are fetched from `rodata`, in the future 
Character ROM Bank switching technology will be supported.

## Minimum Spec (Windows/Mac/Linux/Web)

On the SDL3 targets, the per-tick cost is the shared game logic plus the software PPU compositor
(`src/emu/ppu.cpp`) writing a real frame every 60Hz, independent of display refresh. That workload needs an
out-of-order CPU — in-order cores (e.g. Atom Bonnell, ARM11) stall heavily on its per-pixel branches even when
their nameplate clock/MIPS looks sufficient.

- **With a GPU** (hardware textured-quad blit, e.g. SDL3's accelerated renderer): an out-of-order CPU roughly on
  the order of a Pentium II 233 / K6-2 300 (1998) or a Cortex-A53 (2013+) is enough; GPU requirements are
  negligible (any hardware blit path since the late '90s clears it).
- **Without a GPU** (software-rendered/fbdev path): the scale-and-present step also runs on CPU, raising the
  floor to roughly a Pentium II 300-350 / K6-2 350+ or a Cortex-A7 quad (Raspberry Pi 2, 2015)-class core.

These are order-of-magnitude estimates, not measured benchmarks of this codebase.
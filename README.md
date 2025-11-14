# XTCE-Blue

XTCE-Blue is a fork of [reenigne's XTCE 8088 emulator](https://github.com/reenigne/reenigne/tree/master/8088/xtce) providing video emulation, an SDL3 interface and an ImGui-based debugger.

## About XTCE

XTCE is a cycle-interruptable, microcode-based 8088 emulation core. It was developed by the brilliant demo-coder reenigne, 
one of the programmers behind such amazing demos as [8088 MPH](https://www.youtube.com/watch?v=yHXx3orN35Y) and [Area 5150](https://www.youtube.com/watch?v=fWDxdoRTZPc). 
He designed it to simulate the operation of his IBM 5160 in a cycle-exact manner, down to each DRAM refresh cycle.

As an author of another cycle-accurate PC emulator, [MartyPC](https://github.com/dbalsom/martypc), I have admired the audacity to execute the 8088's microcode directly, especially since reenigne was the [first person to decode it](https://www.reenigne.org/blog/8086-microcode-disassembled/). 

Unfortunately, the massive technical accomplishment that XTCE represents has been somewhat overlooked considering that XTCE lacks such amenities as a display, floppy disk controller, or even a keyboard. This limits its reach to the most hardcore of retro programmers looking for a powerful research tool.

With reenigne's blessing I aimed to take XTCE and make a fully fleshed out demonstration emulator out of it - and so XTCE-Blue was born.

XTCE-Blue integrates several device implementations ported from [MartyPC](https://github.com/dbalsom/martypc), including its precise, overscan-aware CGA emulation.

![screenshot_01](/images/screenshot_01.png)

## Goals
 - Create a clean, well-commented C++ reference emulator for cycle-accurate emulation of the IBM PC/XT.
 - Demonstrate a modern debugging GUI with ImGui for a PC emulator
 - Run 8088 MPH and Area 5150!

## Non-Goals

 - Be a long-term project
   - I don't intend to give up development of my flagship emulator, [MartyPC](https://github.com/dbalsom/martypc). 
     MartyPC has over three years of blood sweat and tears poured into it, and I will continue to improve it into the future.
   - Once XTCE-Blue has proven accurate enough to run the ultimate gauntlet of Area 5150, development on it will likely stop. 
     - You are encouraged to submit PRs to improve the emulator's accuracy, but issues lacking a PR will unlikely be addressed.
     - You are strongly encouraged to fork the entire project to make the emulator of your dreams!

## Debugger Features

 - CPU status display with registers, flags, prefetch queue contents, microcode state and more
   - Stepping by CPU cycle or by instruction boundaries (a bit glitchy still).
   - Single code execution breakpoint (CS:IP)
 - Memory viewer
 - VRAM viewer
 - Stack display
 - Instruction disassembly display
 - Video card Status display

## Hardware Implementation Status

Currently, XTCE-Blue has emulation of the following devices:

 - Intel 8088 CPU
 - Intel 8253 Programmable Interrupt Timer
 - Intel 8259 Programmable Interrupt Controller
 - Intel 8255 Programmable Peripheral Interface
 - Intel 8237 Programmable DMA Controller
 - IBM Color Graphics Adapter
 - IBM Floppy Disk Controller (NEC u765) 
 - IBM Model F Keyboard

What it is currently lacking:

 - Serial Port(s)
 - Parallel Ports(s)
 - Game Port

# Thanks to

 - Ken Shirriff for his invaluable analysis of the 8088
 - Omar Cornut for the excellent Dear ImGui library
 - Blargg for the Blip_Buffer waveform synthesis library
# LittleBlue

An SDL3 interface to [reenigne's](https://reenigne.org) XTCE 8088 emulator.

## XTCE

XTCE is a cycle-interruptable, microcode-based 8088 emulation core. It was developed by the brilliant demo-coder reenigne, 
one of the programmers behind such amazing demos as [8088 MPH](https://www.youtube.com/watch?v=yHXx3orN35Y) and [Area 5150](https://www.youtube.com/watch?v=fWDxdoRTZPc).

Unfortunately, the massive technical accomplishment that XTCE represents has been somewhat overlooked considering that XTCE lacks such amenities as a display, keyboard, floppy disk controller, etc.

reenigne simply hasn't had the time to build XTCE out into a full-featured emulator.

I wanted to change that.

LittleBlue is the fusion of device implementations from MartyPC and reenigne's XTCE CPU core, adding a front-end utilizing SDL3 for rendering and Dear Imgui for a debugging interface.

![screenshot_01](/images/screenshot_01.png)

## Debugger Features

 - CPU Status with registers, instruction queue contents
 - Step CPU by cycle or instruction
 - Single code breakpoint (CS:IP)
 - Memory Viewer
 - VRAM Viewer
 - Stack display
 - Instruction Disassembly
 - CRTC register viewer

## Hardware Implementation Status

Currently, LittleBlue has emulation of the following:

 - 8088 CPU
 - 8253 PIT
 - 8259 PIC
 - 8255 PPI
 - 8237 DMA Controller
 - IBM CGA video card (in text mode)

What it is currently lacking:

 - Keyboard
 - Floppy Disk Controller

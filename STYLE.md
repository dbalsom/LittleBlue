# Project style guide

## Purpose

This file documents the naming and small-formatting conventions the project should follow. 
It is intentionally concise, consistency is enforced with tooling (.clang-tidy / clang-format).

## Tabs
- 4 spaces

## Naming Conventions
- Types (classes, structs, enum class): PascalCase (UpperCamelCase)
- Free functions and methods: lowerCamelCase (camelBack)
- Private data members: lower_snake_case_ (trailing underscore required)
- Public data members (avoid): lower_snake_case (no trailing underscore) or PascalCase only for constants
- Local variables and parameters: lower_snake_case
- Constants/macros: UPPER_SNAKE_CASE
- Enum values (enum class): PascalCase
- Boolean accessors: isXxx / hasXxx style (lowerCamelCase)

## Examples

### Types:
- Good: `class Cga;`, `struct DmacStatus;`
- Bad: `class cga;`, `class CGAClass;`

### Methods / functions:
- Good: `void reset();`, `uint8_t* getBackBuffer();`, `bool isRunning() const;`
- Bad: `void Reset();`, `uint8_t* GetBackBuffer();`

### Private members:
- Good: `uint8_t cur_fg_;`, `bool mode_hires_gfx_;`
- Bad: `uint8_t curFg_;`, `_curFg`, `curFg`

### Constants/macros:
- Good: `#define VRAM_SIZE 0x4000`, `static constexpr uint32_t CGA_XRES_MAX = ...;`
- Bad: `static constexpr uint32_t cgaXresMax = ...;`

### Enum classes:
- Good: `enum class MachineState { Running, Stopped, BreakpointHit };`
- Bad: `enum MachineState { RUNNING, STOPPED };`

### Booleans:
- Member field: `bool irq_pending_;`
- Accessor: `bool isIrqPending() const;` or `bool irqPending() const;` (prefer isX/hasX for clarity)



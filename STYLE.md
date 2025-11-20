# Project style guide

## Purpose

This file documents the naming and small-formatting conventions the project should follow. 
It is intentionally concise, consistency is enforced with tooling (.clang-tidy / clang-format).

## C++ Standard Version
 
The CPU core should stick to C++11 compatible features to make it easier to extract for use in other emulators.
The rest of the emulator is free to use all modern C++ features, including std::format.

## Tabs

4 spaces

## Brace Style

K&R Brace style. Always use braces for if and for loops - no dangling statements.

- Good: 
```cpp
    if (foo) {
        bar();
    }
```

- Bad:
```cpp
    if (foo)
        bar();
```

## Naming Conventions
- Types (classes, structs, enum class): PascalCase (UpperCamelCase)
- Free functions and methods: lowerCamelCase (camelBack)
- Private data members: lower_snake_case_ (trailing underscore required)
- Public data members (avoid): lower_snake_case (no trailing underscore)
- Struct fields: lower_snake_case (no trailing underscore)
- Local variables and parameters: lower_snake_case
- Macros/Defines: UPPER_SNAKE_CASE
- Constants: kPascalCase
- Enum values (enum class): PascalCase, unless an existing acronym (VGA) in which case UPPERCASE is acceptable
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



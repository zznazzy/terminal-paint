# Terminal Paint

A simple ASCII art painting application that runs in your terminal. Built with C and ncurses.

## Features

- Paint with different characters (`#`, `*`, `@`, `%`, `+`, `o`, `x`, `.`, `~`, `&`)
- 8 colors (Black, Red, Green, Yellow, Blue, Magenta, Cyan, White)
- Pen mode for continuous drawing
- Save/load your artwork

## Build & Run

**Linux/Unix:**
```bash
gcc -O2 -Wall terminal_paint.c -o terminal_paint -lncurses
./terminal_paint
```
**Windows (MSYS2):**
```bash
gcc terminal_paint.c -o terminal_paint -I/ucrt64/include/ncursesw -L/ucrt64/lib -lncursesw
./terminal_paint.exe
```
**Windows (MinGW):**
```bash
gcc -O2 -Wall terminal_paint.c -o terminal_paint.exe -lpdcurses
terminal_paint.exe
```

## Controls

- **Arrow Keys / WASD / HJKL** - Move cursor
- **Space** - Paint at cursor
- **Enter** - Toggle pen mode (paint while moving)
- **B** - Change brush character
- **C** - Cycle colors
- **0-7** - Pick color directly
- **E** - Eraser mode
- **X** - Clear canvas
- **S** - Save to `paint_save.txt`
- **L** - Load from `paint_save.txt`
- **Q** - Quit

## Requirements

- ncurses library (Linux/macOS) or PDCurses (Windows)
- Terminal with at least 20x10 characters

## File Format

Saves as simple text format with canvas dimensions and character/color data.

---

*A portfolio project demonstrating C programming.*
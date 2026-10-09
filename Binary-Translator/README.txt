# Binary Translator

A modern, professional Windows desktop application that converts **text ⇄ binary** and **numbers ⇄ binary** on the fly. Built from scratch in pure C++ using the native Win32 API — no frameworks, no external dependencies, just one clean executable with a custom app icon.

Made by **Matrix Core Studios**.

---

## Features

- **5 modes in one window** — switch instantly with clickable tabs:
  - Text → Binary
  - Binary → Text
  - Number → Binary
  - Binary → Number
  - Help / Guide
- **Live conversion** — the output updates while you type, no button needed
- **Left = Input, Right = Output** split workspace
- **Modern dark UI** with smooth animations:
  - sliding tab indicator
  - hover effects and focus glow
  - output pulse on every conversion
  - "COPIED!" flash animation
- **One-click Copy** — sends the full output to your clipboard
- **Mouse-wheel scrolling** inside the panels
- **Keyboard shortcuts** — `Ctrl+1` … `Ctrl+5` to switch between modes
- **DPI aware** — scales cleanly on high-resolution displays
- **Loop friendly** — the app stays open, convert and switch modes as many times as you like

## Conversion Details

- Standard **8 bits per byte**, encoded one character at a time
- Example: `A` → `65` → `01000001`
- Negative numbers use **64-bit two's complement**
- Every number result is also shown in **hex** and **octal**
- Binary input accepts spaces: `01001000 01101001` → `Hi`
- Binary input up to **64 bits** supported

## Project Structure

```
Binary-Translator/
├── BinaryTranslator.exe    ready-to-run app (with custom icon)
├── README.md
└── src/
    ├── main.cpp            the full source (single file)
    ├── app.rc              icon resource script
    ├── app.ico             application icon
    ├── build.bat           compiles src -> ../BinaryTranslator.exe
    └── run.bat             builds if needed, then launches the app
```

## Build

You only need a C++ compiler with the Win32 SDK (MinGW/GCC or MSVC). The icon is compiled with `windres`, which ships with MinGW.

From inside the `src` folder:

```
windres app.rc -O coff -o app.res
g++ -std=c++14 -O2 -static -mwindows -o ../BinaryTranslator.exe main.cpp app.res
```

Or simply run `src\build.bat` — it also works without `windres` (builds without the icon in that case).

## Run

Double-click `BinaryTranslator.exe`, or use `src\run.bat` — it builds the executable automatically if it is missing.

## Tech

- Pure **C++14**, single source file (`main.cpp`)
- Native **Win32 API** GUI with **GDI** double-buffered rendering
- RichEdit 50W control for styled output text
- Zero third-party libraries — everything ships with the Windows SDK
- Runs on Windows 7 and newer (tested on Windows 10)

## Author

**Matrix Core Studios**

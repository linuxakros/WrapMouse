# WrapMouse

WrapMouse is a lightweight Windows utility that wraps the mouse cursor around the edges of the screen.

It supports multi-monitor setups, optional visual effects, keyboard shortcuts, and configuration through a UTF-8 INI file.

## Features

- Wrap the mouse cursor around the edges of the screen.
- Support for multiple monitors with different layouts.
- Distinguish between natural transitions from one monitor to another and actual screen-edge wrapping.
- Optional Portal visual effect when the cursor wraps.
- Optional crosshair overlay centered on the cursor.
- Optional cursor-centering shortcut.
- Start with Windows option.
- English and French interface.
- Configuration stored in `WrapMouse.ini`.
- Single-instance protection.

## Requirements

- Windows
- Visual Studio C++ Build Tools or Visual Studio with MSVC
- C++17-compatible MSVC compiler

The project is currently built and tested as a native x64 Windows application.

## Download

Precompiled versions of WrapMouse are available in the GitHub **Releases** section.

The repository contains the source code only.

## Building

Open an **x64 Native Tools Command Prompt for Visual Studio**, or initialize the Visual Studio developer environment first.

Compile with:

```bat
cl /O2 /EHsc /std:c++17 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DUNICODE /D_UNICODE "WrapMouse.cpp" /Fe:WrapMouse.exe
```

The resulting executable is:

```text
WrapMouse.exe
```

## Configuration

On first launch, WrapMouse creates:

```text
WrapMouse.ini
```

next to the executable.

The configuration file contains sections for:

- language selection;
- mouse wrapping;
- cursor centering;
- crosshair;
- Portal effect.

Colors are specified as hexadecimal RGB values.

Example:

```ini
[Wrap]
Enabled=0

[Crosshair]
Enabled=0
HorizontalLength=-1
VerticalLength=-1
Color=00FF00

[Portal]
Enabled=0
MinLength=40
MaxLength=120
MinThickness=4
MaxThickness=6
WrapDepartureColor=1EAAFF
WrapArrivalColor=FF9619
DirectDepartureColor=00E5A0
DirectArrivalColor=FFD500
```

### Crosshair

The crosshair can be enabled from the tray menu.

Its keyboard shortcut is:

```text
△ + ▽
```

The shortcut works in either order, provided the two key presses occur within the configured time interval.

### Cursor centering

The cursor-centering feature can be enabled from the tray menu.

Its keyboard shortcut is:

```text
◁ + ▷
```

The shortcut also works in either order.

## System tray

WrapMouse runs in the Windows system tray.

Right-click the tray icon to access:

- **Wrap**
  - Enable
  - Portal effect
- **Shortcut**
  - Center cursor
  - Crosshair
- **Options**
  - Start with Windows
  - Language
- **Exit**

The Portal effect option is available only when mouse wrapping is enabled.

## Multi-monitor behavior

WrapMouse uses the Windows virtual desktop and monitor layout.

When the cursor reaches an outer edge that is not connected to another monitor, it is wrapped to another suitable monitor.

When the cursor naturally crosses from one monitor to another, WrapMouse does not treat this as a wrap. The Portal effect can nevertheless display a different visual effect for this transition.

## Project structure

The project is intentionally small and self-contained:

```text
WrapMouse.cpp
README.md
```

The main application is implemented in a single C++ source file.

The `WrapMouse.ini` configuration file is generated automatically on first launch and is not included in the repository.

The compiled executable is distributed through the GitHub **Releases** section.

## UIAccess

WrapMouse can be configured with Windows `uiAccess` so that its cursor wrapping can continue to operate when interacting with elevated applications.

For `uiAccess` to work, Windows requires the executable to be digitally signed and installed in a trusted secure location.

This is an optional deployment requirement and is not needed simply to compile the project.

## License

CC0 1.0 Universal. See [LICENSE](LICENSE) for details.
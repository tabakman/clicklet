<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="res/logo-dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="res/logo.svg">
    <img alt="Clicklet" src="res/logo.svg" width="360">
  </picture>
</p>

**Map your extra mouse buttons. That’s it.**

Clicklet is a tiny Windows tray app for mapping extra mouse buttons (4 and 5) to keyboard shortcuts. It runs as a single native `.exe` with an optional config file for up to two mappings. Free and open source.

## How it works

Run `Clicklet.exe`. It sits in the tray.

By default, Button 5 sends Ctrl+Enter.

Want something else? Add `Clicklet.ini` next to the executable:

```ini
[Mapping]
Button4=ctrl+c
Button5=ctrl+v
```

That’s the whole config.

Leave a button out and it stays normal. No config file means Button 5 defaults to Ctrl+Enter.

Invalid settings are reported at startup.

Save `Clicklet.ini` without a byte-order mark (BOM).

## Use it for

- **Claude Code & AI tools** — put frequently used shortcuts on your mouse
- **Gmail and Outlook** — send with Ctrl+Enter
- **JupyterLab** — run the selected cell with Ctrl+Enter
- **Copy and paste** — Ctrl+C and Ctrl+V
- **Paste plain text** — Ctrl+Shift+V in apps that support it
- **Close a tab** — Ctrl+W
- **Screenshot** — Win+Shift+S

## Supported shortcuts

Each button can send one supported key, optionally combined with Ctrl, Alt, Shift, Win, or any combination of them.

Supported keys: letters, digits, F1–F24, Enter, Tab, Esc, Space, Backspace, Delete, Insert, Home, End, Page Up, Page Down, and arrow keys.

Modifiers are optional, so a bare key such as `f5` is valid.

One shortcut per button. No sequences or macros.

## Small by design

- Single native `.exe` under 400 KB
- No installer
- No runtime to install
- No admin rights
- Zero CPU when idle
- Optional config with up to two mappings

## Private by design

- No network connections
- No telemetry
- No logging
- No file or registry writes

The source is one C++ file and is open for inspection.

## Tray

The tray menu shows each mapped button and its shortcut.

Enable or disable Clicklet from the menu, or open the About box.

## Start with Windows

Clicklet doesn't add itself to startup.

To run it when you sign in, add a shortcut to `Clicklet.exe` in:

```text
shell:startup
```

## Limits

Clicklet is intentionally narrow:

- Mouse buttons 4 and 5 only
- One shortcut per button
- No sequences or macros
- No per-app profiles
- No media or volume keys
- Never remaps left, right, or middle click
- Doesn't work in apps running as administrator
- Mouse-vendor software can intercept the side buttons
- Unsigned, so Windows SmartScreen may warn on first run

## Specs

| | |
|---|---|
| Platform | Windows 11, x64 |
| Size | Under 400 KB |
| Language | C++ / Win32 |
| Dependencies | None to install |
| Install | Portable |
| Admin | Not required |
| Config | Optional `Clicklet.ini` |
| Mappings | Up to 2 |
| Price | Free |
| Code license | MIT |

## License

Clicklet's source code is licensed under the MIT License.

The Clicklet logo and icons are © Tal Tabakman, all rights reserved.

# Replay Tools

A Geode mod for Geometry Dash that adds an in-game menu (default key `Y`) with speed control, macro recording and playback, and No Clip.

**Geode 5.11.2 | Geometry Dash 2.2081 | Windows**

## Features

- **Speed control:** presets from 0.25x to 5x, plus fine adjustment in steps of 0.1 and 0.5
- **Macro recording and playback** at 240 TPS using the GDR2 format; recordings are saved as `<level ID>_<level name>.gdr2`
- **Macro import:** load GDR2/GDR files and zBot `.zbf` files; list and delete saved macros from the menu
- **No Clip:** ignore deaths during a session (resets when the game restarts)
- **Safe mode (on by default):** while speed changes, macro playback, or No Clip are in use, the level is marked as test mode, so progress is not saved or submitted

## Install

Copy `namakamu.replaytools.geode` to `%localappdata%\GeometryDash\geode\mods` and restart the game.

## Usage

1. Enter a level and press `Y` to open the Replay Tools menu. The key can be changed in the mod settings.
2. On the **Speed** tab, drag the slider, pick a preset, or fine-tune with the `-` / `+` buttons.
3. On the **Macro** tab, press **Record** to restart the level and record your inputs. Press **Stop** (or finish the level) to save the macro.
4. On the **Files** tab, press **Load** next to a macro, then press **Play** on the **Macro** tab to replay it.
5. Use **Open Folder** to open the macros folder, or **From Settings** to load a file chosen in the mod settings. Safe Mode and No Clip are on the **Options** tab.

## Settings

| Setting | Description |
| --- | --- |
| Safe mode | Marks the level as test mode when speed, macro playback, or No Clip is active. On by default. |
| Menu key | Letter or number that opens the menu in a level. Default: `Y`. |
| Game speed | Speed multiplier from 0.1 to 5.0. `1.0` is normal. |
| Macro file | Pick any macro file, then press **From Settings** in the menu to load it. |

## Notes

- Disabling safe mode lets results from these features be saved and submitted, which may be flagged by the game's anti-cheat. Use at your own risk.
- Classic levels, normal mode, and 240 TPS only. Macros recorded at other TPS values are converted and may be inaccurate.
- Platformer macros are not supported.
- The hotkey works on Windows only.

## Building

Requires the [Geode SDK](https://docs.geode-sdk.org/getting-started/). Clone the repository and build with CMake:

```
geode build
```

The repository also includes a GitHub Actions workflow that builds the mod on every push.

## License

All rights reserved unless a license file is added to this repository.

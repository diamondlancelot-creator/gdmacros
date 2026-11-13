# Replay Tools

A Geode mod for Geometry Dash that adds a menu (default key `Y` in a level, `Ctrl+Y` anywhere else) with speed control, macro recording and playback, and No Clip.

**Geode 5.11.2 | Geometry Dash 2.2081 | Windows**

## Features

- **Speed control:** presets from 0.25x to 5x, plus fine adjustment in steps of 0.1 and 0.5
- **Macro recording and playback** at 240 TPS using the GDR2 format; recordings are saved as `<level ID>_<level name>.gdr2`
- **Macro import:** press **Import...** in the menu (works inside a level) to pick a GDR2/GDR or zBot `.zbf` file; it is copied to the macros folder and loaded. Saved macros can be listed and deleted from the menu
- **Auto Play:** turn it on in the Options tab, load a macro, and it starts by itself whenever you open a level (resets when the game restarts)
- **No Clip:** ignore deaths during a session (resets when the game restarts)
- **Safe mode (on by default):** while speed changes, macro playback, or No Clip are in use, the level is marked as test mode, so progress is not saved or submitted

## Install

Copy `namakamu.replaytools.geode` to `%localappdata%\GeometryDash\geode\mods` and restart the game.

## Usage

1. Press `Y` inside a level, or `Ctrl+Y` from any other screen (main menu, level browser, etc.), to open the Replay Tools menu. Record / Play / Save need a level; Files, Import and Options work everywhere. The key can be changed in the mod settings.
2. On the **Speed** tab, drag the slider, pick a preset, or fine-tune with the `-` / `+` buttons.
3. On the **Macro** tab, press **Record** to restart the level and record your inputs. Press **Stop** (or finish the level) to save the macro.
4. On the **Files** tab, press **Import...** to pick a macro file from anywhere on your PC, or press **Load** next to a macro that is already in the list. Then press **Play** on the **Macro** tab to replay it.
5. Use **Open Folder** to open the macros folder. Safe Mode and No Clip are on the **Options** tab.

## Settings

| Setting | Description |
| --- | --- |
| Safe mode | Marks the level as test mode when speed, macro playback, or No Clip is active. On by default. |
| Menu key | Letter or number that opens the menu in a level. Default: `Y`. |
| Stop key | Letter or number that switches the macro off while a level is being played. Default: `X`. |
| Game speed | Speed multiplier from 0.1 to 5.0. `1.0` is normal. |

## Notes

- Disabling safe mode lets results from these features be saved and submitted, which may be flagged by the game's anti-cheat. Use at your own risk.
- Classic levels and 240 TPS only. Practice mode is supported: the macro follows the level's own step counter, so dying rewinds it to the last checkpoint exactly, regardless of how many clicks were made. Record and Play always start from the beginning of the level (practice checkpoints are cleared first). Macros recorded at other TPS values are converted and may be inaccurate.
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

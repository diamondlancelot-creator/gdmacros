# Replay Tools

Geode mod for Geometry Dash. Opens an in-game menu (default key `Y`) with speed control, macro recording and
playback, and No Clip.

Geode 5.11.2 | Geometry Dash 2.2081 | Windows

## Features

- Speed presets (0.25x to 5x) and fine adjustment
- Record and play macros at 240 TPS (GDR2); recordings are saved as `<level ID>_<level name>.gdr2`
- Load GDR2/GDR and zBot `.zbf` files; list and delete macros from the menu
- No Clip (resets on game restart)
- Safe mode, on by default: with speed changes, macro playback or No Clip, the level is marked as test mode and
  progress is not saved or submitted

## Install

Copy `namakamu.replaytools.geode` to `%localappdata%\GeometryDash\geode\mods` and restart the game.

## Notes

- Disabling safe mode lets results from these features be saved and submitted, which may be flagged by the
  game's anti-cheat. Use at your own risk.
- Classic levels, normal mode and 240 TPS only. Macros from other TPS are converted and may be inaccurate.
- The hotkey works on Windows only.

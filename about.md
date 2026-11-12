# Replay Tools

Replay Tools adds a clean in-game menu to Geometry Dash. Press `Y` inside a level to open it, with no need to go through the pause screen.

## What it does

- **Speed control:** choose a preset (0.25x, 0.5x, 1x, 2x, 3x, 5x) or adjust the speed in small steps with the `-` / `+` buttons.
- **Macro recording and playback:** record your inputs at 240 TPS and replay them later. Macros use the GDR2 format, so they work with other bots.
- **Macro import:** load GDR2/GDR files and zBot `.zbf` files. Macros made at a different TPS are converted to 240 TPS automatically.
- **File manager:** browse, load, and delete your saved macros directly from the menu, or open the macros folder.
- **No Clip:** ignore deaths for practice and testing. It resets when you restart the game.

## Safe mode

Safe mode is **on by default**. While you use speed changes, macro playback, or No Clip, the level is marked as test mode. Progress and completions are not saved or submitted.

You can turn safe mode off from the menu or the mod settings, but doing so lets results be saved and submitted, which may be flagged by the game's anti-cheat. Use at your own risk.

## Limitations

- Classic levels only (no platformer macros)
- Normal mode only (no practice mode recording or playback)
- 240 TPS is the native format; other TPS values are converted and may be less accurate
- The menu hotkey works on Windows only

## Settings

- **Safe mode:** on by default
- **Menu key:** the key that opens the menu (default `Y`)
- **Game speed:** 0.1x to 5.0x
- **Macro file:** choose a file, then press **From Settings** in the menu to load it

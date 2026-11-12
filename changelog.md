# Changelog

## v1.5.0

- Practice mode macros are now synced to the level itself: the macro frame is the level's own physics step, so respawning at a checkpoint rewinds the macro to exactly that point instead of relying on our own counter or the number of clicks
- Record and Play in practice mode now always start from the beginning of the level (existing checkpoints are cleared first)
- Added an **Import...** button on the Files tab: pick a macro file from inside a level, it is copied to the macros folder and loaded right away
- Removed the "Macro file" mod setting and the **From Settings** button, which could not be used from inside a level

## v1.4.1

- Removed the notifications shown when macro recording or playback starts, stops or finishes
- The song now follows the speed (its pitch changes with the speed setting)
- Macro recording and playback now work in practice mode: the macro rewinds to the checkpoint when you respawn

## v1.4.0

- Redesigned the menu with a modern, web-style look: a tabbed window (Speed, Macro, Files, Options) in dark navy with a single blue accent, bordered cards, flat buttons and a live Safe Mode badge in the header
- Added a draggable speed slider, with presets and fine-tune buttons
- Added smooth open, tab-switch and button-press animations; click outside the window to close it
- Added switch-style toggles for Safe Mode and No Clip
- All in-game text and mod settings are now in English
- Improved loading of macros recorded at other TPS (for example 360): duplicate events are removed and very short clicks are kept after conversion

## v1.3.1

- Fixed a build error caused by invalid content at the top of `src/main.cpp`
- Safe mode now also covers No Clip: the level is marked as test mode while it is on
- Menu text and settings polished

## v1.3.0

- Added No Clip (resets when the game restarts)
- Added safe mode toggle to the menu, on by default
- Added a "Safe mode off" warning when progress could be saved

## v1.2.0

- Added support for loading GDR2/GDR and zBot `.zbf` macros
- Macros recorded at other TPS values are converted to 240 TPS automatically
- Added a macro file list with load, delete (with confirmation) and open-folder buttons
- Added a setting to choose a macro file and load it with the **From Settings** button

## v1.1.0

- Added macro recording and playback at 240 TPS using the GDR2 format
- Macros are saved as `<level ID>_<level name>.gdr2`; existing files are never overwritten
- Manual input is blocked while a macro is playing

## v1.0.0

- Initial release
- Added an in-game menu opened with a hotkey (default `Y`, configurable in settings)
- Added speed control with presets from 0.25x to 5x and fine adjustment buttons
- Added safe mode: levels are marked as test mode when speed is changed, so progress is not saved or submitted

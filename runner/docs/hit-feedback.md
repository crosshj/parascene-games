# Block-hit feedback

Both Story's BOX_HIT event and Arcade's collision trigger one feedback pulse.
A BOX_JUMP, another-lane box, or idle traversal does not trigger it. Existing
one-mark-per-box and ST_HIT transitions prevent repeated triggers on one box.

The four-tick (about 67 ms at 60 Hz) controller pulse uses both motors, with
large-motor strength 64/255. The small motor is on/off only, so its light touch
comes from the short duration. Startup now sends the DualShock configuration
commands to map both motors before BIOS polling starts. Older controllers that
do not accept the configuration retain the legacy small-motor fallback. This
latest two-motor change has not been built or tested, at the user's request;
the emulator results below describe the earlier small-motor implementation.

The screen
alternates by at most one pixel for four rendered frames, then settles. Drawing
offsets apply to the active framebuffer only, with its normal clipping rectangle;
the camera, path sampling, gameplay and baked visibility do not move. Every frame
sets its offset explicitly, so neither double buffer retains a stale shake.
Pause, menus and restart cancel both effects. Screen shake is independent of
controller status: an incomplete BIOS packet must not erase a confirmed hit.
Motor output tolerates up to three invalid polling ticks; four consecutive
invalid ticks cancel it. Reconnection after sustained loss cannot resume an
old motor pulse. Hit targets, collision judgement and scoring are unchanged.

Controller polling and memory-card I/O remain with the BIOS. Rumble uses the
[documented legacy small-motor protocol](https://psx-spx.consoledev.net/controllersandmemorycards/#old-method-one-motor-no-config-commands-scph-1150-scph-1200-scph-110)
and the BIOS's internal pad-output setter. The
[documented output-clipping patch](https://psx-spx.consoledev.net/kernelbios/#patch_optional_pad_output)
is applied only to resident kernel RAM after checking the exact clamp/store
instruction sequences and setter shape. Unknown BIOS/HLE layouts skip rumble
without altering input. No BIOS file is changed. Digital pads ignore the motor
bytes. Emulator vibration requires a compatible controller mapping.

The host test checks pulse length, one-pixel bounds, no spontaneous feedback,
pause/restart/disconnect/reconnect cancellation, and rejection of unknown or
truncated BIOS layouts. It runs in `check_world_visuals`. An optional BIOS-file
argument verifies that a recognized layout exists without changing the file.
The installed SCPH-5501 BIOS passed that check. A clean DuckStation boot of the
rebuilt `build/runner.cue` logged `Hit feedback: BIOS rumble output enabled`,
confirming that the actual resident BIOS accepted the setup. Physical vibration
and collision feedback during emulator gameplay still require verification.

The standalone hardware-path test subsequently exposed a buffer-layout bug:
the BIOS consumes output byte 0 as an enable flag, then transmits bytes 1 onward.
The old buffer put `40 01` at bytes 0/1, so setup succeeded but no motor activated.
The corrected buffer is `01 40 01`: enable, motor byte 1, motor byte 2. The host
test now checks those actual buffer bytes independently of the state helper.

Build `runner_rumble_test` and cold-boot `runner/build/runner_rumble_test.exe`
in DuckStation with Debug logging to file enabled. It uses the same feedback
implementation and card/pad initialization as the game, without writing saves.
It requests three normal 100 ms hit pulses and one longer pulse, then stops.
Validate the saved log with:

```
python3 runner/tools/check_rumble_log.py runner/build/rumble-emulator.log
```

On the installed DuckStation, the before-fix trace requested all four pulses
but contained no motor activations. The after-fix trace contains four matching
`Set small motor to 1, large motor to 0` activations and four stops. SDL also
reports rumble support for the connected DualSense. This verifies actual
emulated motor output, not just buffer intent or startup. Physical vibration
still needs to be felt by the user. Diagnostic runs restored the prior logging
level and closed their emulator processes afterward.

The inspected DuckStation configuration has an AnalogController with both SDL
motor bindings present and `InputSources/SDLControllerEnhancedMode = true`.
The installed application's help text identifies Enhanced Mode as providing
Bluetooth vibration/LED support for DualSense controllers. The automated settings
change aborted when the quit request was canceled; the setting was subsequently
observed enabled before the clean boot. That boot detected the DualSense as
`SDL-0`. These checks establish configuration and game output setup, not proof
that the physical motor moved.

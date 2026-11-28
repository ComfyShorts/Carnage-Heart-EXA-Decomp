# Controls (PC port)

The keyboard and any plugged-in gamepad stand in for the PSP's buttons and analog
stick. Both work at once. Gamepads can be plugged in and out while the game runs.
`chexa-pc --controls` prints the table below, with any changes from your config file.
A short list is also logged at start-up.

The code is `src/platform/controls.c` (the bindings, the config file, the help hint)
and `src/platform/input.c` (the live state, hotplug, `sceCtrlPeekBufferPositive`).
`chexa-controls-test` checks the mapping.

## The defaults

| PSP | Keyboard | Gamepad (SDL names) |
|---|---|---|
| D-pad up / down / left / right | Up / Down / Left / Right | `dpup` / `dpdown` / `dpleft` / `dpright`, and the left stick |
| ○ Circle | Z, Return | `b` (east) |
| × Cross | X, Backspace | `a` (south) |
| △ Triangle | S | `y` (north) |
| □ Square | A | `x` (west) |
| L | Q | `leftshoulder`, `lefttrigger` |
| R | W | `rightshoulder`, `righttrigger` |
| START | Space | `start` |
| SELECT | Right Shift | `back` |
| Analog stick up / down / left / right | I / K / J / L | left stick (`lefty-` / `lefty+` / `leftx-` / `leftx+`) |
| (quit) | Escape | |

- **Keys** are bound by position (SDL scancodes), named for a US layout. On an AZERTY
  keyboard, Circle is still the key left of X, wherever its label says.
- **Gamepad buttons** are bound by position, as on a PlayStation pad: east is Circle and
  south is Cross. On an Xbox-style pad that is B for Circle and A for Cross.
- **This game uses Circle to confirm and Cross to go back.** Rebind them if you want A
  to confirm.

## The config file

Your own bindings go in `controls.ini` in your config folder, never in the repository:

| System | File |
|---|---|
| Linux / BSD | `$XDG_CONFIG_HOME/chexa-pc/controls.ini`, else `~/.config/chexa-pc/controls.ini` |
| Windows | `%APPDATA%\chexa-pc\controls.ini` |
| macOS | `~/Library/Application Support/chexa-pc/controls.ini` |

The environment variable `CHEXA_CONTROLS` can name another file. `chexa-pc
--write-controls` writes the current bindings there, with comments, to edit. It won't
overwrite a file that's already there.

```ini
[keyboard]
circle = Keypad 6, Return     # replaces Z, Return
select = none                 # unbound
[gamepad]
circle = a                    # A confirms
cross = b
l = lefttrigger               # the shoulder button no longer
[options]
stick_moves_dpad = no         # the stick only moves the PSP's stick
deadzone = 30                 # percent of tilt before a stick or trigger counts
```

- **Control names:** `up down left right circle cross triangle square l r start select
  stick_up stick_down stick_left stick_right`.
- **Inputs:** a control lists up to four, separated by commas. A control named in the
  file replaces its default list; controls not named keep their defaults.
- **Key names** are SDL's scancode names: `Z`, `Return`, `Right Shift`, `Keypad 8`,
  `F1` and so on.
- **Gamepad names:**
  - buttons: `a b x y` (or `south east west north`), `back start guide`, `leftstick
    rightstick`, `leftshoulder rightshoulder`, `dpup dpdown dpleft dpright`;
  - axes, with a direction: `leftx- leftx+ lefty- lefty+ rightx- rightx+ righty-
    righty+`;
  - the triggers: `lefttrigger righttrigger`.
- **Comments** start with `#` on their own line, or after a value with a space before
  the `#`.
- **Errors:** a line that can't be used is skipped and changes nothing. Start-up logs how
  many lines were skipped and what was wrong with the first.

## Which buttons a screen uses

Every screen's help window goes through `ui_help_draw`. The game's help text names the
buttons it wants in a fixed set of words:

- "○ button", "× button", "△ button", "□ button" (Shift-JIS symbols);
- "L and R buttons", "L button", "R button";
- "START button", "SELECT button";
- "directional buttons";
- "up and down buttons", "left and right buttons".

When the help shown names any of these, the window title lists their keys (and the
gamepad's, if one is connected), for example `D-pad Up/Down/Left/Right | pad D-pad or
stick` on Select Area. The log says the same (`[input] this screen's help names: ...`).
The game's own screen is left as it is.

## What is real and what is the port's

| Part | Real (the game's or the PSP's) | The port's choice |
|---|---|---|
| The buttons | The 12 PSP buttons and their bits (`PSP_CTRL_*`), read once a frame through `sceCtrlPeekBufferPositive` as the game does (`FUN_08b1ed50`) | Which key or gamepad input stands for each |
| The stick | The game sets analog sampling at start-up and keeps the stick's position, counting anything within 0x40 of the centre (0x80) as centred (`FUN_08b1ed50`). The only reader found hands it to the program chips' pad state (`FUN_08b0e36c`, `FUN_08974dfc`) | The stick is passed through in analog mode. The manual-control chips (Button Input, Assign Button, Assess Analog) aren't wired to the pad in the battle stand-in yet, so the stick has no effect in battle |
| The stick moving the D-pad | On the PSP it doesn't: the menus read only the D-pad | `stick_moves_dpad = yes` by default, as asked for; turn it off in the config |
| Deadzone | The game's own 0x40 of 0x80, i.e. 50% | The same 50% by default, also used for triggers as L / R |
| Help hints | The words come from the game's help strings | Matching them to keys and showing them in the title bar |

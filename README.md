# SYNC'S TOOLKIT (v3.2.0)

Made by **Sync**. An Overstrike script for Marvel's Spider-Man 2 (PC). Needs the ModSettings script.

Everything is in the pause menu under **SYNC'S TOOLKIT**. No game files are touched and nothing is hooked.

## Menus

| Menu | What it does |
| --- | --- |
| QUICK SWITCHES | God mode, fly mode, SWING SPEED and HEAL NOW on the first screen |
| HEALTH | Heal, add or take health (pick 25, 50, 75, 100 ... 5000), slow regen, custom max health, low health guard |
| ENEMY | DAMAGE THEY DO (0.25X to 10X) and HEALTH THEY HAVE (0.25X to 10X) |
| SWING SPEED | 1X to 5X speed while swinging, gliding or falling fast (F6 steps through the speeds) |
| FLY | Speed, boost, smoothness, steering (follow camera or strafe), forward direction, safe landing, controller fly button and controller up and down buttons |
| TELEPORT | Jump forward, back, up, down, undo, 5 saved spots, copy my position and go to a copied position (meet up with friends), position logger |
| HOTKEYS | Every key and button |
| DIAGNOSTICS | What works, live position, crash log, save report to file |
| SETTINGS | Keyboard hotkeys on or off, save, panic, reset |

## Keys and buttons

| Input | Action |
| --- | --- |
| R | Fly on or off |
| Click both sticks (controller) | Fly on or off (D-pad up tap or hold are options in the FLY menu) |
| F6 | Next swing speed |
| W A S D, Y, Left Ctrl, Left Shift | Fly, up, down, boost |
| Left stick, right trigger, left trigger, left stick click | Fly, up, down, boost on a controller (bumpers or buttons for up and down are options) |
| F5 | God mode |
| F7 | Log position to positions.csv |
| Home | Jump forward |
| F10 | Undo last jump |
| Insert and Delete | Jump distance |
| Page Up and Page Down | Fly speed |
| End | Panic, everything off |

F8 and F9 are not used.

## Enemy multipliers

* **DAMAGE THEY DO** scales every hit you take, 0.25X to 10X. At 0.25X a hit does a quarter of its damage. At 10X it does ten times.
* **HEALTH THEY HAVE** scales the health of enemies near you (within 250 units), 0.25X to 10X. It keeps how hurt they are, so a half dead enemy stays half dead. Back to 1X puts them back to normal.
* The health tool finds enemies through the game's actor table and checks that table against your hero first. If the check fails it turns itself off and DIAGNOSTICS says why. It never writes to a guess.
* It scales anything near you that has a Health component and is not you, so a friendly character or a civilian can be scaled too.
* PANIC and BACK TO NORMAL set both to 1X.

## What is new in 3.2

* **Fly no longer switches itself off.** Saving settings used to force fly off. Saves happen whenever settings change, so fly kept unchecking. Fixed.
* **Controller.** Xbox pads and anything Steam Input or DS4Windows turns into one work as before. DualSense and DualShock 4 pads are now read directly (USB or Bluetooth), so they work without Steam Input. DIAGNOSTICS shows which kind was found. The default fly button is now **click both sticks**, because D-pad up also scrolls the pause menu and used to flip fly by accident.
* **Swing speed.** New SWING SPEED menu and a quick option on the first screen. It adds a share of the distance the game moves you, so it works without touching any game code. It only acts when you are already moving fast. Turn on BOOST RUNNING TOO to speed up running as well.
* **Easier health.** HEALTH AMOUNT, REGEN SPEED and MAX HEALTH are lists (25, 50, 75, 100 ...) instead of sliders you drag one number at a time. The ADD and TAKE buttons show the amount.
* **Enemy health.** Changing it again and again is now safe. Enemies are no longer forgotten while still scaled (which made the next change stack on top of the old one), scaled enemies are kept in step with the setting wherever they are, a hero swap restores the other hero's health, and every change is written to log.txt. DIAGNOSTICS shows how many writes the game kept or refused.

## Meet up with a friend

1. You: TELEPORT, COPY MY POSITION. Paste it to your friend in chat. It looks like `SYNC 123.45 67.89 -10.00`.
2. Friend: copy that text, then TELEPORT, GO TO COPIED POSITION.

## If something goes wrong

DIAGNOSTICS shows crashes the toolkit saw. The files live in `<game folder>/SyncsToolkit/`:

* `crash_report.txt` and `crash_report_old.txt` list each crash with the module and offset, so you can tell whether it was the toolkit or something else
* `log.txt` is this session, `log_prev.txt` is the session before (the log is kept across a crash)
* `diagnostics.txt` is written by SAVE REPORT TO FILE
* `settings.ini`, `spots.txt`, `positions.csv`

Send those files if something misbehaves.

## Notes

* Built and syntax checked, but not run against a live game. Check DIAGNOSTICS on first launch.
* Swing speed moves you by writing your position. If DIAGNOSTICS shows BOOSTS stuck at 0 while you swing, or you feel yourself pulled back, the game is overriding it, and send log.txt.
* Flying follows the camera sideways: the game turns your hero toward the way you push relative to the camera, and the toolkit flies that way. Up and down are Y and Left Ctrl. Looking up or down with the camera does not tilt the flight, because the toolkit has no verified way to read the camera yet.
* The default controller fly button is click both sticks. If you pick TAP D PAD UP, scrolling a menu with the D-pad will flip fly.
* Fly moves the hero by writing its position. If the game keeps moving you back, DIAGNOSTICS says FLY CHECK: GAME KEEPS MOVING YOU BACK.
* If forward flies sideways, change FORWARD DIRECTION in the FLY menu.
* Remove the old SM2Toolkit entry from Overstrike. It is replaced by this script.

## Build

Upload this folder to GitHub and run the workflow (see `.github/workflows/build.yml`), or run `./build.ps1` with Visual Studio 2022 and CMake. Output: `SyncsToolkit.script`.

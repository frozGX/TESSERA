# Tessera

Snaps windows into zones while you drag them. It sits in the tray, has no installer and never uses the network.

## Using it

1. Run `Tessera.exe`. A frog icon appears in the tray.
2. Start dragging a window by its title bar.
3. **Right-click** once to show the zones.
4. Drop the window on a zone. It fills that zone.

While the zones are showing, press **1** to **9** to switch to that preset. The preset is remembered per monitor.

Hold **Ctrl** while the zones are showing to stretch the window across several: it will cover everything from the zone the pointer was in when you pressed Ctrl to the zone it is in when you drop. Spanning stays within one monitor and one preset: carried to another monitor, the window fills just the zone it is dropped on. (Settings lets you pick another key for this, or turn it off.)

Right-click again to hide the zones for that drag. **Esc** cancels the move as usual, and nothing is snapped.

## The tray icon

Right-click it for **Settings**, **Edit Zones**, **Restart as Administrator** and **Close** (which exits Tessera). With zone edits or settings changed and not yet saved, Close and Restart bring that window forward instead, so nothing is lost by accident. While the zone editor is open, Settings brings the editor forward instead of opening, and dragging a window shows no zones: finish with the editor first.

**Restart as Administrator** starts Tessera again with administrator rights, after Windows asks you; saying no leaves everything as it was. Windows only lets a program move an administrator's windows if it is one itself, so this is what lets Tessera snap those. While it runs that way the icon is the golden toad instead of the frog, and the menu item reads *Running as Administrator*. Start with Windows starts it the ordinary way. A left click opens the zone editor. Running `Tessera.exe` while it is already running opens nothing: it only puts the tray icon back if it had gone missing.

## Editing zones

The editor covers the monitor your mouse is on, so zones are shown at their real size.

| Do this | To |
|---|---|
| Click a zone | split it left and right at the mouse |
| Shift+click a zone | split it top and bottom |
| Drag a line | move it |
| Right-click a line | remove it (only when it separates exactly two zones) |
| Right-click a zone | close without saving, the same as Esc. While dragging a line, it puts the line back instead |
| Drag a line with Shift held | move it pixel by pixel instead of in whole percents |
| Double-click a line | type where it should stand: `60%` of the way across (or down) the screen, or `1200px` for the size of the zone to its left (or above it). A bare number is a percentage up to 100 and pixels beyond that. Enter moves the line, as far as the 5 % rule allows; Esc leaves it be |
| Arrow keys | nudge the line you last touched by one pixel (ten with Shift): left and right for upright lines, up and down for level ones |
| Ctrl+click a zone | split exactly at the mouse instead of at a whole percent |
| 1 to 9, or the numbered buttons | choose the preset to edit |
| Type in the box | name the preset. Left empty, the box shows the preset's shape instead, such as `41% / 59%`, and keeps it up to date |
| Clear preset | empty it, so its number does nothing |
| Monitor 1 of 2, or M | with more than one monitor: move the editor to the next, and show the preset it uses |
| Ctrl+Z, or Undo | take back the last change |
| Save, or Ctrl+S | keep the changes and carry on editing. The preset on screen also becomes the one this monitor uses, so Save lights up as soon as you pick a different preset |
| Close | close the editor without saving, as Esc does: to keep changes, press Save first. Save is dim while there is nothing to save |
| Esc | close without saving: every preset goes back to how it was at the last save |

While a line is moving, by the mouse or the arrows, a label beside it shows the size in pixels of what lies either side and where the line stands as a percentage.

No zone can be smaller than 5 % of the screen, and a preset holds at most 128 zones: once it is full, a click no longer splits.

If a monitor is plugged in or unplugged, or the screen's resolution changes, while the editor is open, the editor saves what it has and closes, since the screen it was drawn for is no longer the same.

## Settings

A change shows at once, but is only kept when you press **Save**, which leaves the window open and goes dim until something else changes. Closing the window with the X or Esc puts anything not saved back as it was; Tessera itself keeps running in the tray. **Reset to default** puts every setting here back (your zones are not touched), and also needs Save. The window opens wider than tall, with Behaviour beside Look and Theme and the preview right across below them. It can be dragged to any size and shape, or maximized: the text and controls grow with it, and height to spare deepens the preview. Made taller than wide, it stacks the four sections in one column instead. The size is remembered.

Everything also works from the keyboard: Tab and Shift+Tab move between the controls, Space or Enter presses the one in focus, and the left and right arrows move a slider (five steps at a time with Shift).

**Behaviour**

- **Hold a key to show zones**: Off (the default), Shift, Ctrl or Alt.
- **Hold a key to span zones**: Off, Shift, Ctrl (the default) or Alt. With the zones showing, this key stretches the window across several. If it is the same key that shows the zones, spanning is off and the window says so.
- **Right-click while dragging shows the zones** (on by default). The zones only ever show on a right-click or the key; keys 1 to 9 always switch preset while they are showing.
- **Start with Windows**.
- **Keep Windows' own snapping and snap bar out of the way** (on by default): Windows' edge snapping is switched off for as long as Tessera's zones are on screen, and back on the moment they are not, so the two never both act on a drop; that part is never saved to your Windows settings, and if Tessera were to stop in the middle of a drag, starting it again puts Windows' snapping back. The bar of layouts Windows 11 drops down from the top of the screen during a drag is switched off as well, by flipping the Windows setting "Show snap layouts when I drag a window to the top of my screen" (Settings > System > Multitasking) when you press Save; unticking and saving switches it back on, if it was Tessera that switched it off: a bar you turned off in Windows yourself stays off.

**Look**

- **Opacity of the zones**: 5 % to 95 % (25 % by default). The zone a drop would fill is drawn a little more solid than the rest.
- **Space between windows**: pixels left between snapped windows and around the screen edge.
- **Padding between zones**: how far apart the zones are drawn while you drag, from none (edge to edge) to 24 pixels, on top of any space between windows. This only changes how the zones look, not where windows land.
- **Fade in and out**: how long the zones take to appear and disappear, up to 200 ms (80 by default), or Off for at once. The highlight moving between zones keeps pace with it.
- **Show a number in each zone**, **Show the preset's name**.

**Preview**

The current preset drawn over a mock desktop, in the proportions of your screen, with the look and colours you have chosen. The fill of a zone is as faint as the opacity says; its border and number stay crisp.

**Theme**

- **Slate, Frog, Gold, Mono**: ready-made sets of the four colours. The one in use is lit.
- **Colours** of the zones, the highlighted zone, the border and the numbers. Click a swatch to change it.

Presets and settings are kept in `Tessera.cfg` beside the exe. If Tessera finds something in it that it can't use (a preset edited by hand so that it no longer covers the screen, say), it keeps a copy of the file as `Tessera.cfg.damaged` before carrying on without that part.

## Good to know

- Turn off FancyZones in PowerToys while using Tessera, or both will draw zones.
- Windows that can't be resized are left alone.
- Programs running as administrator can't be snapped unless Tessera is also run as administrator. A Tessera running as administrator trusts the folder it sits in (its exe and `Tessera.cfg`), so on a shared computer keep it somewhere only administrators can write.
- Tessera only watches the keyboard and mouse during a window drag, and for a moment after it if a number key or the right button it took is still held down.
- If Save beeps and stays lit, the settings file could not be written just then (another program was holding it, or the folder can't be written to); press it again.

## Building

`WIP\Build\Build.ps1` builds into `%LOCALAPPDATA%\Tessera-build`. `Run-Tests.ps1` runs the checks without showing
anything on screen. `Make-Icon.ps1` redraws the two icons. `Make-Release.ps1` makes the zip in `Release\`, and
`Update-Portable.ps1` installs it into `Portable\Tessera`.

## Licence

Tessera is free software under the [MIT licence](LICENSE). Copyright (c) 2026 BabaYaeger.

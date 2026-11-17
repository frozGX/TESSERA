# Ideas not built

Things that were discussed and set aside, with enough of the thinking to pick them up later.

## Remember where apps go

**Set aside 2026-11-16: "sounds risky". Not to be built until asked for.**

When a window is snapped, note the program and the zone. When that program next opens a window, move it into
that zone unasked.

How it would work:

- **Remembered:** the program by its exe path, and the monitor, preset and zone it was last snapped to. Kept in
  `Tessera.cfg`, updated on every snap of that program.
- **When it acts:** on a new top-level window appearing, heard through a passive Windows event like the one that
  already announces a drag, so nothing runs while idle. Once per window, at creation; a window the user has
  since moved is never touched again.
- **Controls:** one setting to turn it on, off by default; a way to forget one program or all of them.

Why it is risky:

- **Several windows from one exe** (Explorer, browsers, Blender's extra windows). "Last zone wins" stacks a second
  window on the first. FancyZones does the same.
- **The preset has changed since.** Zone 2 then means something else. Restore only when the remembered preset is
  the one in use; otherwise leave the window alone.
- **Dialogs, splash screens, palettes** must be skipped. Ignoring windows that can't be resized covers most, but
  some programs would need an exclude list.
- **Programs that restore their own position** appear in one place and then jump. There is no clean way round it.
- **Administrator programs** can't be moved.

The core is about a day of work; the cases above are where the time would go, and it is the feature most likely
to do something unexpected. Keyboard snapping would give most of the benefit with none of the guessing.

Left open: whether it should apply to every program snapped, or only ones the user marks.

## Other suggestions, not yet decided on

From the list given with 0.3.1:

- **Everyday use:** snap by keyboard (hotkey and arrows); restore a window's earlier size when it is dragged
  out of a zone.
- **Editor:** duplicate and swap presets; one-click templates.
- **Settings and look:** hex colour entry; a list of excluded programs.
- **Housekeeping:** a first-run notice that Tessera is in the tray; export and import of presets and settings;
  run as administrator at sign-in (0.4.0 can restart itself as administrator from the tray, but Start with
  Windows still starts it the ordinary way).

Built in 0.4.0, from the same list: spanning zones while dragging, typing an exact size for a line, the monitor
picker, theme presets, and the fade setting.

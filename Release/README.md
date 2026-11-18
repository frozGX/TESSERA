# Releases

| Version | Date | Notes |
|---|---|---|
| 0.5.6 | 2026-11-18 | Bug fixes: a fade of 110 ms and preset names like '50 / 50' no longer reset on start; Save no longer switches Windows' snap bar back on unasked; zones work with the mouse buttons swapped; the drop is settled the moment the button comes up; windows inside another program's window are left alone; clicking away from the editor's size box no longer splits a zone; zone numbers stay in reading order after a line moves. |
| 0.5.5 | 2026-11-17 | Editor: Save & Close is now Close, which closes without saving |
| 0.5.4 | 2026-11-17 | Padding between zones is 2 px by default; running Tessera.exe again no longer opens the editor |
| 0.5.3 | 2026-11-17 | Icons: slightly thicker bevels on the tiles |
| 0.5.2 | 2026-11-17 | Icons on a four-tile mosaic, with slightly thicker lines |
| 0.5.1 | 2026-11-17 | Settings: maximize works, Save keeps the window open, wider default size, span key chosen with buttons, one tick for Windows' snapping, fade 80 ms by default; two options and the Edit Zones button removed. Icons on a nine-tile mosaic |
| 0.5.0 | 2026-11-16 | Settings opens wider than tall, in two columns with the preview right across; its text grows as the window is widened |
| 0.4.9 | 2026-11-16 | Second bug hunt: the tray icon is no longer loaded afresh without the old one being released when Explorer restarts. The tests now include 200,000 random layout edits and long random sessions in the editor, settings and drag overlay, with leak checks |
| 0.4.8 | 2026-11-16 | Bug hunt: Close and Restart as Administrator no longer throw away unsaved zone edits or settings (they bring that window forward); a bare number above 100 typed for a line is read as pixels; the drag overlay window is now covered by the automated tests |
| 0.4.7 | 2026-11-16 | Editor: Save keeps the changes and stays open; Save & Close keeps them and closes |
| 0.4.6 | 2026-11-16 | The administrator icon: scale sizes 18, 16, 14, 12 from the top row down |
| 0.4.5 | 2026-11-16 | The administrator icon: scale sizes 18, 16, 13, 12 from the top row down |
| 0.4.4 | 2026-11-16 | The administrator icon: the scales shrink row by row, the topmost the largest |
| 0.4.3 | 2026-11-16 | The administrator icon: the mouth is parted by a line along its middle |
| 0.4.2 | 2026-11-16 | The administrator icon: four scales a side, each fading from the body's colour to a deeper gold |
| 0.4.1 | 2026-11-16 | The administrator icon redrawn: the frog's own outline in gold, a few scales standing out from it, a straight outlined mouth between puffed cheeks, eyes of one colour |
| 0.4.0 | 2026-11-16 | Restart as Administrator in the tray menu, with a golden toad icon while it runs that way; hold Ctrl to stretch a window across several zones; double-click a line to type its place (60% or 1200px); a monitor picker in the editor; fade speed and theme presets in Settings |
| 0.3.1 | 2026-11-16 | Settings: each section on its own card, the bottom buttons on a bar, the version shown beside Save. Editor: right-click on a zone closes without saving like Esc, and puts a line back mid-drag. Dragging a line or a slider repaints at once instead of trailing the mouse, and Save's state settles when you let go |
| 0.3.0 | 2026-11-16 | Polish and speed: zones are translucent pixel by pixel (faint fill, crisp border and number, smooth corners) and fade in, out and between zones; windows redraw only what changed (a full editor repaint is about 4 ms); buttons react to the mouse; a line being moved shows its sizes, and arrow keys nudge it; Settings works from the keyboard and its preview has the screen's proportions; Windows' own snapping is paused while zones show |
| 0.2.5 | 2026-11-14 | Bug test: Settings keeps its size when moved between monitors with different scaling; no zones are offered for administrator windows that can't be moved; long shape names fall back to a zone count |
| 0.2.4 | 2026-11-14 | Settings: the preview has its own section, opacity runs from 5 % to 95 %, and the window is 30 % wider by default |
| 0.2.3 | 2026-11-14 | Padding between zones is a setting, from none (zones edge to edge) to 24 px; the editor guide sits in its own box under the panel; rounded corners throughout; Undo has an icon; Clear, Undo and Save are less tall; unnamed presets show their shape (41% / 59%) as the name; Save is dim until something changes; colours moved under the preview into a Theme section; the settings window is 20 % wider and resizes freely in both directions |
| 0.2.2 | 2026-11-14 | Tray menu restyled like Helios (roomy items, icons, rounded highlight); Settings has Save and Reset to default, no Close button, resizes with everything scaling, and can hide Windows' own snap bar; default zone opacity 25 %; shorter editor guide with the keys picked out; fixes from a bug test (see below) |
| 0.2.1 | 2026-11-14 | Zone editor: Save keeps changes (Ctrl+S too), Esc closes and discards them; the preset name no longer flickers as the mouse moves; Shift while dragging a line moves it by pixels |
| 0.2.0 | 2026-11-14 | Settings window for behaviour and look (opacity, spacing, colours, preview); tray menu is Settings, Edit Zones, Close; right-click is the default way to show zones; frog icon |
| 0.1.0 | 2026-11-14 | First release. Zones while dragging (held key or right-click), 1 to 9 switch preset mid-drag, layout editor with nine presets |

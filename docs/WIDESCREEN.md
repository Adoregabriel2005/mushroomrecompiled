# Experimental widescreen — v1.5.0

The level is presented on the 426 × 240 16:9 canvas without stretching sprites or tiles, or on the native 256 × 240 frame: the menu's VIDEO tab toggles between 16:9 and 4:3 (`widescreen` in `config.ini`, `--widescreen` / `--4:3`). The wide camera respects the map edges. The central frame and the HUD (rows 192–239) have their overscan cut — 12 pixels on each side — and are centred at 232 pixels, between two 97-pixel black bars.

The HUD is its own layer, as implemented in `src/hud.c`, and the block is copied from it with the overscan cut and centred. The row where the bar starts comes from the cartridge's scanline IRQ: the mapper records the frame's last IRQ, and the bar starts on the following row (240 when the frame has no split). From there, the first row that really belongs to the bar is the first that does not continue the previous row in the frame's per-scanline capture — the layer's `hud_bar_first()` — and the row the content is drawn from is the first below it that runs to the end of the image with the bar's CHR banks. The rows that the mid-frame split stages — the first one or two rows of the status area, which reach the video as a solid strip of the scenery's background colour (blue outdoors) or of the card — are drawn from the anchor: the background comes from the frame's own nametable and from the CHR at the bar's scroll, and the sprites on that row come from the captured OAM, as the hardware would. The bar then starts on its first row and nothing staged appears above it. The anchor row is also redrawn, because the PPU fetched its first tiles before the split write: that is where a remnant of the row above is left in the first columns. The following HUD rows come out identical to the native frame — the drawing reproduces them pixel by pixel — and the rows between the split and the bar's first row are redrawn from the capture itself, with the scroll the level had on that row, so the floor reaches the last scanline of the playfield while still being the `hud_build()` drawing. A frame without a split (title, map, menu) gets no layer at all. The machine's internal frame, the hashes and the save state do not change: the HUD layer only decides what is presented.

## Scenery and borders

The compositor reads the full map from SRAM and the metatile tables from the supplied ROM. CHR, palette and camera are observed at scanline 8, before the graphics switch to the HUD. The native 232-pixel centre is preserved. The 12-pixel strips at the edges of the original image are rebuilt from the map to remove tiles and attributes partly updated in VRAM (four columns beyond the 8-pixel NES blanking). Sprites in those strips are restored from the captured OAM.

## Object activation and persistence

From v1.4 on, the wide-mode simulation also changes: the level's real object records are activated in the enlarged area. It uses the original five main slots, without replacing active objects, duplicating spawn indices, or anticipating event controllers and the end-of-level card. Records are used only after scene initialisation. Right after loading, a short grace window also activates the records already visible in the margins (covered by the reveal of the mode); in steady state, only records strictly outside the image are activated, so nothing appears suddenly in the middle of play. Two free slots are always reserved for the original generator.

New objects enter the game's own initialisation state. Movement, animation, AI and collisions still run in the recompiled code. The horizontal culling comparisons now use the wide camera's edges, keeping each routine's specific margins. The vertical culling rule remains original.

The renderer re-runs the object's state routine on an isolated copy of the machine, with the object in the middle of the screen so no piece is cut at the edge (cut pieces later mixed into the frame were the cause of the jitter). Before calling it, it rewinds the slot's dynamic state to the pre-update positions saved from the previous frame: the routine moves the object as it draws, and without the rewind the pieces would run one phase of motion ahead of the frame (the plant's sway, the enemy's step). Positioning votes cannot fix pieces with the wrong phase. All of this state is kept in per-slot vectors (`$91`, `$76`, `$A3`, `$88`, `$BE`, `$D0`, `$689`, `$691`, `$9A`, `$669`, `$679`, `$AC`, `$B5`, `$651`, `$681`, `$D9`, `$518`, `$520`, `$7F`, `$7FD0`, `$C6`): the slot index must be added on every read and write. Without it the rewind always fell into slot 0 and the object under test kept its already advanced values — an animation tick and a frame of motion ahead of the image, which is the stem's tremor. It keeps only pieces relative to the rewound origin (offset, pattern, attributes and OAM order).

The rerun's sprite base is fixed per slot (`$058F = $40 + slot*16`), inside the OAM page. The game's generator rotates this base every frame and the drawers advance it with 8-bit addition: when it fell on `$E8`, the plant's block crossed `$02FF`, the stem stopped at offset 0, and the palette correction (`Sprite_RAM+$0A,Y`) was written to `$0302`, outside the page the PPU transfers — the stem kept the head's palette for a frame and flickered. The absolute offset does not matter for the pieces (they are relative), and the per-slot base preserves the priority order the margin pass uses to sort the pieces. Two OAM points with the same pattern and mirroring bits vote on positioning even if the palette disagrees (the game can disagree with itself for a frame); the claim that turns off repair, however, requires exact attributes, because over-claiming leaves the native strip unpainted and its garbage shows on screen. The snapshot only rotates when the machine has advanced: two presentations of the same frame (the edge test does this) must produce the same pieces, and re-recording the rewind target with the post-update state would draw a different animation tick.

Each object anchors its origin to the (piece, sprite) pair with the most votes per offset — the sprite region rotates and leaves old copies of the same patterns exactly 8 pixels apart, and anchoring to one of them would shift the whole sprite. Only slots within 2 pixels of the anchor are claimed; the rest belong to the frame repair, which shows them exactly as the PPU drew them. With no pair (entirely in the margin), it keeps the previous anchor. Each OAM slot has exactly one owner: the strip repair skips claimed slots, and the margin pass draws them including in the strips, so a sprite across the edge is never painted twice side by side — that was what thickened the lines. The CPU, RAM, mapper and clock of this copy are discarded on return; speculative movement is not applied to the match. The main drawing remains original, and from the HUD only the rows staged by the split are rebuilt by the bar layer. Plants that skip a drawing frame hold the previous pieces for one frame, as in the game.

Early activation and the farther culling change the match's sequence compared with native mode. The determinism check compares recompiled and interpreted execution under the same wide policy; equality between widescreen on and off is no longer expected.

## Limits

- The capacity remains five main slots: full levels may wait for a free slot. Special objects, projectiles and controllers keep their original triggers.
- Maps, titles, bonuses, Toad Houses, vertical rooms, transitions and locations without enough width keep the original framing. The end-of-level card and the player's run are not transitions: they stay on the wide frame until `Level_ExitToMap`.
- All levels, bosses, water/raster effects and enemy combinations still need validation. The mode is experimental.
- The window is always an integer multiple of the canvas (426 × `scale` by 240 × `scale`), so each image pixel covers exactly the same number of screen pixels. In fullscreen the game uses the largest 16:9 mode whose height is a multiple of 240 — 1280 × 720 on a 1080p panel — and hands that surface to the compositor, which enlarges it to fill the screen.
- There is no longer a way to turn expansion off: scenes without enough width keep the original framing, and the rest use the wide spawn and culling policy.

## Tests

```powershell
cd build
.\smb3-wide-verify.exe "..\game.nes"
```

The script enters 1-1 and scrolls the camera. It checks the HUD, native centre and bars pixel by pixel — including that each row staged by the split continues the bar's own background rather than the scenery's, that every row below the anchor reproduces the PPU frame exactly, that no row above the split is touched, that a second presentation of the same frame does not rebuild the layer, and that a frame without a split IRQ gets no layer. It injects garbage into the edges to check the repair; verifies active enemies outside the old area and retained beyond the old culling limit; and rejects duplicate spawn indices. A Piranha on the first pipe must be active and have sprites in the wide margin, while the same sequence in native mode does not yet activate it.

It also compares 1,800 recompiled/interpreted states with enlarged enemies, verifies deterministic continuation after save/load, and confirms that isolated sprite generation does not alter the real machine. These tests cover the script, not every level.

Technical research: [Southbird — reference disassembly](https://github.com/captainsouthbird/smb3), especially PRG000 and PRG005; [SM3Mix](http://www.sonicepoch.com/sm3mix/). The disassembly was consulted for research; data and graphics come from the user's local copy. On original artefacts: [NESdev Mirroring](https://www.nesdev.org/wiki/Mirroring) and [Attribute tables](https://www.nesdev.org/wiki/PPU_attribute_tables).

## v1.11 — wide level end

The end-of-level card and the player's run no longer switch off the expansion. The window follows the camera to the map edge, as the wide composer does: the native frame slides to the side instead of returning to the centre, and the HUD stays centred. The native 256 × 240 path does not change; the wide hooks only exist with wide mode on. The exit to the map (`Level_ExitToMap`), pipe transitions, maps, bonuses, vertical rooms and the end of the game remain at 4:3. The wide enemy generator stops creating records during the card and the run: the game no longer advances the native spawner there, and a margin scan was creating late Goombas on top of those still on screen.

## v1.10 — whole-pixel presentation

The v1.10 menu had lost the WIDESCREEN entry and `config.ini` had lost `widescreen` and `aspect`; the toggle is back on the menu's VIDEO tab, persisted as `widescreen` in `config.ini`, with the native 256 × 240 path unchanged underneath.

The window is now sized as 426 × `scale` by 240 × `scale`, and both presenters (SDL2 and Vulkan) draw the image at the largest integer scale that fits the window, instead of stretching by a fractional factor. Before, 1920 ÷ 426 = 4.507: fractional fitting gave four screen pixels for some columns and five for their neighbours, and each sprite repeated the defect internally — the 4/5 pattern the user's capture showed.

In fullscreen the frontend requests the largest 16:9 mode whose height is a multiple of 240. On a 1080p panel that is 1280 × 720 (exactly 3×), not 1920 × 1080 (4.5×): the image is drawn at whole pixels on that surface and the compositor enlarges it 1.5× to fill the screen. The fullscreen request is now deferred until the window is mapped, because Wayland silently discards a request made before that.

Measured on the panel's real 1920 × 1080 output: each 16-pixel cell of the end-game board comes out with 70 solid pixels plus two of transition, identical across the full width of the screen, with no column of 4 next to one of 5. A fractional fit would show a 4/5 pattern, because it fits 426 columns into 1920 unevenly; this project's sharpness comes from the default window of 852 × 480 (exactly 2×).

The limit is arithmetic: 1080 ÷ 240 = 4.5, so no integer scale fills a 1080p panel. Filling the screen and keeping hard edges are incompatible goals at this resolution; the choice was to keep the drawing at an integer 3× and leave the final enlargement to the compositor. On 1440p and 4K panels the height is already a multiple of 240 (6× and 9×), so fullscreen is whole-pixel edge to edge.

## v1.9 — full `hud_build()` port and inter-frame cache

The layer implementation is complete. Drawing starts at the split row, not the bar's first row: `hud_build()` walks `hud_top..239`, and the rows between the split and the bar's first row are drawn from the capture itself, with the scroll the playfield had on that row (the layer's `own = y < s.first || i >= anchor_i`). The floor under the player remains the level's drawing down to the last scanline of the level.

This costs what it costs when the game switches from the card to the level in the middle of a row: the capture is one state per whole row, and `hud_build()` has no way to reproduce the half that fell on the other side of the write either. Measured on the 1,800-frame script: 2 frames (f=1564 and f=1565, split=192, first bar row=193) lose 1 row and 12/11 pixels each, 23 in total; outside that block, **zero** rows and **zero** pixels differ at any point in the image. The layer now draws exactly the rows the hardware draws, from the same sources.

The inter-frame cache (`hud_matches`/`hud_snapshot`, the `HudLayer::Source`) was ported along with it. The layer stores what it used to build the image — split, first row, anchor, scroll and the anchor's OAM, the frame's nametables, and per row the scroll, `$2000`/`$2001`, palette and CHR banks — and rebuilds only when one of those values changes. The built image is copied to the frame in contiguous blocks of rows with rendering on, like `hud_blit()`. Mirroring is not a separate key because the frame's nametables are already the mapped view: a mirroring change that alters the image alters the snapshot. On the same script the layer was built 1,469 times for 1,748 frames with a split (279 reused the previous one), and a second presentation of the same frame never rebuilds — `smb3_hud_builds()` checks this in the test.

The wide image now pastes the layer's rows (like the `framebuf` after the layer's `hud_compose`), not the native frame, so the rows between the split and the bar are the same at 4:3 and 16:9.

## v1.8 — IRQ split and frame nametables

The split row is not derived from the capture: the mapper records the frame's last scanline IRQ (`cyc_frame_split_line`), and the bar starts on the following row, exactly as this project's `hud_top`. A frame without that IRQ gets no layer — title, map, menus and raster effects stay as the machine drew them, with no scan trying to guess where the bar starts. From the split on, the bar's first row and the anchor still come from the per-scanline capture, as in the layer's `hud_bar_first()`/`hud_settled_row()`.

The background of a row is no longer read from the live CIRAM. At the end of the frame (line 241, dot 1), before the game's NMI handler rewrites tiles for the next frame, the runner snapshots the four nametable quadrants as the PPU maps them; the layer draws from that snapshot. Without it, the level's column streaming had already changed the lower tiles when the layer was composed, and the rebuild came out different from the frame.

Each row's colour also applies the greyscale of `$2001` (bit 0), like the layer's `color_mask` and the PPU's `output_pixel`; before, the layer drew in colour when the game had greyscale video. The bar's sprites use row `(y - 1) - OAM Y`, the same as the hardware: a scanline's patterns were fetched during the previous one.

In v1.8, the only part of `hud.cpp` not yet ported was the inter-frame cache (`hud_matches`/`hud_snapshot`): the layer was rebuilt on every presentation, with the same pixels the cache would return. v1.9 completes the port.

Checked on the 1,800-frame script: 1,749 with a bar, 1,139 staged rows, and **zero** differences between the layer and the machine outside the staged block — including in 1,507 bar rows with sprites. In v1.8, rows above the bar were not redrawn: the game switches from the card to the level in the middle of one of them (the scroll write falls inside the row) and the capture is one state per whole row; redrawing them as `hud_build()` does costs 23 pixels in 2 of the 1,749 frames. v1.9 redraws them the same way, accepting that cost so the layer is complete.

## v1.7 — HUD layer

The HUD became its own layer, replacing the earlier rule that looked for a single-colour line above the bar. That rule got the outdoor level right (black bar) and got the start-of-level card wrong, where the bar is the card's panel: the HUD background is not always black, and the staged row has to be drawn from the bar itself. The layer also only touches rows whose capture has rendering on, like the layer's `hud_blit`; a row the game left blank remains the machine frame's.

The break that separates level and bar does not come from a constant: it comes from the frame's own per-scanline capture. The first point after the split that does not continue the previous row (vertical scroll, fine X or pattern-table selection) is the first HUD row; the first row below it that runs to the end of the image is the content anchor. Drawing uses the anchor's scroll, and reproduces the bar's background and sprites exactly: redrawn from the capture, the settled HUD rows match the pixels the game drew 100%. The staged rows use the same drawing, but at the anchor's scroll.

## v1.6 — object window and load hook

The hook that announces a level load (`smb3_wide_level_load`) was emitted outside the condition that protected it: the code generator placed it before *every* instruction, so the short-exit window (`load_grace`) was re-armed every frame and stayed permanently open. Because of that, the margin records were created repeatedly, including inside the visible image — an old record at x=224 appeared on top of Mario. The hook now sits inside the block for the sound-reset address (a single call, at `$A242`), and the window is also armed on entering a game scene, a signal visible in RAM in both engines: the interpreted mode (F2) does not run the generated hook and diverged from the recompiled execution. Record creation now requires the records to be outside the native image even in the short exit, so nothing appears suddenly.

## v1.5 — transitions and object edges

In v1.5, expansion was disabled during the prize, cards, special raster and level exit. The centre uses exactly the native frame and all outer margins are black, without reusing the previous map. The renderer covers the eight existing object slots without increasing the activation capacity of the five main ones. The relative pieces need no alignment with the frame's OAM, and their coverage also includes the 12-pixel strips rebuilt at the old edges.

The scenes_verify test uses a destination swap and positioning only inside the test process to traverse two tall levels, compare the reconstructed background with the real PPU background, and simulate the bonus, raster, exit and — since v1.11 — the wide card/level-end indicators. These interventions are not part of the application.

Framing uses the scroll the PPU actually applied, reconstructed in map space. The gameplay camera in RAM can be a few pixels ahead of the displayed frame; using it directly created seams between the rebuilt margins and the native centre, especially in tall levels with horizontal offset.

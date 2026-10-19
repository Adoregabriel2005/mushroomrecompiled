# Cartridge expansion review packet

The cartridge expansion and commercial-reference fixes landed through PRs
#25-38 after owner gameplay acceptance on 2026-09-26. The final implementation
merge is `b9cd901b7e9bd1ad5a01c4ec04c731314ade9b75`; its complete source tree
matches the tested candidate `4f7e06fd08f81bd745f48027da89eca7e230c535`.
The original per-mapper commits are preserved. The table records the historical
review boundaries; these PRs are already merged and must not be merged again.

| PR | Area | Main review focus |
|---|---|---|
| [25](https://github.com/mstan/nesrecomp/pull/25) | Initial 15 mapper IDs | Bank handoffs, register apertures, bus conflicts and PPU behavior |
| [26](https://github.com/mstan/nesrecomp/pull/26) | NES 2.0 and board variants | Header acceptance/rejection, RAM geometry, four-screen memory, bus conflicts |
| [27](https://github.com/mstan/nesrecomp/pull/27) | MMC2/MMC4 | Latches on PPU read-pulse release, including held $2007 reads |
| [28](https://github.com/mstan/nesrecomp/pull/28) | Mapper 31 and 4 KiB PRG | Physical bank identity, instruction handoffs and seed units |
| [29](https://github.com/mstan/nesrecomp/pull/29) | VRC2/VRC3/VRC4 | Address wiring, RAM permissions, IRQ clock/acknowledgement and DMA |
| [30](https://github.com/mstan/nesrecomp/pull/30) | VRC6/VRC7 | CHR/nametable pins, IRQs, rendered expansion audio and reset |
| [31](https://github.com/mstan/nesrecomp/pull/31) | Bandai/Datach | EEPROM protocol/busy timing, persistence, banked SRAM and barcode input |
| [32](https://github.com/mstan/nesrecomp/pull/32) | MMC5 | PRG/RAM tags, fetch-driven CHR/ExRAM, split/IRQ behavior, PCM and saves |
| [33](https://github.com/mstan/nesrecomp/pull/33) | MMC1 boards/155 | PPU-driven outer PRG, physical RAM wiring, MMC1A A17 and save compatibility |
| [34](https://github.com/mstan/nesrecomp/pull/34) | Mapper 40 | Low ROM window, 4,096-cycle IRQ and low-address execution fallback |
| [35](https://github.com/mstan/nesrecomp/pull/35) | Namco 206/108 | Legacy battery RAM and the original physical-chip diagnostic |
| [36](https://github.com/mstan/nesrecomp/pull/36) | Reference host | Frame completion after HLT without changing CPU/bus timing |
| [37](https://github.com/mstan/nesrecomp/pull/37) | Existing projects | Cycle-backend opt-in, generation dependencies, ROM identity and inventory checks |
| [38](https://github.com/mstan/nesrecomp/pull/38) | MMC5 reference timing | Sample PPU /RD at the CPU edge before register accesses advance master ticks |

[MAPPERS.md](MAPPERS.md) contains board-specific sources and limits.
[PROJECTS.md](PROJECTS.md) explains the build integration and the boundary for
legacy game enhancements. [NAMCO108_PROBE.md](NAMCO108_PROBE.md) explains the
physical measurement needed before modeling the false-write erratum.

## Reproduce the software checks

Build the compiler and standalone runtime in Release, then run the CPU,
cartridge and header contracts:

```sh
cmake -S recompiler -B build/compiler -DCMAKE_BUILD_TYPE=Release
cmake --build build/compiler --config Release --parallel 4
cmake -S runner/cyc -B build/cyc -DCMAKE_BUILD_TYPE=Release
cmake --build build/cyc --config Release --parallel 4
ctest --test-dir build/cyc -C Release --output-on-failure
python tools/cyc/test_cyc_runtime.py \
  --recompiler build/compiler/NESRecomp \
  --interp build/cyc/cyc_interp --oracle build/cyc/cyc_oracle \
  --out build/cart-regressions --build-timeout 1800
python tools/cyc/test_cyc_saves.py --fixtures build/cart-regressions \
  --interp build/cyc/cyc_interp --oracle build/cyc/cyc_oracle
```

For Visual Studio builds, use `Release/<name>.exe` beneath each build directory
and pass `--generator "Visual Studio 17 2022"` to Python build harnesses.
Windows automation should launch tools with hidden processes, as in AGENTS.md.
Use `--case-prefix` to select a mapper family while iterating. Keep separate
output directories for Windows and Linux builds.

Additional checks live in `tools/cyc`: `test_cyc_mapper_headers.py`,
`test_cyc_seed_units.py`, `test_cyc_expansion_audio.py`, `test_cyc_barcode.py`,
`test_cyc_mmc5_public.py`, `test_cyc_project.py` and `test_cyc_owner_roms.py`.
Famicom Disk System media (`common/nes_fds.h`) has `cyc_fds_disk_test` in
`ctest` (fixtures from `fds_fixtures.py`) and `test_fds_owner_images.py`, which
parses a local disk library read-only and compares every side stream with the
Mesen loader transliterations; `NESRecomp --fds-info` prints an image's table.
Their `--help` lists required fixture directories or external inputs. The MMC5
public test compares six published AWJ pictures; only a consistent palette
conversion and the published overscan crop are allowed. Expansion PCM tests
check actual rendered output separately from register-state comparisons.

Retain generated fixtures, stdout, per-frame hashes, save files and picture/
audio artifacts. The ordinary harness checks expected results and compares
native, embedded interpreter, standalone interpreter and TriCNES at all four
CPU/PPU alignments. Oracle agreement is a differential check, not an independent
proof of every newly implemented mapper rule.

## Integration evidence and limits

The final acceptance campaign exercised thirteen locally supplied USA-compatible
commercial/unlicensed titles serially. All 1500-frame native/reference routes
matched at alignment 0, within a cumulative emulator wall-time budget below
30 seconds per title. The six requested core titles reached gameplay: Dr. Mario,
Yoshi, Yoshi's Cookie, Super Mario Bros., The Legend of Zelda and Super Mario
Bros. 2. Their inspected routes showed responsive input without observed title
corruption, crash or obvious softlock.

The additional titles were Castlevania III, Gemfire, Dragon Warrior III, Deadly
Towers, Captain Comic, Double Strike and Fire Hawk. Gemfire and Dragon Warrior
III's bounded reference checks cover menus; separate acceptance boots reached
the strategy map and starting room. Those extended boots are not additional
reference-verified gameplay. All thirteen standalone builds were presented for
hands-on acceptance, and the owner reported that all looked good.

The commercial MMC5 comparisons exposed a reference-adapter timing bug, fixed
in #38. Its 51 MMC5 fixtures pass four execution modes at all four alignments
(816 executions), including a write fixture proven to fail before the fix.
Fresh CPU, cartridge and header CTest targets passed on the isolated integration
tree. Each PR merge preserved its validated head's complete source tree.
Private evidence and the resumable handoff are retained in
`mstan/nesrecomp-core-playtest`; ROMs and generated binaries remain local.

The following earlier broad checks supplement that acceptance campaign.

The available owner inventory covers 25 distinct images on mappers 0/1/2/4/40/66.
All 400 executions matched over the 600-frame route. This includes two locally
built Metroid variants that halt in both the pre-expansion runtime and this
stack; their agreement does not establish playable games. Light-gun gameplay
and game completion are not covered by the controller route. The mapper40
SMB2J conversion uses interpreter execution for its $6000 ROM code (18.2%
native on this route). Zelda also executes code outside the native ROM window.

The existing SMB3 project's 3,000-frame route matches all modes/alignments and
the PR25 observable baseline, with 100.0% displayed native execution.
AccuracyCoin also matches that baseline and completes every test. Scores are
144/143/141/143 out of 144 by alignment; the known failures are retained, not
reported as universal 144/144 hardware accuracy.

New cartridge families without owner game images still need real-game and
physical-board validation. The reference models and original fixtures cover
their contracts, while analog expansion-audio matching, EEPROM/barcode timing
against physical devices and the Namco108 false-write trigger need external
evidence. This work is tracked in central Beads issue `beads-2dw.1.38`.
FCNS mapper1 submapper6 and mapper40's different submapper1 multicart remain
explicitly unsupported. Keep those limits visible during per-PR merge review.

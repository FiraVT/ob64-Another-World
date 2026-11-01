# Design: the port's save and load menu (profiles)

Status: proposal, 2026-10-30. Nothing built. Developer request: the port's own
save/load menu, reached from the port's menu, with profiles named after the
player's character, more than the game's three records, details of where each
save was made, and the game's save-screen artwork kept.

## 1. Goals

1. Save and load from the port's menu without the game's save screens.
2. Profiles: one per campaign, named after the leader (the name entered at New
   Game), each with any number of saves.
3. Each save shows where and when it was made: a picture of the screen, the
   mission or map location, the in-game day, the Chaos Frame, Goth, the leader's
   portrait, army size and the real date.
4. The game's own save rules decide when a save is possible, and a choice of
   save style for players who want a challenge (section 5).
5. Saves stay real game saves: every file is a record the game itself wrote,
   so a save works in the game's own Load screen and in other emulators.

## 2. What the game has (docs/notes/save-items.md)

| record | size | where | written by |
|---|---|---|---|
| slot 0, slot 1 | `0x1850` | battery `0x10 + n*0x1850` | the world map's Save |
| slot 15, suspend | `0x4AE8` | battery `0x30B0` | Suspend on the mission field and in battle |

Each record: two checksums seeded with its device offset (`func_8007541C`),
`QuestOG3`, a "slot in use" stamp, a 26-byte summary (`func_8016AF90`: counts,
`0x801936CA/CB`, four bytes of `0x80196A48`, the leader's name in ASCII), then
the packed field blob (`func_80074494` packs, `func_800742F4` unpacks). The
writer is `func_8007485C`, the reader `func_800749C0`. The runtime keeps the
battery image in memory and writes `<config>/saves/<game id>.bin`
(`app/features/save_backup.cpp`).

## 3. How it works

The battery keeps its three records and the game keeps writing them. The port
adds a library of records around it.

- **Save:** the port asks the game to save through the game's own routine
  (world map: a normal slot; mission field and battle: the suspend record),
  then copies the record the game just wrote into the profile as a new file.
  The battery slot it used is restored to what it held, so the player's own
  three records are untouched.
- **Load:** the port writes the chosen record into the battery slot of its
  kind (fixing nothing: the record carries its own checksums for that slot's
  offset, so a record is loaded into the slot type it came from), then starts
  the game's own load of that slot, from the title or from the map.
- **Profile:** the leader's name from the record's summary
  (`summary +0x08`, 17 bytes). Every New Game starts a new profile, even with
  a name already used: its first save takes the name, or "<name> 2",
  "<name> 3" when that folder exists (developer report 2026-10-31: every new
  game named Magnus saved into one folder). The port marks a new campaign when
  the name-entry form (scene `0x07`, reached only by a New Game) runs, and
  loading a profile save continues that profile. A game loaded through the
  game's own Load Game (scene `0x12`) is not traced to a profile and saves under
  the leader's name. `app/features/save_profiles.cpp` `choose_profile`. A profile can
  be renamed in the menu.
- Files: `<config>/saves/profiles/<name>/<stamp>.ob64save` (the raw record) and
  `<stamp>.json` (the details below) and `<stamp>.png` (the screen at saving).

## 4. The menu

A SAVE / LOAD tab in the Esc menu (and a LOAD entry on the title through the
same screen):

- The left column lists profiles, each with the leader's portrait and name and
  its latest save's date.
- The right column lists the profile's saves, newest first, as cards: the
  screen picture, "Mission 3: Tenne Plains" or "World map: Zenobia", "Day 4,
  14:00", Chaos Frame and Goth, units in the army, the leader's level, the
  real date and time, and the kind (Save or Suspend).
- Actions: Save here (new card), Overwrite, Load, Delete (asks), Rename,
  Export to the battery slot (so the game's own Load screen sees it).
- The game's save-screen artwork is the menu's background, if it is one of
  the njpeg backgrounds the port already decodes (to be confirmed from the
  developer's captures of the save screen, 2026-10-30).

## 5. Save styles (a setting)

From the developer's notes on freeform saving: saving anywhere removes the cost
of a mistake, so the choice is the player's.

| style | when you can save |
|---|---|
| FREE (default) | wherever the game allows a save or a suspend: the world map, the mission field, battles |
| CLASSIC | only where the game's own menus allow it today |
| ONE LIFE | one save per profile, overwritten on every save and on mission start; loading does not restore a lost unit |

Saving in a cutscene or a menu is not offered: those states are not records the
game can write, and a snapshot of the whole machine breaks with every build of
the port (the debug checkpoints are valid only for the binary that wrote them).

## 6. What has to be found

| # | question | how |
|---|---|---|
| S1 | The call that saves a normal slot from the world map, and its arguments | **found 2026-10-30** (run 9, `OGRE_COVER` censuses 16-17 around saving both slots): `func_8007485C(slot)` packs the record (`func_80074494`, the summary `func_8016AF90`) and `func_8007578C(slot, buffer)` writes it to the battery at `0x10 + slot*0x1850` through `func_8008A0F0`; `0x8007578C` ran once per slot |
| S2 | The call that writes the suspend record in the field and in battle | **found** (censuses 18-19 around a battle suspend): `func_8007485C(15)` calls `func_8007513C`, which builds the `0x4AE8` record (the enemy state through `func_8016D110`) and writes it with `func_80075858` |
| S3b | Loading without the title | **found 2026-10-30**: the Load Game screen's confirm `func_801A9B3C` loads the slot (`func_800749C0`), stores a normal slot's number in `0x80187051` and sets the pending scene `0x800C4C26` to `0x8005` (the world map), or, when story byte `0x801936CA` is `0x35`, sets `*(0x800C4BBC)+4` = 5, calls `0x801723B4`, `0x8017225C(1)`, `0x80170814` and sets the pending scene to 2; a suspend sets the pending scene to the control value 3 and `*(0x800C4BBC)+4` = 5 after `func_80074834` marks it used. The SAVES tab's LOAD does the same on the game thread (the record into its slot, the loader, the slot's previous bytes back), skips `func_80074834` so a profile suspend stays loadable, and closes the menu. Not played |
| S3 | The call that loads a slot, from the title and from the map | partly found (run 9, censuses 22-23 around loading a battle suspend): `func_80074F9C(slot)` is the loader (`0x80074834` calls it, then `func_80074BF0`) |
| S3a | Why loading a suspend deletes it | **found**: slot 14 is a third record, 16 bytes, a control word (`func_8016CDF4` builds it: bit 31 = byte `0x80187050`, "a suspend exists"; bits 27-30 = byte `0x80187051` & 0xF, its kind). Reading it (`func_8016CD90`) clears both bytes, and after the suspend loads the game saves slot 14 again (`func_8007485C(14)`) with the flag at 0. The suspend record itself (slot 15) is left as it was; the title just stops offering it. Keeping it: write the flag back after the load (a hook on `func_8016CDF4`, or setting `0x80187050` before the slot-14 save), or put the profile's copy back. In FREE save style the suspend is kept; CLASSIC and ONE LIFE keep the game's single use |
| S4 | Where the location, mission and day come from for the details | known: clock `0x800E7D34`, Chaos Frame `0x801936C9`, Goth `0x80196A8C`; the mission index and the map location are to be read |
| S5 | The save screen's artwork | the save-screen captures |

## 7. Phases

| phase | content | done when |
|---|---|---|
| 1 | S1-S3; profile files; Save here and Load from the Esc menu with a plain list **Built 2026-10-30, not played** (`app/features/save_profiles.cpp`): the MAIN tab's PROFILE SAVES section (in the game only) has SAVE HERE and the newest 12 profile saves. SAVE HERE runs when the menu closes, on the game thread (the frame hook `ogre_organize_frame`): on the world map `func_8007485C(1)`, on the mission field and in battle `func_8007485C(15)`; the record is read back from the runtime's battery buffer (`save_read` through a buffer from the game's allocator `0x80070F30`), written to `<config>/saves/profiles/<leader>/<stamp>-s<slot>.rec` with a details line (.txt: kind, day and hour, Chaos Frame, Goth), and the slot's previous bytes are put back (`save_write_ptr`). Loading a normal save writes it into its slot; the title's Load Game plays it. Loading a suspend waits on the suspend flag's kind value (S3a). Needs RmlUi (the frame hook is organize_edit's). Played 2026-10-30: three world-map saves landed in `profiles/Magnus` (6224-byte records with `QuestOG3` and the name); the list did not show them because the rows were built before the saves. Moved the same day to its own SAVES tab (in the game only): SAVE HERE, then one section per profile with every save (date and details), LOAD and DELETE (asks twice) as choices; the tab rebuilds when the menu opens and when a save finishes. Moved back to MAIN the same day at the developer's request (the SAVES tab is hidden): under the SAVES section, SAVE HERE and the profile sections follow BACK UP SAVE. Loading from the mission field or a battle crashed on the world map; the field's own exits call `0x801723B4`, `0x80172144` and `0x80170814` before they store the pending scene (`0x801D6404`, `0x801D3A4C`), so a load from scene 3 or `0x0D` ran them first. That still crashed (run-field13): the world map called `0x8019F5FC` and `0x801A365C`, function entries of record 3's module (unit A), after its own module (unit M) had replaced that RAM (`streamed function stub called`). No field capture holds either address as a word. A load made outside the title, the Load Game screen and the world map sets the pending scene to `0x8004` (the game's own move to the title, `0x80177EA8`), waits 60 frames on the title and then loads as the title's Load Game does. The three exit calls are not made; they only clear the scene-script state (`0x80197B38`/`0x80197B3C`, `0x80196F80`). Run-field15 still called the mission's functions on the way to the title: they are callbacks of the object pool (`0x800E82C8`, 6 entries of `0xA8`, run from the index list `0x800C4C10` with the count `0x800C49D0`; the mission's module stores them at `func_ovlA_8019EE70` and `func_ovlA_801A07F0`; the frame update runs an entry while its flags have `0x2000` and `0x8000`). Before the move to the title, entries whose callbacks point at `0x80197B90` or above are removed from the run list and zeroed (run-field17 found the mission entry with flags `E200` and callbacks `801A07F0 801A1354 801A1B98 801A365C`; `func_80076AE8` runs a listed entry even with flags clear when its `+0x18` callback is set). The developer also reported that repeated switching broke after a few loads. The pending-scene word drives a scene stack in `func_80075BC0` (`0x800C4BBC` the current 8-byte entry, `0x800AF020` the depth; `0x8000` replaces, no `0x8000` pushes, `0xFFFE` pops), and each suspend load (pending 3) pushed an entry. The first title visit records its stack entry, and every load resets the stack to it. Run-field18 (both pool entries freed, no stub calls, stack reset 2 to 1) then crashed at the world map's start: a write through guest address `-1` in `func_80071950`, the heap free-list unlink under the allocator `0x80070F30` (fault `exe+0x236DFC`, resolved with the linker map `build-dist/ogrebattle64.map`, now written by `/MAP` in `CMAKE_EXE_LINKER_FLAGS`). The field's exits free the scene-script buffers `0x80197B38`/`0x80197B3C` through `0x80170814`, and the scene change resets the heap (`0x80070F14`); left set, a later free of them breaks the free list. The leave step now calls `0x80170814` first. Run-field19: field → title → world map loaded, and the next load, world map → world map (the old direct path), broke the heap inside `func_80070F30`. The game never re-enters the world map from itself, so every load not made from the title (or the Load Game screen) now goes through the title the same way. Not played | a save made in the menu loads back into the same mission |
| 2 | The cards: picture, location, day, Chaos Frame, Goth, portrait | the list says where every save was made |
| 3 | Profiles by leader name, rename, delete, export to the battery | two campaigns keep separate lists |
| 4 | Save styles (section 5) and the title's Load through this menu | ONE LIFE cannot be reloaded past a loss |
| 5 | The artwork | the menu looks like the game's save screen |


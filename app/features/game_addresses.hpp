// The game addresses (US revision 1 RAM) that more than one feature reads.
// An address used by one feature only stays in that feature's file.
//
// Each entry says what the address holds and where it was first written down:
//   lfarroco    lfarroco's notes or code at the fork (history-archive:docs/history/DECISIONS.md, PLAN.md,
//               app/src, the session handoffs);
//   this project docs/anotherworld/ or app/features/;
//   LordlyCaliber  rempred's editor (packaging/CREDITS.txt).
// The addresses are facts about the game; the source is kept as a credit.
#pragma once

#include <cstdint>

namespace ogre::addr {

// --- scenes ------------------------------------------------------------------
// u16: the scene the dispatcher is running (0x03 the mission field, 0x05 the
// world map, 0x06 Organize, 0x0D a battle). lfarroco (bank_overlays.cpp).
constexpr uint32_t kActiveScene = 0x800E810Eu;
// u16: the pending scene word, 0x8000 | id, plus control values. lfarroco
// (docs/symbols.md g_pending_scene).
constexpr uint32_t kPendingScene = 0x800C4C26u;

// --- the army ------------------------------------------------------------------
// Player characters, 0x38 bytes each, index 1..99. lfarroco (PLAN.md).
constexpr uint32_t kCharacters = 0x80193BE0u;
constexpr uint32_t kCharacterSize = 0x38u;
// Player unit records, 0x19 bytes each, units 0..29: +0x02..+0x06 member
// references, +0x07..+0x0B their squares. lfarroco (PLAN.md).
constexpr uint32_t kUnits = 0x80197210u;
constexpr uint32_t kUnitSize = 0x19u;
// u32: the army's goth. lfarroco (session 123 handoff).
constexpr uint32_t kGoth = 0x80196A8Cu;
// u8 0..100: the Chaos Frame. lfarroco (app/src/game.hpp).
constexpr uint32_t kChaosFrame = 0x801936C9u;
// Enemy characters, 0x34 bytes each. lfarroco (docs/notes/save-items.md).
constexpr uint32_t kEnemyCharacters = 0x80195580u;

// --- tables ----------------------------------------------------------------------
// The four content tables sit back to back in segment B data (ROM = vram -
// 0x8016AF80 + 0x40E80); each count is where the next table starts. The
// content database (content_db.hpp) loads them; docs/anotherworld/
// design-content-database.md section 2 has the readers.
//
// Class table, 0x48 bytes per class, +0x00 name pointer (ROM 0x5DB34). This
// project (docs/anotherworld/design-class-rebalance.md). func_8016DE3C reads
// +0x45 (key) and +0x04 (size) with index*0x48 from 0x80187C34; 165 classes
// end at the attack table (class 165's first word is attack 0's name pointer).
constexpr uint32_t kClasses = 0x80187C34u;
constexpr uint32_t kClassSize = 0x48u;
constexpr uint16_t kClassCount = 165;
// Item table, 0x20 bytes per item (ROM 0x6232C): +4 category, +5 element.
// lfarroco (session 122 handoff). The accessors func_8016F5D0 (+0x00 name),
// func_8016F5E8 (+0x04), func_8016F64C (+0x08 u16 price) and the rest up to
// func_8016F8A4 index it with id << 5. 278 items: the 0x116 bound
// (`slti ..., 0x116` at 0x8016B720 and others), and item 278 is consumable 0.
constexpr uint32_t kItems = 0x8018C42Cu;
constexpr uint32_t kItemSize = 0x20u;
constexpr uint16_t kItemCount = 278;
// Consumable table, 0x0C bytes per item (ROM 0x645EC): +0 name, +4 category.
// This project (design-organize-screen.md, Carry Item names). The accessors
// func_8016F500 (+0x00), func_8016F520 (+0x04), func_8016F540 (+0x05),
// func_8016F560 (+0x08) and func_8016F580 (+0x09) index it with id*3*4. 45
// records (0 "None" .. 44 "Pedra of Bane"); 0x8018E908 holds text.
constexpr uint32_t kConsumables = 0x8018E6ECu;
constexpr uint32_t kConsumableSize = 0x0Cu;
constexpr uint16_t kConsumableCount = 45;
// Attack table, 0x10 bytes per attack: +0 name, +4 kind, +5 element, +6 target.
// This project. 159 records by their name pointers (0 .. 158 "Fatal Dance");
// 0x8018B48C is not a record. No reader's bound is traced yet.
constexpr uint32_t kAttacks = 0x8018AA9Cu;
constexpr uint32_t kAttackSize = 0x10u;
constexpr uint16_t kAttackCount = 159;
// Battle reward table, 8 bytes per row: u16 class, then three u16 outcomes
// (equipment id | 0x8000, or a consumable id). It is data of bank N (record 7,
// ROM 0x101D00, the mission field module) at ROM 0x142278, and shares its RAM
// with banks C (rec10b), S and X (rec10c). The reader at 0x801ADD3C walks the
// rows until the class word or 0, then reads slot rand() % 3 at +2. 35 rows,
// then the terminating row (class 0, no outcomes) that a class without a row
// uses. LordlyCaliber (creature_rewards.h, revision 0 ROM 0x142258); this
// project (design-war-director.md 4.10, the loot upgrade).
// Appearance table, 6 bytes per class id (ROM 0x64C00): the halfwords at +0
// and +2 pick the portrait (v >> 3) and palette (v & 7) (portraits.cpp,
// func_8016FD0C). 36 reader sites (design-new-classes.md section 2).
// LordlyCaliber calls it "appearances" (revision 0 0x8018ECE0).
constexpr uint32_t kAppearances = 0x8018ED00u;
constexpr uint32_t kAppearanceSize = 0x06u;
constexpr uint16_t kAppearanceCount = 165;
constexpr uint32_t kRewards = 0x801EDB38u;
constexpr uint32_t kRewardSize = 0x08u;
constexpr uint16_t kRewardCount = 35;
constexpr uint32_t kRewardsRom = 0x142278u;
constexpr uint32_t kBankNRom = 0x101D00u;

// The content arena: guest RAM the content database owns for data that does
// not fit the game's tables (new names today; relocated tables in R2). The game
// never allocates above 0x80400000: both heap set-ups pass a fixed end of
// 0x80400000 (0x80071EEC: 0x800E9C20..0x80400000; 0x801784B4:
// 0x80243DD0..0x80400000), whatever osGetMemSize answers (8 MiB in this port).
// The port's own users of the upper 4 MiB are the OGRE_SYNTH_FRAME probe
// (0x80500000, 0x80700000) and RT64's words at 0x807FFC08. This project.
constexpr uint32_t kContentArena = 0x80600000u;
constexpr uint32_t kContentArenaSize = 0x100000u;
// In the arena: the moved class table (room for 255 records; it keeps the
// low 16 bits of 0x80187C34, so only each reader's lui changes,
// tools/relocate_tables.py), then the new names.
constexpr uint32_t kMovedClasses = 0x80607C34u;
constexpr uint32_t kClassTableShift = (kMovedClasses - kClasses) >> 16;  // 0x48
constexpr uint32_t kNamesArena = 0x80610000u;
// The names end here; the class key table copy is at 0x806EF3D8 and the class
// line table copy at 0x806FF2EC (class_change.cpp, tools/relocate_tables.py).
constexpr uint32_t kNamesArenaEnd = 0x806E0000u;
// The moved appearance table (tools/relocate_tables.py), same shift.
constexpr uint32_t kMovedAppearances = 0x8060ED00u;

// --- the mission ---------------------------------------------------------------
// u8: the mission (scenario) index. lfarroco (PLAN.md).
constexpr uint32_t kMissionIndex = 0x8018F4A1u;
// Map object pointer per unit slot, 4 bytes each. lfarroco (session 79).
constexpr uint32_t kMapObjects = 0x801F0CD0u;
// u32: the number of locations (towns, temples, strongholds). This project
// (docs/anotherworld/design-war-director.md 6a).
constexpr uint32_t kLocationCount = 0x801F1090u;
// u16 per location, 0x24 bytes apart: 0x04 enemy-held, 0x02 player-held, bit
// 0x80 adds 40 to the liberation score. This project; the liberation rule is
// LordlyCaliber's.
constexpr uint32_t kLocationState = 0x801951ECu;
constexpr uint32_t kLocationSize = 0x24u;
// u32: hours since the campaign began, 16.16. This project (captures of
// 2026-10-30).
constexpr uint32_t kGameClock = 0x800E7D34u;
// u8: the map id; the map's data is the word at kMapDataTable + 4 * id. This
// project.
constexpr uint32_t kMapId = 0x800E7AC5u;
constexpr uint32_t kMapDataTable = 0x800E7A90u;

// --- screens and modules ---------------------------------------------------------
// u32: the Organize screen's state, set by its enter func_ovlS_801D4BB0. lfarroco
// (docs/notes/item-equipped.md).
constexpr uint32_t kOrganizeState = 0x80196B18u;
// The arena the unit builder (bank R, ROM 0x195430) and the transition module
// stream into. lfarroco (bank_overlays.cpp).
constexpr uint32_t kBuilderArena = 0x80214FA0u;

}  // namespace ogre::addr


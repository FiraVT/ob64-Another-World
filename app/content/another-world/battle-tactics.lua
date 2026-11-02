-- Another World: battle tactics (docs/anotherworld/design-another-world-pack.md,
-- docs/anotherworld/design-class-rebalance.md section 4.1).

local FIGHTERS = 20

-- Faerie heals. Throw a Kiss (attack 0x17) powers up allies. When an ally on
-- the Faerie's side, itself included, is below 70% HP, the Kiss becomes
-- Healing (0x57), and the game's own heal targeting picks the most hurt ally.
-- Faerie heals once from the front and middle rows, so with its MEN it heals
-- about 60% of a Cleric's two back-row casts.
local THROW_A_KISS = 0x17
local HEALING = 0x57
local HURT_PERCENT = 70

on("battle_attack", function(fighter, attack)
  if attack ~= THROW_A_KISS or not options.get("faerie_heals") then
    return
  end
  local me = battle.fighter(fighter)
  if me == nil then
    return
  end
  for n = 0, FIGHTERS - 1 do
    local f = battle.fighter(n)
    if f and not f.out and f.side == me.side and f.hp_max > 0 and f.hp * 100 < f.hp_max * HURT_PERCENT then
      return HEALING
    end
  end
end)

-- Centurion command (developer, 2026-10-31). Every Centurion (class 34 male,
-- 35 female, 94 Europea) that can still fight adds one attack to each Soldier
-- (class 1) on its side, and the bonuses stack.
local SOLDIER = 1
local CENTURION = {[34] = true, [35] = true, [94] = true}

on("battle_attack_count", function(fighter, count)
  if not options.get("centurion_command") then
    return
  end
  local me = battle.fighter(fighter)
  if me == nil or me.class ~= SOLDIER then
    return
  end
  local bonus = 0
  for n = 0, FIGHTERS - 1 do
    local f = battle.fighter(n)
    if f and CENTURION[f.class] and f.ready and f.side == me.side then
      bonus = bonus + 1
    end
  end
  if bonus > 0 then
    return count + bonus
  end
end)

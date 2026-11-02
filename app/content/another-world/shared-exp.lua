-- Another World: shared battle EXP (docs/anotherworld/design-another-world-pack.md).
--
-- The EXP a battle gives the player's unit is pooled and split across its
-- members with the game's own end-of-battle weighting (func_ovlJ_801D5DFC),
-- max(1, unit average level - level + 8)^2, so lower levels get more.
--
-- EXP is counted as level * 100 + EXP. A member keeps every level the battle
-- gave it, so the game's level-up and stat growth are never undone, and a
-- member's EXP stays inside its current level (0..99). Within those limits the
-- split keeps the unit's total gain equal to what the battle gave: a member
-- that levelled up past its share keeps the level, and the others share only
-- what is left (review AW-03, 2026-11-01; the first version gave out more EXP
-- than the battle did).
--
-- The result is written for a short window after the battle, so a late write
-- from the game's result screen cannot put the old value back.

local SETTLE_FRAMES = 30 -- after the battle scene ends, before the split
local HOLD_FRAMES = 120  -- frames the split values are held

local members = {}
local settle = 0
local hold = 0

on("battle_start", function(unit)
  members = {}
  settle = 0
  hold = 0
  -- Option SHARED EXP: off leaves the game's own EXP alone.
  if unit < 0 or not options.get("shared_exp") then
    return
  end
  for _, ref in ipairs(game.unit(unit)) do
    if ref >= 1 and ref <= 99 then
      local c = game.character(ref)
      if c.class ~= 0 then
        members[#members + 1] = {ref = ref, start = c.level * 100 + c.exp}
      end
    end
  end
end)

on("battle_end", function(unit)
  if #members >= 2 then
    settle = SETTLE_FRAMES
  end
end)

-- Splits `total` across `list` in proportion to each entry's weight, with each
-- share inside [lo, hi]. The sum of the lo is at most total and the sum of the
-- hi at least total, so the shares always add up to total. Entries whose share
-- falls outside their limits are fixed at the limit on the side that is
-- further out, and the rest is split again among the others.
local function split(total, list)
  local fixed = {}
  local share = {}
  for _ = 1, #list do
    local rest, weights = total, 0
    for i, m in ipairs(list) do
      if fixed[i] then
        rest = rest - share[i]
      else
        weights = weights + m.weight
      end
    end
    local under, over = 0, 0
    for i, m in ipairs(list) do
      if not fixed[i] then
        share[i] = weights > 0 and rest * m.weight / weights or 0
        if share[i] < m.lo then
          under = under + m.lo - share[i]
        elseif share[i] > m.hi then
          over = over + share[i] - m.hi
        end
      end
    end
    if under == 0 and over == 0 then
      break
    end
    for i, m in ipairs(list) do
      if not fixed[i] then
        if under >= over and share[i] < m.lo then
          share[i], fixed[i] = m.lo, true
        elseif under < over and share[i] > m.hi then
          share[i], fixed[i] = m.hi, true
        end
      end
    end
  end
  -- Whole numbers: round down, then hand out what is left by the largest
  -- fraction.
  local gains, left = {}, total
  for i, m in ipairs(list) do
    gains[i] = math.max(m.lo, math.min(m.hi, math.floor(share[i])))
    left = left - gains[i]
  end
  while left > 0 do
    local best, fraction = nil, -1
    for i, m in ipairs(list) do
      local f = share[i] - gains[i]
      if gains[i] < m.hi and f > fraction then
        best, fraction = i, f
      end
    end
    if best == nil then
      break
    end
    gains[best] = gains[best] + 1
    share[best] = share[best] - 1
    left = left - 1
  end
  return gains, left
end

local function share_exp()
  local total, level_sum = 0, 0
  for _, m in ipairs(members) do
    local c = game.character(m.ref)
    m.level = c.level
    m.exp = c.exp
    m.gain = math.max(0, c.level * 100 + c.exp - m.start)
    total = total + m.gain
    level_sum = level_sum + c.level
  end
  if total == 0 then
    log("battle end:", #members, "members, no EXP gained")
    return false
  end
  local average = level_sum / #members
  for _, m in ipairs(members) do
    local d = math.max(1, average - m.level + 8)
    m.weight = d * d
    -- The member's level stays: EXP 0 of it at least, EXP 99 at most, and
    -- never below what it had before the battle.
    m.lo = math.max(0, m.level * 100 - m.start)
    m.hi = m.level * 100 + 99 - m.start
  end
  local gains, left = split(total, members)
  if left ~= 0 then
    log("battle end:", total, "EXP could not be split; left as the game gave it")
    return false
  end
  log("battle end:", total, "EXP across", #members, "members")
  for i, m in ipairs(members) do
    m.target = m.start + gains[i] - m.level * 100
    log("  #" .. m.ref, "L" .. m.level .. ": gained", m.gain, "share", gains[i], "EXP", m.exp, "->", m.target)
  end
  return true
end

on("frame", function()
  if settle > 0 then
    settle = settle - 1
    if settle == 0 and share_exp() then
      hold = HOLD_FRAMES
    end
  end
  if hold > 0 then
    hold = hold - 1
    for _, m in ipairs(members) do
      if m.target and game.character(m.ref).exp ~= m.target then
        game.set_exp(m.ref, m.target)
      end
    end
  end
end)

-- Another World: class rules (docs/anotherworld/design-another-world-pack.md).

-- Class changes do not check alignment (developer, 2026-10-29). The game skips
-- an alignment limit of 255, so every limit becomes 255. The stats, the class
-- line, the basic equipment and the quest classes stay the game's own.
-- Option CLASS ALIGNMENT: VANILLA keeps the game's limits.
on("class_alignment", function(limit)
  if options.get("class_alignment") == "IGNORED" then
    return 255
  end
end)

-- A large class takes one place in a unit, so large members can stand side by
-- side and a leader with four dragons fits. The class keeps its size 2, so
-- battle and the field still draw its large figure. Applies to the Organize
-- rules and to the enemy units the war director builds. Option LARGE UNITS:
-- VANILLA keeps two places.
on("unit_size", function(size)
  if size == 2 and options.get("large_units") == "ONE SQUARE" then
    return 1
  end
end)

-- The Loot Tweaks example. Copy the whole loot-tweaks folder into user-content/
-- to try it. docs/anotherworld/design-lua.md lists the API.

-- Hawkman (reward row 0; content-dump/rewards.json lists the rows) drops a
-- Short Sword in its first slot. An equipment drop is the item id + 0x8000.
content.set("rewards", 0, "drop_0", 0x8000 + 1)
log("Hawkman's first drop is now", content.get("items", 1, "name"))

-- A Heal Leaf (consumable 1) found or bought becomes a Heal Seed (2).
on("item_granted", function(item)
  if item == 1 then
    return 2
  end
end)

-- Every item the army receives is written to the log (game.log in the settings
-- folder), as an item word: the id, plus 0x8000 for equipment.
on("item_granted", function(item)
  log("item granted:", item)
end)

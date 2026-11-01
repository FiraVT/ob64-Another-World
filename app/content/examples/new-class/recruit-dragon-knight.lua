-- Recruits a Dragon Knight at level 5 on the first mission field of a launch.
-- The game's own character builder makes it (stats for the level, the class's
-- gear, a generated name); it joins the army without a unit, and Organize
-- lists it with the others.
local done = false
on("frame", function()
  if done or game.scene() ~= 3 then -- 3: the mission field
    return
  end
  done = true
  if game.recruit("new-class-example.dragon-knight", 5) then
    log("a Dragon Knight joins the army on this mission field")
  end
end)

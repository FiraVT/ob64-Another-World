-- Until a story event recruits a Dragon Knight, this turns the first Fighter
-- (class 2) among the army's characters into one, once, on the mission field
-- or the world map (not while the Organize screen has the characters open).
-- Hired soldiers are not characters (units keep them as soldier slots), so a
-- named character is changed.
local done = false
on("frame", function()
  local scene = game.scene()
  if done or (scene ~= 3 and scene ~= 5) then -- 3: the mission field, 5: the world map
    return
  end
  done = true
  for character = 1, 99 do
    if game.class_of(character) == 2 then
      game.set_class(character, "new-class-example.dragon-knight")
      log("character", character, "is now a", content.get("classes", "new-class-example.dragon-knight", "name"))
      return
    end
  end
  log("no Fighter among the army's characters; nothing changed")
end)

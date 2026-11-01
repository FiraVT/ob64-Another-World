# New class template: the Dragon Knight

This mod adds one class, the Dragon Knight, and is meant to be copied. Copy the
folder into `user-content/`, rename a few things, and you have a class of your
own.

## The files

| file | what it does |
|---|---|
| `mod.json` | the mod's id, name and version |
| `dragon-knight.json` | the class: its stats, gear, the Change Class rules and its art files |
| `recruit-dragon-knight.lua` | a script that recruits one Dragon Knight at level 5 on the first mission field of a launch |
| `give-class.lua` | an example of `game.set_class`; turns the first Fighter into a Dragon Knight once per launch. Not needed with Change Class; delete it, or keep it to test quickly |
| `art/` | the class's images; `art/README.md` says how to make them |

## Make your own class from it

1. Copy this folder into `user-content/` and rename the folder.
2. In `mod.json`, set your own `id` (lowercase letters, digits, `-`), `name`
   and `version`.
3. In the class file, rename the key `new-class-example.dragon-knight` to
   `<your mod id>.<your class>`. The key is how the game remembers the class in
   saves (`saves/class-ids.json` gives it an id from 172 up), so keep it once
   you play with it.
4. Set `copy_of` to the class it starts as (any id 0..164;
   `content-dump/classes.json` lists them). Everything you do not set is a copy
   of that class: stats, attacks, resistances, size.
5. Change `name` and any fields (the field names are the ones in
   `content-dump/classes.json`).
6. Change Class: `change_class.family_of` puts the class in that class's line
   (a Fighter can change into it when `family_of` is in the Fighter's line,
   like 34 here). The requirements are minimum STR, VIT, INT, MEN, AGI, DEX and
   an alignment range; 255 means none. Leave the block out for a class that
   Change Class does not offer, like Dio's.
7. Gear: `equipment_0`..`equipment_3` are the basic equipment (item ids in
   `content-dump/items.json`). A character changing class must own or buy it.
8. Art: make your images (`art/README.md`) and name them in the `art` block.
9. In the scripts, change the key in `game.recruit(...)` / `game.set_class(...)`.

The game reloads the class file while it runs; art is read at start.

## Share it

Put your mod folder in a GitHub repository and add the topic `ob64-mod` to the
repository (the gear icon next to "About"). Every repository with that topic is
listed at https://github.com/topics/ob64-mod, which is where players look for mods.

## Status of the art

The port builds the art into the game when it starts: the portrait, the
formation figures (ordinary-size classes), and the battle sprite's colours.
Restart the game to see a change to an image.

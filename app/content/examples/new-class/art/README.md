# The class's images

| file | size | where the game shows it |
|---|---|---|
| `portrait.png` | 40 x 48 | menus and dialogue |
| `figure-front.png` | 16 x 24 | the formation figure as enemies show it |
| `figure-back.png` | 16 x 24 | the formation figure as the army shows it |
| `battle/descriptor<d>-bank<b>.png` | 16 x 16 | the battle sprite's colours |

The class file names each one in its `art` block. Leave a file out and the
class keeps its `copy_of` class's art there.

## Start from the game's own art

The images are not shipped, because they come from the game. Make them from
your own ROM with:

```
python tools/export_class_art.py <rom> 34 user-content/new-class/art --hue 140 --only 185-265
```

`34` is the class to start from (the Centurion). `--hue 140 --only 185-265`
turns the blues by 140 degrees, so the armour goes crimson and the skin and
gold stay. Leave both out for the class's own colours, then paint over them.

## Rules for your own images

- Keep the sizes above.
- Portrait: opaque, at most 80 colours.
- Figures: a clear (fully transparent) background. The figures share one
  palette per atlas with every other class, so each colour is moved to the
  nearest colour that palette has; strong new colours come out close, not
  exact. Only classes drawn with ordinary-size figures take new figures so
  far (not large or special ones).
- Battle sprite: the battle art is colour by number. Each swatch is one palette
  bank of the class's battle sprite, pixel `i` (left to right, top to bottom)
  is colour `i`; change a pixel's colour and every part of the sprite drawn
  with that number changes. `d` counts the class's battle descriptors (two for
  the Centurion: 9 and 5 banks), `b` the bank. New battle shapes come later.
- Pixel art: no smoothing or scaling. The game shows the pixels as they are.
- Art is read when the game starts; restart to see a change.

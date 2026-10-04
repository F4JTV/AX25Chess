# The piece font

The board draws its pieces from `AX25ChessPieces.ttf`, compiled into the
program. A system font cannot be relied upon for this: the chess symbols are
missing from many, and the outline of a piece is drawn from the glyph's path,
which has no font fallback - a font without them would give invisible pieces,
not boxes. Shipping the font makes the board identical on Linux, Windows and
Android.

It is a subset of DejaVu Sans (Bitstream Vera licence, `LICENSE.txt`), made
with fontTools from the copy Ubuntu installs:

```python
from fontTools import subset
from fontTools.ttLib import TTFont
opts = subset.Options(); opts.name_IDs = ['*']; opts.name_languages = ['*']
font = TTFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
s = subset.Subsetter(opts); s.populate(unicodes=list(range(0x2654, 0x2660)) + [0x20]); s.subset(font)
for rec in font["name"].names:
    if rec.nameID in (1, 4, 16): rec.string = "AX25Chess Pieces"
    elif rec.nameID == 6: rec.string = "AX25ChessPieces"
font.save("AX25ChessPieces.ttf")
```

The licence asks for a renamed family when the font is modified; a subset
counts, hence "AX25Chess Pieces".

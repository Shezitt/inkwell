#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generates data/fonts/InkwellBlank.ttf, a font in which every character is
blank and takes no space.

The editor draws hidden Markdown syntax in it: the characters stay in the
text, but nothing of them shows, not even when they are selected.

Usage: python3 -m pip install fonttools
       python3 tools/make-blank-font.py data/fonts/InkwellBlank.ttf
"""

import sys

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib.tables._c_m_a_p import CmapSubtable

FAMILY = "Inkwell Blank"


def build(path):
    builder = FontBuilder(unitsPerEm=1000, isTTF=True)
    glyphs = [".notdef", "blank"]
    builder.setupGlyphOrder(glyphs)
    builder.setupCharacterMap({})
    empty = TTGlyphPen(None).glyph()
    builder.setupGlyf({name: empty for name in glyphs})
    # No advance, and no height, so that lines are as high as without it.
    builder.setupHorizontalMetrics({name: (0, 0) for name in glyphs})
    builder.setupHorizontalHeader(ascent=0, descent=0)
    builder.setupOS2(
        sTypoAscender=0, sTypoDescender=0, sTypoLineGap=0, usWinAscent=0, usWinDescent=0
    )
    builder.setupNameTable(
        {
            "familyName": FAMILY,
            "styleName": "Regular",
            "licenseDescription": "MIT License",
        }
    )
    builder.setupPost()

    # A format 13 table maps ranges of code points to a single glyph: here
    # all of Unicode to the blank one, in a few bytes. A font that lacks a
    # character would make Qt draw it from another font.
    table = CmapSubtable.newSubtable(13)
    table.platformID, table.platEncID, table.language = 3, 10, 0
    table.cmap = dict.fromkeys(range(0x110000), "blank")
    builder.font["cmap"].tables = [table]

    # The same input gives the same file, so it can be checked in.
    builder.font.recalcTimestamp = False
    builder.font["head"].created = builder.font["head"].modified = 0
    builder.font.save(path)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    build(sys.argv[1])

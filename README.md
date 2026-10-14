# Auto Object Skins

A [Geode](https://geode-sdk.org) mod for Geometry Dash (2.2081, Geode v5) that scans a level, finds blocks with decoration attached that repeat throughout it, and saves each combo as a custom object.

Use it from the editor (button next to pause) or from a level's page. Online levels must be free to copy.

## How detection works
1. The level string is parsed into objects.
2. Solid blocks (and optionally slopes) are the "bases"; decoration objects are attached to the base whose box (plus the attach distance) contains their center, picking the closest.
3. Each base + its deco gets a signature (object IDs, relative offsets, rotation, flips, scale, colors, z layers).
4. Signatures that repeat at least *Minimum Repeats* times become block sets, most common first.

## Building
`geode build`, or push to GitHub and download the **Build Output** artifact from Actions.

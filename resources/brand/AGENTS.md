<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-10-27 | Updated: 2026-10-27 -->

# resources/brand

## Purpose

The committed brand sources: the app icon and the macOS disk-image window. Not staged into the
app bundle (that is `resources/island/`); the platform icon files are generated from these.

## Key Files

| File | Description |
|------|-------------|
| `app-icon.svg` | Light-appearance app icon: 1024 canvas, 824 squircle body at (100,100), soft shadow, no raster images. The one source for the `.icns`, `.ico`, Linux PNGs and Linux SVG |
| `app-icon-dark.svg` | Dark-appearance variant; kept beside the light one, not yet used by a build (macOS 26 draws its dark glass rendition from `Island.icon`) |
| `Island.icon/` | Icon Composer document for the macOS 26 Liquid Glass icon: `icon.json` (gradient fill, one group, `squares: shared`) and `Assets/{sea,sand,meadow,forest}.svg` layers split from `app-icon.svg` by `brand_icons.py glass`. `src/main/CMakeLists.txt` compiles it with `actool` into `Assets.car` (`CFBundleIconName` = `Island`); with no Xcode 26 `actool` the build warns and ships `island.icns` only |
| `dmg-background.svg` | The 660x400 pt dmg window picture (the window content is 660x428 pt: the picture plus Finder's 28 pt path bar; the bottom colour fills the rest when the path bar is hidden): label plates at the two icon slots, an arrow, "Drag Island to Applications" |
| `dmg-background.png`, `dmg-background@2x.png` | Rendered from the SVG at 660x400 (72 dpi) and 1320x800 (144 dpi); `installers.py` joins them into a multi-resolution TIFF |

## For AI Agents

### Working In This Directory

- The current artwork is "Paper Relief" (concept `paper` of `~/hsse/agents/reports/brand/contour/`,
  owner's choice 2026-10-27; spec `docs/superpowers/specs/2026-10-27-island-app-icons-design.md`).
  `install-concept` also regenerates `Island.icon`. To inspect the glass renditions run
  `"/Applications/Xcode.app/Contents/Applications/Icon Composer.app/Contents/Executables/ictool" resources/brand/Island.icon --export-image --output-file out.png --platform macOS --rendition Default|Dark|TintedLight --width 512 --height 512 --scale 1`. One command installs a concept and regenerates every derived file:
  `python3 scripts/brand_icons.py install-concept <light.svg> <dark.svg> [<dmg-background.svg>]`
  (macOS, Pillow; `pip install -r scripts/requirements-brand.txt`).
- Never edit the derived files by hand: `src/main/macos/island.icns`, `src/main/windows/island.ico`,
  `src/main/linux/{island-browser.svg,icons/*.png}` and the two dmg PNGs come from
  `scripts/brand_icons.py generate`.
- The dmg picture is fixed, but Finder draws icon labels black (light theme) or white (dark theme)
  over it, so every label sits on a plate of luminance ~0.19 (`tests/package/test_brand.py`
  checks 4:1 against both). Keep the icon centres in step with `DMG_LAYOUT` in
  `scripts/installers.py` (app 165,140; Applications 495,140), and keep text above y=345 because
  Finder's global path bar covers the last ~30 pt.

### Testing Requirements

```bash
python3 -m pytest tests/package/test_brand.py
```

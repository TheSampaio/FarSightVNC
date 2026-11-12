# Lucide icons (vendored)

Icons of [Lucide](https://lucide.dev), embedded in the viewer as `RCDATA`
resources (`tvnviewer/tvnviewer.rc`) and drawn with Direct2D
(`gui/d2d/SvgIcons.cpp`).

- Package: `lucide-static`, version **0.468.0** (pinned).
- Source: `https://unpkg.com/lucide-static@0.468.0/icons/<name>.svg` and
  `https://unpkg.com/lucide-static@0.468.0/LICENSE`, downloaded on 2026-11-12.
- Files are unmodified. At run time the viewer replaces `stroke="currentColor"`
  with the theme color before parsing.
- License: ISC, with portions from Feather (MIT); see `LICENSE`, which must
  ship with the notice in the About section and in `LEIAME.txt`/`README.txt`.

| File | Icon |
|------|------|
| `icons/plus.svg` | `plus` (new connection button of the title bar) |

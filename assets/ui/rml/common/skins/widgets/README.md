# WORR RmlUi Widget Skin Assets

This folder contains renderer-safe SVG widget surfaces used by the shared
RmlUi themes. The files intentionally stay within the current OpenGL bridge SVG
subset: rect, line, polyline, polygon, path, circle, flat fill/stroke colors,
and opacity.

Use these as control decorators in RCSS, for example:

```
decorator: image(../skins/widgets/button-normal.svg);
```

Keep new files small, square or simple horizontal slabs where possible, and add
matching hover/focus/active/disabled states when introducing a new widget kind.
Avoid decorative placeholder text or duplicate arrow glyphs inside field/select
slabs; RmlUi draws real text and native child controls on top of these skins.

# Editor look: FlappedEar Telemetry's design language

Owner request of 4 October 2026 (KAN-187): the editor looks like FlappedEar
Telemetry. The source of the design is `lib/ui/theme.dart` (`FetTheme.dark()`,
`FetColors`) in `FlappedEar/Telemetry`; the plan is the Confluence page
"UI overhaul: Telemetry design language" in space FEO.

A dark track-day dashboard of flat charcoal panels, FlappedEar amber as the one
accent, blue for the reference lap, 3 px corners, no outlines on panels, Sora
for text with tabular digits, JetBrains Mono for times.

## Where it lives

- `native/qml/Theme.js` holds every colour, font, corner and type size the
  editor uses. QML imports it with `import "Theme.js" as Theme` (from
  `qml/widgets/` it would be `"../Theme.js"`). It is a JavaScript library, not a
  QML singleton, because the tests and the user-guide capture load QML files by
  path from `native/qml`, where a singleton would need a hand-written `qmldir`.
- `native/resources/fonts/` holds Sora (400 to 700) and JetBrains Mono (500 to
  700) with their SIL Open Font License 1.1 texts, copied from Telemetry's
  `assets/fonts/`. `FlappedEar::registerBundledFonts()`
  (`native/src/app/BundledFonts.cpp`) registers them at startup in every mode,
  so a widget font typed in the editor previews and exports alike. The test
  binary bundles the same files.
- The shared controls `FeButton`, `FeCheckBox`, `FeComboBox`, `FeSlider`,
  `FeSpinBox`, `FeTextField`, `SectionTitle` and `ColorField` use only theme
  names. New screens use these controls and theme names, never their own hex
  values.

## Token mapping

| Role | Theme name | Value | Replaced in the editor |
| --- | --- | --- | --- |
| Window background | `surface` | `#111214` | `#070b10` |
| Deepest background | `surfaceContainerLowest` | `#0b0c0e` | `#0b1119`, `#0d141d` |
| Low panel, disabled control | `surfaceContainerLow` | `#16171a` | `#101720`, `#111a24` |
| Panel, card, dialog | `surfaceContainer` | `#1b1c1f` | `#151d27` (panels) |
| Control background | `surfaceContainerHigh` | `#24262a` | `#151d27`, `#0d131b` (controls) |
| Control hover, pressed | `surfaceContainerHighest` | `#2a2c31` | `#1c2632`, `#202b38` |
| Divider | `outlineVariant` | `#2e3035` | `#202a36`, `#2a3645` |
| Strong border, disabled text | `outline` | `#5d6068` | `#596575`, `#657386` |
| Text | `onSurface` | `#f2f2f2` | `#f2f6fb`, `#dce4ee`, `#e8edf4` |
| Secondary text | `onSurfaceVariant` | `#a3a6ad` | `#8b98a8`, `#91a0b2`, `#778596` |
| Accent: selection, focus, primary action | `primary` (amber) | `#fcb203` | green `#55e6a5` |
| Text on the accent | `onPrimary` | `#1a1200` | `#07140f` |
| Warning | `warning` (= `error`) | `#ff6b5c` | `#ffb84d`, `#ffc66d`, `#d6a457` (owner decision, 4 October 2026: amber is the accent, so warnings are red) |
| Soft highlight (selected row, badge) | `primaryContainer`, `onPrimaryContainer` | `#3a2c00`, `#ffe08a` | green tints `#16261f`, `#10251d` |
| Reference lap, links | `secondary` | `#3d8bff` | `#4da3ff` |
| Ready, success, time gained | `tertiary` / `gain` | `#53bc94` | green status marks |
| Error, danger, time lost | `error` / `loss`, `errorContainer` | `#ff6b5c`, `#4d1610` | `#ff8090`, `#21171d` |
| Fastest of the day | `dayBest` | `#b37bff` | (new) |

Shape: `radius` 3 px on controls, `dialogRadius` 6 px on dialogs; panels have
no outline. A focused control shows a 2 px amber ring. Sliders use a light
track and thumb on a dark track, as in Telemetry.

Type: `sans` (Sora) with `numbers` (`tnum`, tabular digits) for interface text,
`mono` (JetBrains Mono) for timecodes and lap times. The editor keeps its
desktop density (12 px body text, 32 and 38 px control heights); Telemetry's
44 to 48 px touch targets are a mobile rule.

## Scope

Restyled (KAN-187): the shared controls and `FeLabel`; `Main.qml` (bars,
preview surround, timeline and cue marks, status bar, welcome screen, export
dialog, recovery and other dialogs, menus); the `WidgetOverlay` selection
outline and resize handle; `InspectorPanel.qml`; `VideoChaptersDialog`;
`StartupError`. `Main.qml` sets the full Basic-style palette from the theme, so
standard dialogs, menus and dialog buttons follow it. Editor text uses
`FeLabel` (or sets `font.family: Theme.sans`); the window's own `font` is left
alone, because it would also reach the overlay widgets' labels in the preview
but not in export.

Widget colour defaults shown in the inspector (`colorValue: ... || "#..."` and
the tyre `fallback` colours) are overlay content and stay as they are.

Not restyled: the Lap Analysis window and its panels and dialogs, and
`BatchImportDialog`, which leave this app under KAN-166: the window and its panels were
deleted by step 4, and `BatchImportDialog` went with day import in step 6. The overlay widgets drawn into the video keep their own
colours: they are the user's design and live in templates.

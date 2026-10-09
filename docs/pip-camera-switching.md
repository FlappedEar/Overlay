# Picture-in-picture and camera switching (KAN-245)

Owner request (Arek, 9 October 2026): "Customizable and optional picture-in-picture after syncing
independent videos, ability to change cameras in flight - just like real broadcasts do".
It extends KAN-131 (additional videos with manual sync). Steps: KAN-246 (this plan), KAN-247
(model), KAN-248 (composition rules), KAN-249 (export), KAN-250 (preview), KAN-251 (editor),
KAN-252 (owner check with real footage).

## Defaults chosen by Claude

Arek has not reviewed these; each is a revision away.

- Picture-in-picture (PIP) is a per-project switch, changeable at any time. When the `pip` key is
  absent, a project with additional videos keeps the KAN-131 look (PIP on), so saved projects do
  not change.
- PIP is customizable: corner, size, margin, border (width, colour) and which cameras appear.
- Camera switching is a list of cut points on the **main video's timeline** (the editor's playhead
  time). From a cut's time on, its camera is the *program camera*: it fills the frame. Any synced
  video can be the program camera, not only the main one.
- The program camera before the first cut is the main video.
- Default transition: hard cut. Optional crossfade of 0.1 to 2 s, set per project.
- PIP shows the cameras that are not the program camera (or the cameras the user picked).
- A camera with no footage at its cut time cannot be shown: the main video shows instead.
- Cuts apply in picture-in-picture mode; side by side keeps its fixed layout.
- Audio stays the main video's only.

## Project format

`pip` and `program` are optional objects inside `videoLayout` (so they travel with the layout through
run switching, Save As and recovery). Unknown keys are kept. Keys equal to the defaults are not written.

```json
"videoLayout": {
  "mode": "pictureInPicture",
  "pip": {
    "enabled": true,
    "corner": "topRight",
    "size": 0.28,
    "margin": 0.03,
    "borderWidth": 0,
    "borderColor": "#FFFFFF",
    "cameras": ["helmet"]
  },
  "program": {
    "transition": "cut",
    "crossfadeSeconds": 0.5,
    "cuts": [ { "time": 12.0, "camera": "helmet" }, { "time": 30.5, "camera": "main" } ]
  }
}
```

- `corner`: `topRight` (default), `topLeft`, `bottomRight`, `bottomLeft`.
- `size`: share of the frame's width and height one PIP box may take, 0.10 to 0.50 (default 0.28).
- `margin`: share of the shorter frame side, 0 to 0.10 (default 0.03).
- `borderWidth`: pixels at 1080p scale, 0 to 12 (default 0); `borderColor`: `#RRGGBB`.
- `cameras`: camera ids shown in PIP; absent means every camera that is not the program camera.
- `cuts`: at most 200, `time` finite and at least 0, `camera` is `main` or an additional video id,
  strictly increasing times. A cut naming a removed video is kept and ignored (the main video shows).
- `transition`: `cut` (default) or `crossfade`; `crossfadeSeconds` 0.1 to 2 (default 0.5).

## Rules shared by preview and export (`VideoComposition`)

1. `programAt(cuts, time)` is the camera of the last cut at or before `time`; `main` before the first.
2. Segments: consecutive cuts become `[start, end)` intervals per camera.
3. PIP boxes are laid out from `pip` (corner, size, margin) and stacked away from the corner's
   edge; every position and size stays even.
4. Crossfade: a segment fades in over `crossfadeSeconds` from its start, over the previous camera.

## Export (Stage B)

Every camera is already retimed onto the output timeline. The base layer is the main video. For each
program segment of a camera other than the base, a full-frame layer of that camera is overlaid with
`enable='between(t,start,end)'`; with crossfade the layer gets an alpha fade-in and the previous
segment stays enabled for the fade. PIP boxes (with border) are overlaid on top of the program
layers, and the telemetry overlay stays on top of everything. Time in the expressions is export time
(main video time minus the export start).

## Preview

`AdditionalVideosPreview.qml` follows the same segment and layout results: the program camera fills
the item, PIP boxes show the other cameras, and a crossfade is an opacity ramp.

## Editor

DATA tab, Additional videos panel: a Picture-in-picture group (switch, corner, size, margin, border,
camera checkboxes) and a Camera switching group (list of cuts with time and camera, **Cut here** at
the playhead, delete, transition and crossfade length). Edits mark the project dirty (the editor has no undo stack for documents).

## Not planned

Per-cut transitions other than crossfade, picture-in-picture animation, audio switching, a
multi-track timeline view.

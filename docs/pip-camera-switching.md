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
- A camera is on air only where it has footage; the main video shows for the rest of its stretch.
- The id `main` is reserved for the main video; an additional video cannot have it.
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
  strictly increasing times. A file that names a camera that is not there is valid: the
  main video shows for it. Removing a video in the editor removes its cuts and box choices.
- `transition`: `cut` (default) or `crossfade`; `crossfadeSeconds` 0.1 to 2 (default 0.5).

## Rules shared by preview and export (`VideoComposition`)

1. `onAirAt(program, ids, time)` is the camera of the last cut at or before `time`; the main video
   before the first cut, or when a cut names a camera that is not there.
2. `segments` joins consecutive cuts into half-open `[start, end)` stretches per camera.
3. `plan` turns the layout into drawing-ordered layers with windows on the main video's timeline:
   full-frame layers for a camera other than the main video while it is on air (letterboxed in
   black), then the PIP boxes. The main video is the base under everything.
4. Boxes have one fixed size. The cameras off air close up from the corner into slots, so a camera
   takes the slot its place among them gives. If the stack would not fit the frame, the box height
   is reduced so it does. Every position and size stays even.
5. Crossfade: each switch gets its own layer that fades in over the previous one, which stays under
   it for the fade. A switch back to the main video is a main-video layer that fades in the same
   way, so crossfades are symmetric.

## Export (Stage B)

Every camera is retimed once and split into one copy per layer. A layer is scaled, bordered or
letterboxed, optionally given an alpha fade-in, and overlaid on the previous result with
`enable='gte(t,a)*lt(t,b)+...'` (half-open, so a hard cut lands on an exact frame; FFmpeg's
`between` is inclusive at both ends and is not used). `t` is export time: main video time minus the
export's start. The telemetry overlay stays on top of everything.

## Preview

`AdditionalVideosPreview.qml` draws `VideoComposition::plan` through the controller's
`previewLayers`: the on-air camera fills the item, boxes show the others, a crossfade is an opacity
ramp. The main video in a box is a copy of its output.

## Editor

Keys 1 to 4 cut to the first to fourth camera of the list at the playhead (not while a text field
is active). DATA tab, Additional videos panel: a Picture-in-picture group (switch, corner, size, margin, border,
camera checkboxes) and a Camera switching group (list of cuts with time and camera, **Cut here** at
the playhead, delete, transition and crossfade length). Edits mark the project dirty (the editor has no undo stack for documents).

## Camera box widget (KAN-254)

Owner request (Arek, 9 October 2026): "I wish the PIP box would be a widget - editable to my needs".
Steps: KAN-256 (widget type and composition rules), KAN-257 (export and preview), KAN-258 (editor
and user guide). Jira umbrella: KAN-254.

### Defaults chosen by Claude

Arek has not reviewed these; each is a revision away.

- A picture-in-picture box becomes a widget of the new type `cameraBox` ("Camera box" in the Add
  widget list). One widget is one box. You place and size it on the canvas like any widget, so the
  corner, size and margin options are no longer the only way.
- Settings: `camera` (`main` or an additional video's id; a new widget takes the first camera no box
  uses yet), `borderWidth` (pixels at 1080p scale, 0 to 12, default 0), `borderColor` (default
  `#FFFFFF`) and `fill` (`fill` crops the picture to the box, the default; `fit` keeps the whole
  picture and fills the rest with black).
- The box's rectangle is the widget's `x`, `y`, `width` and `height` (shares of the frame, as every
  widget), rounded to even pixels. Rotation, scale and opacity are ignored; a widget that is not
  visible draws no box.
- The widget itself draws nothing into the overlay: the video box is drawn under the overlay by the
  same plan in preview and export, so the widget is the box's handle in the editor.
- A new box starts in the frame corner where it overlaps the existing widgets least, 28 % of the frame wide with the main video's aspect (the real-footage check of 9 October 2026 found the top-right default under the template's map and heart-rate widgets). The video box is under the whole overlay, so widgets draw over it.
- Boxes follow camera switching as before: a camera's box shows while it is not on air; while it is
  on air it fills the frame and its box is empty. A camera without a box widget has no box.
- If the project has at least one visible camera box, the boxes replace the `pip` options (corner,
  size, margin, border, cameras) entirely, and the DATA tab says so. With none, the `pip` options
  work as before, so existing projects do not change. `pip.enabled: false` still hides all boxes.
- Box widgets are saved with the other widgets (they travel with templates and My widgets); no new
  project keys. Two boxes for one camera are allowed.

### Built (KAN-256 to KAN-258)

`cameraBox` widget type (descriptor, defaults, `CameraBoxWidget.qml` draws nothing); `cameraBoxes()`
and `plan(..., boxes)`; export crops fill boxes and the preview draws the same crop;
`AppController::addWidget` gives a new box its camera and `VideoComposition::startingBoxArea` its
corner; the inspector has Camera, Picture and Border controls (`cameraBoxControls`); the DATA tab
greys the corner options and says so while a box exists. User guide: Widgets > Camera box.

### Rules (VideoComposition)

`plan` takes the boxes as input (`CameraBox`: camera id, rectangle as shares of the frame,
border, colour, fill) read from the widgets by one function both callers use. Boxes whose camera is
not in the run are skipped. Every box position and size stays even and inside the frame.

### Not planned yet

Rounded corners, box opacity or fades, labels, and showing a box through widget cues.

## Not planned

Per-cut transitions other than crossfade, picture-in-picture animation, audio switching, a
multi-track timeline view.

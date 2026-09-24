# Instance Mover

Editor mode for picking **individual instances** of Instanced Static Mesh (ISM) and Hierarchical Instanced Static Mesh
(HISM) components in the level viewport and moving them with the normal transform gizmo.

Editor-only: one `Editor` module, no runtime code, nothing cooked.

## Using it

Open **Modes → Instance Mover** in the level editor toolbar.

| Action | Input |
|---|---|
| Select an instance | Click it |
| Add to selection | Shift + click, or Shift + marquee |
| Toggle in selection | Ctrl + click |
| Select many | Marquee drag (instance *location* must be inside) |
| Move / rotate / scale | Gizmo — W / E / R, world/local space, grid snapping all work as for actors |
| Duplicate and move | Alt + drag the gizmo |
| Duplicate in place | Ctrl + D, or **Duplicate** in the panel |
| Delete | Delete, or **Delete** in the panel |
| Frame selection | F |
| Clear selection | Esc |

Panel buttons: **Snap to Surface**, **Duplicate**, **Select All in Component**, **Delete**, **Clear Selection**.

Every operation is a single undo step; a whole gizmo drag is one transaction however many frames it spans.

Clicking anything that is not an editable instance falls through to normal actor selection, and selecting an actor
hands the gizmo back to it — so you can stay in the mode while still moving ordinary actors.

## How picking works

1. If the component has **`bHasPerInstanceHitProxies`** on (Details → Rendering → Advanced), the click is pixel-exact.
2. Otherwise a collision trace is tried — ISM traces report the exact instance hit.
3. Otherwise the nearest instance whose **mesh bounds** the click ray crosses wins. This keeps components with
   collision disabled pickable, but on overlapping, irregular meshes (trees, bushes) it can choose a neighbour —
   turn on per-instance hit proxies on that component for exact picking.

## Snap to Surface

Traces straight down from the top of each instance's bounds on `SnapTraceChannel`, ignoring the instance's own
component (a trace cannot exclude a single instance, so instances of one component cannot be stacked on each other
this way). Options: align to surface normal, rest the bounds bottom (vs. the pivot) on the surface, extra offset.

## Not editable here (by design)

- **Foliage** instances — Foliage mode owns them and keeps its own spatial data.
- Components created by a Blueprint **construction script** — the script rebuilds them, so a move would be lost
  the next time it reruns. Components added in the Blueprint's component list are fine.
- Actors with **Lock Location** set, and actors in **locked levels**.
- Procedurally regenerated ISMs (e.g. PCG output) are editable but will be overwritten when regenerated.

## Settings

Per-user (`config=EditorPerProjectUserSettings`, saved under `Saved/Config`) and shown in the mode panel. Defaults
live in the `UInstanceMoverSettings` constructor:

| Setting | Default |
|---|---|
| Pivot Mode | Selection Center |
| Snap Trace Distance | 100000 cm |
| Snap Trace Channel | Visibility |
| Align to Surface Normal | off |
| Rest on Bounds Bottom | on |
| Surface Offset | 0 cm |
| Selection Color / Line Thickness | orange / 1.5 |

## Tests

`Source/InstanceMover/Private/Tests/InstanceMoverTest.cpp` — gizmo delta math, ray picking, surface snap, selection
bookkeeping — registered under `PetwallParade.InstanceMover.*`, so the project's headless automation command
(see the root `CLAUDE.md`) runs them.

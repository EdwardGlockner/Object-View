# ObjectView Scene Format

ObjectView scenes are JSON files for generic 3D visualization. The format avoids domain-specific names so other projects can use it for robotics, CAD, scientific visualization, trajectories, annotations, or ML tools.

## Top Level

```json
{
  "name": "Scene Demo",
  "objects": [],
  "paths": [],
  "vectors": [],
  "frames": [],
  "markers": []
}
```

All arrays are optional, but a scene must contain at least one renderable item.

## Objects

Objects load OBJ assets and place them in the scene.

```json
{
  "id": "rover",
  "asset": "../samples/perseverance-rover/perseverance_rover.obj",
  "transform": {
    "position": [0, 0, 0],
    "rotation": [0, 0, 0],
    "scale": 1
  }
}
```

Asset paths are resolved relative to the scene JSON file. Rotation is in degrees.

### Keyframes (optional)

An object can animate over time by adding a `keyframes` array. Each keyframe is a time (`t`, in seconds) plus a partial transform (`position`, `rotation`, `scale` — any of which can be omitted to fall back to the object's base `transform`).

```json
{
  "id": "rover",
  "asset": "../samples/perseverance-rover/perseverance_rover.obj",
  "transform": { "position": [0, 0, 0], "rotation": [0, 0, 0], "scale": 1 },
  "keyframes": [
    {"t": 0, "position": [0, 0, 0], "rotation": [0, 0, 0]},
    {"t": 4, "position": [2, 0, 1], "rotation": [0, 45, 0]}
  ]
}
```

Keyframes are interpolated linearly. The scene's total duration is the latest `t` across all objects' keyframes; a scene with no keyframes has no timeline and renders as a static snapshot. Objects without `keyframes` stay at their static `transform` regardless of playback time.

### Ghosts (optional)

An object can add translucent duplicates of itself at other transforms via a `ghosts` array. Each ghost is a `transform` plus an `opacity` (0–1, default 0.35).

```json
{
  "id": "rover",
  "asset": "../samples/perseverance-rover/perseverance_rover.obj",
  "transform": { "position": [0, 0, 0], "rotation": [0, 0, 0], "scale": 1 },
  "ghosts": [
    {"transform": {"position": [2, 0, 1], "rotation": [0, 45, 0]}, "opacity": 0.3}
  ]
}
```

Ghosts render independently of keyframes/playback — they're always visible alongside the object's current pose. A downstream project can use them for before/after comparisons, design variants, or predicted future states.

## Paths

Paths draw connected 3D line strips.

```json
{
  "id": "path-a",
  "points": [[0, 0, 0], [1, 0, 0.4], [2, 0, 0.7]],
  "color": [0.44, 0.91, 0.77, 1],
  "width": 3
}
```

## Vectors

Vectors draw directional arrows from an origin.

```json
{
  "id": "velocity",
  "origin": [0, 0.3, 0],
  "direction": [1, 0, 0.2],
  "scale": 1.2,
  "color": [0.94, 0.70, 0.36, 1]
}
```

## Frames

Frames draw XYZ axes at a transform.

```json
{
  "id": "tool-frame",
  "transform": {
    "position": [0, 0, 0],
    "rotation": [0, 45, 0],
    "scale": 1
  },
  "size": 0.8
}
```

## Markers

Markers draw small 3D cross markers.

```json
{
  "id": "selected-point",
  "position": [1, 0.2, 0.5],
  "color": [0.95, 0.25, 0.18, 1],
  "size": 0.12
}
```

## Heatmaps

Heatmaps draw a grid of colored quads on a flat plane, useful for density, value, or error overlays.

```json
{
  "id": "density-plane",
  "origin": [-3, -1.58, -3],
  "cellSize": 1,
  "rows": 6,
  "cols": 6,
  "colorLow": [0.16, 0.32, 0.85, 0.45],
  "colorHigh": [0.95, 0.25, 0.18, 0.75],
  "values": [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, "... rows * cols numbers, row-major"]
}
```

The grid extends from `origin` along +X (`cols`) and +Z (`rows`) in steps of `cellSize`; `origin.y` sets its height. `values` is auto-normalized between its own min and max, then mapped from `colorLow` to `colorHigh`. `values` must contain exactly `rows * cols` numbers, in row-major order.

## Design Boundary

ObjectView should only understand generic rendering concepts. A downstream project can interpret these primitives however it wants:

- A path can be a route, motion trail, simulation trace, or imagined future.
- A vector can be velocity, force, action, gradient, or normal.
- A marker can be a selected state, annotation, sensor location, or sampled point.


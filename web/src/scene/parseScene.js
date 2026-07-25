// Parses ObjectView's neutral scene JSON format (see docs/SCENE_FORMAT.md),
// matching the field defaults used by the native parser in backend/src/Scene.cpp.

function vec3(value, fallback = [0, 0, 0]) {
  return Array.isArray(value) && value.length >= 3 ? [value[0], value[1], value[2]] : fallback;
}

function color(value, fallback = [1, 1, 1, 1]) {
  if (!Array.isArray(value) || value.length < 3) return fallback;
  return [value[0], value[1], value[2], value.length > 3 ? value[3] : 1];
}

function transform(raw) {
  return {
    position: vec3(raw?.position),
    rotation: vec3(raw?.rotation),
    scale: typeof raw?.scale === "number" ? raw.scale : 1,
  };
}

export function parseScene(raw) {
  return {
    name: raw.name ?? "ObjectView scene",
    objects: (raw.objects ?? []).map((item) => ({
      id: item.id ?? "",
      asset: item.asset,
      transform: transform(item.transform),
      keyframes: (item.keyframes ?? [])
        .map((k) => ({ t: typeof k.t === "number" ? k.t : 0, transform: transform(k) }))
        .sort((a, b) => a.t - b.t),
      ghosts: (item.ghosts ?? []).map((g) => ({
        transform: transform(g.transform),
        opacity: typeof g.opacity === "number" ? g.opacity : 0.35,
      })),
    })),
    paths: (raw.paths ?? []).map((item) => ({
      id: item.id ?? "",
      points: (item.points ?? []).map((p) => vec3(p)),
      color: color(item.color, [0.44, 0.91, 0.77, 1]),
      width: typeof item.width === "number" ? item.width : 2,
    })),
    vectors: (raw.vectors ?? []).map((item) => ({
      id: item.id ?? "",
      origin: vec3(item.origin),
      direction: vec3(item.direction),
      scale: typeof item.scale === "number" ? item.scale : 1,
      color: color(item.color, [0.94, 0.7, 0.36, 1]),
    })),
    frames: (raw.frames ?? []).map((item) => ({
      id: item.id ?? "",
      transform: transform(item.transform),
      size: typeof item.size === "number" ? item.size : 0.6,
    })),
    markers: (raw.markers ?? []).map((item) => ({
      id: item.id ?? "",
      position: vec3(item.position),
      color: color(item.color, [0.94, 0.7, 0.36, 1]),
      size: typeof item.size === "number" ? item.size : 0.08,
      label: typeof item.label === "string" ? item.label : null,
      time: typeof item.time === "number" ? item.time : null,
    })),
    heatmaps: (raw.heatmaps ?? []).map((item) => ({
      id: item.id ?? "",
      origin: vec3(item.origin),
      cellSize: typeof item.cellSize === "number" ? item.cellSize : 0.5,
      rows: Number.isInteger(item.rows) ? item.rows : 0,
      cols: Number.isInteger(item.cols) ? item.cols : 0,
      values: Array.isArray(item.values) ? item.values : [],
      colorLow: color(item.colorLow, [0.16, 0.32, 0.85, 0.55]),
      colorHigh: color(item.colorHigh, [0.95, 0.25, 0.18, 0.85]),
    })),
  };
}

export function sceneDuration(scene) {
  let duration = 0;
  for (const object of scene.objects) {
    for (const keyframe of object.keyframes) {
      duration = Math.max(duration, keyframe.t);
    }
  }
  return duration;
}

function lerp(a, b, t) {
  return a + (b - a) * t;
}

function lerpVec(a, b, t) {
  return [lerp(a[0], b[0], t), lerp(a[1], b[1], t), lerp(a[2], b[2], t)];
}

export function interpolatedTransform(object, time) {
  const keyframes = object.keyframes;
  if (keyframes.length === 0) return object.transform;
  if (keyframes.length === 1 || time <= keyframes[0].t) return keyframes[0].transform;
  if (time >= keyframes[keyframes.length - 1].t) return keyframes[keyframes.length - 1].transform;

  for (let i = 0; i < keyframes.length - 1; i += 1) {
    const start = keyframes[i];
    const end = keyframes[i + 1];
    if (time < start.t || time > end.t) continue;

    const span = end.t - start.t;
    const t = span > 1e-9 ? (time - start.t) / span : 0;
    return {
      position: lerpVec(start.transform.position, end.transform.position, t),
      rotation: lerpVec(start.transform.rotation, end.transform.rotation, t),
      scale: lerp(start.transform.scale, end.transform.scale, t),
    };
  }

  return object.transform;
}

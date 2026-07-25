import * as THREE from "three";
import { interpolatedTransform, sceneDuration } from "./parseScene.js";
import { resolveRelative, withExtension } from "./sceneAssets.js";

function toColor(rgba) {
  return new THREE.Color(rgba[0], rgba[1], rgba[2]);
}

function pointVector(p) {
  return new THREE.Vector3(p[0], p[1], p[2]);
}

function applyTransform(object3d, transform) {
  object3d.position.set(...transform.position);
  object3d.rotation.set(
    THREE.MathUtils.degToRad(transform.rotation[0]),
    THREE.MathUtils.degToRad(transform.rotation[1]),
    THREE.MathUtils.degToRad(transform.rotation[2])
  );
  object3d.scale.setScalar(transform.scale);
}

function buildAxes(length, lineWidth) {
  const positions = new Float32Array([
    0, 0, 0, length, 0, 0,
    0, 0, 0, 0, length, 0,
    0, 0, 0, 0, 0, length,
  ]);
  const colors = new Float32Array([
    1, 0.25, 0.18, 1, 0.25, 0.18,
    0.55, 0.9, 0.2, 0.55, 0.9, 0.2,
    0.2, 0.62, 1, 0.2, 0.62, 1,
  ]);
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute("position", new THREE.BufferAttribute(positions, 3));
  geometry.setAttribute("color", new THREE.BufferAttribute(colors, 3));
  const material = new THREE.LineBasicMaterial({ vertexColors: true, linewidth: lineWidth });
  return new THREE.LineSegments(geometry, material);
}

function buildPath(path) {
  const geometry = new THREE.BufferGeometry().setFromPoints(path.points.map(pointVector));
  const material = new THREE.LineBasicMaterial({
    color: toColor(path.color),
    transparent: path.color[3] < 1,
    opacity: path.color[3],
    linewidth: path.width,
  });
  return new THREE.Line(geometry, material);
}

function buildVector(vector) {
  const direction = new THREE.Vector3(...vector.direction);
  const length = direction.length() * vector.scale;
  if (length < 1e-6) return new THREE.Group();
  direction.normalize();
  const headLength = Math.min(0.18 * Math.max(vector.scale, 0.4), length * 0.5);
  const arrow = new THREE.ArrowHelper(direction, pointVector(vector.origin), length, toColor(vector.color), headLength, headLength * 0.6);
  return arrow;
}

function buildFrame(frame) {
  const group = buildAxes(Math.max(frame.size, 0.05), 2);
  applyTransform(group, frame.transform);
  return group;
}

function buildMarker(marker) {
  const geometry = new THREE.OctahedronGeometry(Math.max(marker.size, 0.02), 0);
  const material = new THREE.MeshBasicMaterial({
    color: toColor(marker.color),
    transparent: marker.color[3] < 1,
    opacity: marker.color[3],
  });
  const mesh = new THREE.Mesh(geometry, material);
  mesh.position.set(...marker.position);
  mesh.userData = { kind: "marker", marker };
  return mesh;
}

function buildHeatmap(heatmap) {
  const group = new THREE.Group();
  if (heatmap.rows <= 0 || heatmap.cols <= 0) return group;
  const cellCount = heatmap.rows * heatmap.cols;
  if (heatmap.values.length < cellCount) return group;

  let minValue = heatmap.values[0];
  let maxValue = heatmap.values[0];
  for (const value of heatmap.values) {
    minValue = Math.min(minValue, value);
    maxValue = Math.max(maxValue, value);
  }
  const range = maxValue - minValue;

  const geometry = new THREE.PlaneGeometry(heatmap.cellSize, heatmap.cellSize);
  geometry.rotateX(-Math.PI / 2);

  for (let row = 0; row < heatmap.rows; row += 1) {
    for (let col = 0; col < heatmap.cols; col += 1) {
      const value = heatmap.values[row * heatmap.cols + col];
      const t = range > 1e-9 ? (value - minValue) / range : 0.5;
      const r = THREE.MathUtils.lerp(heatmap.colorLow[0], heatmap.colorHigh[0], t);
      const g = THREE.MathUtils.lerp(heatmap.colorLow[1], heatmap.colorHigh[1], t);
      const b = THREE.MathUtils.lerp(heatmap.colorLow[2], heatmap.colorHigh[2], t);
      const a = THREE.MathUtils.lerp(heatmap.colorLow[3], heatmap.colorHigh[3], t);

      const mesh = new THREE.Mesh(geometry, new THREE.MeshBasicMaterial({ color: new THREE.Color(r, g, b), transparent: true, opacity: a, side: THREE.DoubleSide }));
      mesh.position.set(
        heatmap.origin[0] + (col + 0.5) * heatmap.cellSize,
        heatmap.origin[1],
        heatmap.origin[2] + (row + 0.5) * heatmap.cellSize
      );
      group.add(mesh);
    }
  }
  return group;
}

function cloneAsGhost(model, opacity) {
  const clone = model.clone(true);
  clone.traverse((child) => {
    if (!child.isMesh) return;
    const wasArray = Array.isArray(child.material);
    const materials = (wasArray ? child.material : [child.material]).map((material) => {
      const cloned = material.clone();
      cloned.transparent = true;
      cloned.opacity = opacity;
      cloned.depthWrite = false;
      return cloned;
    });
    child.material = wasArray ? materials : materials[0];
  });
  return clone;
}

function pointRadius(p) {
  return Math.hypot(p[0], p[1], p[2]);
}

function calculateRadius(scene) {
  let radius = 1;
  for (const object of scene.objects) {
    radius = Math.max(radius, pointRadius(object.transform.position));
    for (const keyframe of object.keyframes) radius = Math.max(radius, pointRadius(keyframe.transform.position));
    for (const ghost of object.ghosts) radius = Math.max(radius, pointRadius(ghost.transform.position));
  }
  for (const path of scene.paths) for (const point of path.points) radius = Math.max(radius, pointRadius(point));
  for (const vector of scene.vectors) {
    radius = Math.max(radius, pointRadius(vector.origin));
    const end = [vector.origin[0] + vector.direction[0] * vector.scale, vector.origin[1] + vector.direction[1] * vector.scale, vector.origin[2] + vector.direction[2] * vector.scale];
    radius = Math.max(radius, pointRadius(end));
  }
  for (const frame of scene.frames) radius = Math.max(radius, pointRadius(frame.transform.position) + frame.size);
  for (const marker of scene.markers) radius = Math.max(radius, pointRadius(marker.position) + marker.size);
  for (const heatmap of scene.heatmaps) {
    radius = Math.max(radius, pointRadius(heatmap.origin));
    radius = Math.max(radius, pointRadius([heatmap.origin[0] + heatmap.cols * heatmap.cellSize, heatmap.origin[1], heatmap.origin[2] + heatmap.rows * heatmap.cellSize]));
  }
  return radius;
}

function calculatePivot(scene) {
  if (scene.objects.length === 0) return [0, 0, 0];
  const sum = [0, 0, 0];
  for (const object of scene.objects) {
    sum[0] += object.transform.position[0];
    sum[1] += object.transform.position[1];
    sum[2] += object.transform.position[2];
  }
  return sum.map((v) => v / scene.objects.length);
}

export async function buildScene(scene, scenePath, loadModel) {
  const root = new THREE.Group();
  const staticGroup = new THREE.Group();
  root.add(staticGroup);

  for (const path of scene.paths) staticGroup.add(buildPath(path));
  for (const vector of scene.vectors) staticGroup.add(buildVector(vector));
  for (const frame of scene.frames) staticGroup.add(buildFrame(frame));
  for (const heatmap of scene.heatmaps) staticGroup.add(buildHeatmap(heatmap));

  const pickables = [];
  for (const marker of scene.markers) {
    const mesh = buildMarker(marker);
    pickables.push(mesh);
    staticGroup.add(mesh);
  }

  const objectEntries = [];
  for (const sceneObject of scene.objects) {
    const assetUrl = resolveRelative(scenePath, sceneObject.asset);
    const mtlUrl = /\.obj$/i.test(assetUrl) ? withExtension(assetUrl, ".mtl") : null;
    const model = await loadModel(assetUrl, mtlUrl);

    const box = new THREE.Box3().setFromObject(model);
    const center = box.getCenter(new THREE.Vector3());
    const modelRadius = box.getSize(new THREE.Vector3()).length() / 2 || 0.5;
    model.position.sub(center);

    const localGroup = new THREE.Group();
    localGroup.add(model);
    localGroup.add(buildAxes(modelRadius * 0.7, 2));
    root.add(localGroup);

    const globalGroup = new THREE.Group();
    globalGroup.add(buildAxes(modelRadius * 1.15, 3));
    root.add(globalGroup);

    for (const ghost of sceneObject.ghosts) {
      const ghostModel = cloneAsGhost(model, ghost.opacity);
      const ghostGroup = new THREE.Group();
      ghostGroup.add(ghostModel);
      applyTransform(ghostGroup, ghost.transform);
      root.add(ghostGroup);
    }

    objectEntries.push({ sceneObject, localGroup, globalGroup });
  }

  function update(time) {
    for (const entry of objectEntries) {
      const transform = interpolatedTransform(entry.sceneObject, time);
      applyTransform(entry.localGroup, transform);
      entry.globalGroup.position.set(...transform.position);
    }
  }
  update(0);

  return {
    root,
    pickables,
    globalGroups: objectEntries.map((entry) => entry.globalGroup),
    radius: calculateRadius(scene),
    pivot: calculatePivot(scene),
    duration: sceneDuration(scene),
    update,
    legend: {
      objects: scene.objects.length > 0,
      paths: scene.paths.length > 0,
      vectors: scene.vectors.length > 0,
      ghosts: scene.objects.some((o) => o.ghosts.length > 0),
      markers: scene.markers.length > 0,
      frames: scene.frames.length > 0,
      heatmaps: scene.heatmaps.length > 0,
    },
  };
}

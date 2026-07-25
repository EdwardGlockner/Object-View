import "./style.css";

import * as THREE from "three";
import { GLTFLoader } from "three/examples/jsm/loaders/GLTFLoader.js";
import { MTLLoader } from "three/examples/jsm/loaders/MTLLoader.js";
import { OBJLoader } from "three/examples/jsm/loaders/OBJLoader.js";
import { PLYLoader } from "three/examples/jsm/loaders/PLYLoader.js";
import { STLLoader } from "three/examples/jsm/loaders/STLLoader.js";

import { parseScene } from "./scene/parseScene.js";
import { buildScene } from "./scene/buildScene.js";
import { createPlayback } from "./scene/playback.js";
import { createPicker } from "./scene/picking.js";
import { toFetchablePath } from "./scene/sceneAssets.js";

const viewerElement = document.querySelector("#viewer");
const dropzoneElement = document.querySelector("#dropzone");
const sampleSelect = document.querySelector("#sample-select");
const loadSampleButton = document.querySelector("#load-sample");
const modelInput = document.querySelector("#model-files");
const resetViewButton = document.querySelector("#reset-view");
const toggleSpinButton = document.querySelector("#toggle-spin");
const toggleWireframeButton = document.querySelector("#toggle-wireframe");
const clearSceneButton = document.querySelector("#clear-scene");
const screenshotButton = document.querySelector("#screenshot-scene");
const backendStatus = document.querySelector("#backend-status");
const statusText = document.querySelector("#status-text");
const statFormat = document.querySelector("#stat-format");
const statMeshes = document.querySelector("#stat-meshes");
const statTriangles = document.querySelector("#stat-triangles");
const statSize = document.querySelector("#stat-size");
const scenePathInput = document.querySelector("#scene-path");
const loadSceneButton = document.querySelector("#load-scene");
const transportCard = document.querySelector("#transport-card");
const playPauseButton = document.querySelector("#play-pause");
const scrubInput = document.querySelector("#scrub");
const timeLabel = document.querySelector("#time-label");
const legendCard = document.querySelector("#legend-card");
const legendList = document.querySelector("#legend-list");
const markerTooltip = document.querySelector("#marker-tooltip");

const sampleCatalog = {
  "perseverance-rover": {
    label: "Perseverance Rover",
    files: ["/samples/perseverance-rover/perseverance_rover.glb"],
  },
  "craft-racer": {
    label: "Craft Racer",
    files: ["/samples/craft-racer/craft_racer.glb"],
  },
  "toy-car": {
    label: "Toy Car",
    files: ["/samples/toy-car/ToyCar.glb"],
  },
  "calibration-cube": {
    label: "Calibration cube",
    files: ["/samples/calibration-cube/calibration_cube.obj", "/samples/calibration-cube/calibration_cube.mtl"],
  },
  "axis-marker": {
    label: "Axis marker",
    files: ["/samples/axis-marker/axis_marker.obj", "/samples/axis-marker/axis_marker.mtl"],
  },
};

const LEGEND_ROWS = [
  { key: "objects", color: "#ff3f2e", label: "Local axes: spins with object" },
  { key: "objects", color: "#3399ff", label: "Global axes: fixed to world" },
  { key: "paths", color: "#71e7c4", label: "Path: a route or trajectory" },
  { key: "vectors", color: "#bf59d9", label: "Vector: a direction" },
  { key: "ghosts", color: "#b3b3b3", label: "Ghost: alt. pose (faded)" },
  { key: "markers", color: "#fadb59", label: "Marker: point of interest" },
  { key: "frames", color: "#e68033", label: "Frame: authored coord. axes" },
  { key: "heatmaps", color: "#bf4080", label: "Heatmap: value on the ground" },
];

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x101315);
scene.fog = new THREE.Fog(0x101315, 38, 84);

const camera = new THREE.PerspectiveCamera(42, 1, 0.1, 1000);
camera.position.set(5.2, 3.6, 6.2);
camera.lookAt(0, 0, 0);

const renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.domElement.style.cursor = "grab";
viewerElement.appendChild(renderer.domElement);

scene.add(new THREE.AmbientLight(0xffffff, 1.25));

const keyLight = new THREE.DirectionalLight(0xfff0de, 2.15);
keyLight.position.set(6, 8, 7);
scene.add(keyLight);

const rimLight = new THREE.DirectionalLight(0x9ec9ff, 1.15);
rimLight.position.set(-6, 2, -5);
scene.add(rimLight);

const gridHelper = new THREE.GridHelper(10, 10, 0x3a4248, 0x20262a);
gridHelper.position.y = -1.6;
scene.add(gridHelper);

let currentModelRoot = null;
let currentModelContent = null;
let currentObjectUrls = [];
let autoSpin = false;
let wireframe = false;
let baseScale = 1;
let zoomScale = 1;
let lastFrameTime = 0;

let activeScene = null; // { globalGroups, pickables, update, ... } while a scene JSON is loaded
let playback = null;
let scrubbing = false;

const picker = createPicker(renderer.domElement, camera);

const movementKeys = new Set();
const movementKeyCodes = new Set(["KeyW", "KeyA", "KeyS", "KeyD", "KeyQ", "KeyE"]);

const drag = {
  active: false,
  mode: "rotate",
  startX: 0,
  startY: 0,
  rotationX: 0,
  rotationY: 0,
  position: new THREE.Vector3(),
};

function setStatus(message) {
  statusText.textContent = message;
}

function updateStats({ format = "None", meshes = 0, triangles = 0, sizeLabel = "-" } = {}) {
  statFormat.textContent = format;
  statMeshes.textContent = String(meshes);
  statTriangles.textContent = Number(triangles).toLocaleString();
  statSize.textContent = sizeLabel;
}

function createAxisLabel(text, color, scale) {
  const canvas = document.createElement("canvas");
  canvas.width = 128;
  canvas.height = 128;

  const context = canvas.getContext("2d");
  context.fillStyle = color;
  context.font = "800 78px Aptos, Segoe UI, sans-serif";
  context.textAlign = "center";
  context.textBaseline = "middle";
  context.fillText(text, 64, 64);

  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;

  const sprite = new THREE.Sprite(
    new THREE.SpriteMaterial({
      map: texture,
      transparent: true,
      depthTest: false,
      depthWrite: false,
    }),
  );
  sprite.scale.setScalar(scale);
  sprite.renderOrder = 20;
  return sprite;
}

function buildAxes(length, labels) {
  const group = new THREE.Group();
  group.add(new THREE.AxesHelper(length));

  const x = createAxisLabel(labels.x, "#ff775f", length * 0.15);
  x.position.set(length + 0.18, 0, 0);
  group.add(x);

  const y = createAxisLabel(labels.y, "#a8df5f", length * 0.15);
  y.position.set(0, length + 0.18, 0);
  group.add(y);

  const z = createAxisLabel(labels.z, "#49bfff", length * 0.15);
  z.position.set(0, 0, length + 0.18);
  group.add(z);

  return group;
}

function disposeCurrentObjectUrls() {
  currentObjectUrls.forEach((url) => URL.revokeObjectURL(url));
  currentObjectUrls = [];
}

function hideSceneUi() {
  transportCard.hidden = true;
  legendCard.hidden = true;
  markerTooltip.hidden = true;
  picker.setPickables([]);
  playback = null;
  activeScene = null;
}

function clearCurrentModel() {
  hideSceneUi();

  if (!currentModelRoot) {
    updateStats();
    return;
  }

  scene.remove(currentModelRoot);
  currentModelRoot.traverse((child) => {
    child.geometry?.dispose?.();
    const materials = Array.isArray(child.material) ? child.material : [child.material];
    materials.filter(Boolean).forEach((material) => {
      material.map?.dispose?.();
      material.dispose?.();
    });
  });

  currentModelRoot = null;
  currentModelContent = null;
  disposeCurrentObjectUrls();
  updateStats();
}

function createUrlMap(files) {
  disposeCurrentObjectUrls();
  const map = new Map();

  for (const file of files) {
    const url = URL.createObjectURL(file);
    currentObjectUrls.push(url);
    map.set(file.name.toLowerCase(), url);
  }

  return map;
}

function basename(path) {
  return path.split(/[\\/]/).pop().toLowerCase();
}

function createLoadingManager(urlMap = new Map()) {
  const manager = new THREE.LoadingManager();
  manager.setURLModifier((url) => urlMap.get(basename(url)) ?? url);
  return manager;
}

function applyWireframe(object, value) {
  object.traverse((child) => {
    if (!child.isMesh) {
      return;
    }

    const materials = Array.isArray(child.material) ? child.material : [child.material];
    materials.filter(Boolean).forEach((material) => {
      material.wireframe = value;
      material.needsUpdate = true;
    });
  });
}

function countSceneGeometry(object) {
  let meshes = 0;
  let triangles = 0;

  object.traverse((child) => {
    if (!child.isMesh || !child.geometry) {
      return;
    }

    meshes += 1;
    const geometry = child.geometry;
    if (geometry.index) {
      triangles += geometry.index.count / 3;
    } else if (geometry.attributes.position) {
      triangles += geometry.attributes.position.count / 3;
    }
  });

  return { meshes, triangles: Math.round(triangles) };
}

function centerAndFit(object) {
  const root = new THREE.Group();
  const box = new THREE.Box3().setFromObject(object);
  const center = box.getCenter(new THREE.Vector3());
  const size = box.getSize(new THREE.Vector3());
  const maxDimension = Math.max(size.x, size.y, size.z, 0.001);

  object.position.sub(center);
  root.add(object);

  baseScale = 3.15 / maxDimension;
  zoomScale = 1;
  root.scale.setScalar(baseScale);
  root.position.set(0, 0, 0);
  root.rotation.set(-0.08, 0.72, 0);

  const globalAxes = buildAxes(maxDimension * 1.05, { x: "X", y: "Y", z: "Z" });
  const localAxes = buildAxes(maxDimension * 0.48, { x: "x", y: "y", z: "z" });
  root.add(globalAxes);
  root.add(localAxes);

  currentModelRoot = root;
  currentModelContent = object;
  scene.add(root);

  const geometryStats = countSceneGeometry(object);
  updateStats({
    format: "3D",
    meshes: geometryStats.meshes,
    triangles: geometryStats.triangles,
    sizeLabel: `${size.x.toFixed(2)} x ${size.y.toFixed(2)} x ${size.z.toFixed(2)}`,
  });
}

function centerAndFitScene(built) {
  const root = new THREE.Group();
  const pivot = new THREE.Vector3(...built.pivot);
  built.root.position.sub(pivot);
  root.add(built.root);

  const maxDimension = Math.max(built.radius * 2, 0.5);
  baseScale = 3.15 / maxDimension;
  zoomScale = 1;
  root.scale.setScalar(baseScale);
  root.position.set(0, 0, 0);
  root.rotation.set(-0.08, 0.72, 0);

  currentModelRoot = root;
  currentModelContent = built.root;
  scene.add(root);

  const geometryStats = countSceneGeometry(built.root);
  updateStats({
    format: "SCENE",
    meshes: geometryStats.meshes,
    triangles: geometryStats.triangles,
    sizeLabel: `radius ${built.radius.toFixed(2)}`,
  });
}

function resetView() {
  if (!currentModelRoot) {
    return;
  }

  zoomScale = 1;
  currentModelRoot.position.set(0, 0, 0);
  currentModelRoot.rotation.set(-0.08, 0.72, 0);
  currentModelRoot.scale.setScalar(baseScale);
}

async function loadObjWithMaterials(objUrl, mtlUrl, manager = createLoadingManager()) {
  let materials = null;
  if (mtlUrl) {
    try {
      materials = await new MTLLoader(manager).loadAsync(mtlUrl);
      materials.preload();
    } catch {
      materials = null;
    }
  }

  const loader = new OBJLoader(manager);
  if (materials) {
    loader.setMaterials(materials);
  }

  return loader.loadAsync(objUrl);
}

async function loadModelAsset(url, mtlUrl) {
  const extension = url.split(".").pop().toLowerCase();
  if (extension === "obj") {
    return loadObjWithMaterials(url, mtlUrl);
  }
  if (extension === "glb" || extension === "gltf") {
    const result = await new GLTFLoader().loadAsync(url);
    return result.scene;
  }
  if (extension === "stl") {
    const geometry = await new STLLoader().loadAsync(url);
    return new THREE.Mesh(geometry, new THREE.MeshStandardMaterial({ color: 0xd6d2c8, roughness: 0.72 }));
  }
  if (extension === "ply") {
    const geometry = await new PLYLoader().loadAsync(url);
    geometry.computeVertexNormals();
    return new THREE.Mesh(geometry, new THREE.MeshStandardMaterial({ color: 0xd6d2c8, roughness: 0.72 }));
  }
  throw new Error(`Unsupported asset type: ${url}`);
}

async function loadModelFromFileList(fileList) {
  const files = [...fileList];
  const primary = files.find((file) => /\.(glb|gltf|obj|stl|ply)$/i.test(file.name));
  if (!primary) {
    setStatus("Choose at least one supported 3D file.");
    return;
  }

  clearCurrentModel();
  setStatus(`Loading ${primary.name}...`);

  const urlMap = createUrlMap(files);
  const manager = createLoadingManager(urlMap);
  const primaryUrl = urlMap.get(primary.name.toLowerCase());
  const extension = primary.name.split(".").pop().toLowerCase();
  let loadedObject;

  if (extension === "obj") {
    const materialFile = files.find((file) => file.name.toLowerCase().endsWith(".mtl"));
    const materialUrl = materialFile ? urlMap.get(materialFile.name.toLowerCase()) : null;
    loadedObject = await loadObjWithMaterials(primaryUrl, materialUrl, manager);
    inspectObjWithBackend(await primary.text(), primary.name);
  } else if (extension === "glb" || extension === "gltf") {
    const result = await new GLTFLoader(manager).loadAsync(primaryUrl);
    loadedObject = result.scene;
  } else if (extension === "stl") {
    const geometry = await new STLLoader(manager).loadAsync(primaryUrl);
    loadedObject = new THREE.Mesh(geometry, new THREE.MeshStandardMaterial({ color: 0xd6d2c8, roughness: 0.72 }));
  } else if (extension === "ply") {
    const geometry = await new PLYLoader(manager).loadAsync(primaryUrl);
    geometry.computeVertexNormals();
    loadedObject = new THREE.Mesh(geometry, new THREE.MeshStandardMaterial({ color: 0xd6d2c8, roughness: 0.72 }));
  }

  centerAndFit(loadedObject);
  applyWireframe(currentModelContent, wireframe);
  setStatus(`${primary.name} loaded.`);
}

async function inspectObjWithBackend(source, name) {
  try {
    const response = await fetch("/api/inspect", {
      method: "POST",
      headers: {
        "Content-Type": "text/plain",
        "X-Model-Name": name,
      },
      body: source,
    });

    if (!response.ok) {
      return;
    }

    const stats = await response.json();
    statFormat.textContent = "OBJ";
    statTriangles.textContent = Number(stats.triangleCount).toLocaleString();
    if (stats.bounds?.valid) {
      const size = {
        x: stats.bounds.max.x - stats.bounds.min.x,
        y: stats.bounds.max.y - stats.bounds.min.y,
        z: stats.bounds.max.z - stats.bounds.min.z,
      };
      statSize.textContent = `${size.x.toFixed(2)} x ${size.y.toFixed(2)} x ${size.z.toFixed(2)}`;
    }
  } catch {
    backendStatus.textContent = "C++ backend unavailable. Rendering still works.";
  }
}

function renderLegend(flags) {
  legendList.innerHTML = "";
  for (const row of LEGEND_ROWS) {
    if (!flags[row.key]) continue;
    const item = document.createElement("li");
    const swatch = document.createElement("span");
    swatch.className = "legend-swatch";
    swatch.style.background = row.color;
    const label = document.createElement("span");
    label.textContent = row.label;
    item.append(swatch, label);
    legendList.append(item);
  }
  legendCard.hidden = legendList.children.length === 0;
}

function formatSeconds(value) {
  return `${value.toFixed(1)}s`;
}

async function loadSceneFromUrl(url) {
  clearCurrentModel();
  setStatus(`Loading scene ${url}...`);

  const fetchUrl = toFetchablePath(url);
  const response = await fetch(fetchUrl);
  if (!response.ok) {
    throw new Error(`Could not fetch scene: ${response.status}`);
  }
  const raw = await response.json();
  const parsed = parseScene(raw);

  const built = await buildScene(parsed, fetchUrl, loadModelAsset);
  centerAndFitScene(built);

  activeScene = built;
  picker.setPickables(built.pickables);
  renderLegend(built.legend);

  if (built.duration > 0) {
    transportCard.hidden = false;
    playback = createPlayback(built.duration, (time, duration, playing) => {
      built.update(time);
      if (!scrubbing) scrubInput.value = String(time / duration);
      timeLabel.textContent = `${formatSeconds(time)} / ${formatSeconds(duration)}`;
      playPauseButton.textContent = playing ? "Pause" : "Play";
      playPauseButton.classList.toggle("is-active", playing);
    });
  } else {
    transportCard.hidden = true;
  }

  setStatus(`${parsed.name} loaded.`);
}

async function loadSample() {
  const value = sampleSelect.value;

  if (value.startsWith("scene:")) {
    const scenePath = value.slice("scene:".length);
    try {
      await loadSceneFromUrl(scenePath);
    } catch (error) {
      console.error(error);
      setStatus("Could not load the scene.");
    }
    return;
  }

  const sample = sampleCatalog[value];
  clearCurrentModel();
  setStatus(`Loading ${sample.label}...`);

  const extension = sample.files[0].split(".").pop().toLowerCase();
  let object;

  if (extension === "glb" || extension === "gltf") {
    const result = await new GLTFLoader().loadAsync(sample.files[0]);
    object = result.scene;
  } else {
    object = await loadObjWithMaterials(sample.files[0], sample.files[1]);
  }

  centerAndFit(object);
  applyWireframe(currentModelContent, wireframe);

  if (extension === "obj") {
    const objSource = await fetch(sample.files[0]).then((response) => response.text());
    inspectObjWithBackend(objSource, sample.files[0].split("/").pop());
  }

  setStatus(`${sample.label} loaded.`);
}

function resizeRenderer() {
  const { clientWidth, clientHeight } = viewerElement;
  camera.aspect = clientWidth / clientHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(clientWidth, clientHeight, false);
}

function animate(frameTime = 0) {
  requestAnimationFrame(animate);
  const deltaSeconds = Math.min((frameTime - lastFrameTime) / 1000 || 0, 0.05);
  lastFrameTime = frameTime;

  if (autoSpin && currentModelRoot) {
    currentModelRoot.rotation.y += 0.008;
  }

  updateContinuousMovement(deltaSeconds);

  if (playback) {
    playback.tick(deltaSeconds);
  }

  if (activeScene && currentModelRoot) {
    const inverseDrag = currentModelRoot.quaternion.clone().invert();
    for (const globalGroup of activeScene.globalGroups) {
      globalGroup.quaternion.copy(inverseDrag);
    }
  }

  renderer.render(scene, camera);
}

function moveModel(dx, dy, dz, distance = 0.12) {
  if (!currentModelRoot) {
    return;
  }

  const step = distance / Math.max(zoomScale, 0.35);
  currentModelRoot.position.x += dx * step;
  currentModelRoot.position.y += dy * step;
  currentModelRoot.position.z += dz * step;
}

function updateContinuousMovement(deltaSeconds) {
  if (!currentModelRoot || movementKeys.size === 0) {
    return;
  }

  let dx = 0;
  let dy = 0;
  let dz = 0;

  if (movementKeys.has("KeyW")) dz -= 1;
  if (movementKeys.has("KeyS")) dz += 1;
  if (movementKeys.has("KeyA")) dx -= 1;
  if (movementKeys.has("KeyD")) dx += 1;
  if (movementKeys.has("KeyQ")) dy -= 1;
  if (movementKeys.has("KeyE")) dy += 1;

  const length = Math.hypot(dx, dy, dz);
  if (length === 0) {
    return;
  }

  const speed = 1.85;
  moveModel(dx / length, dy / length, dz / length, speed * deltaSeconds);
}

viewerElement.addEventListener("pointerdown", (event) => {
  if (!currentModelRoot) {
    return;
  }

  drag.active = true;
  drag.mode = event.shiftKey || event.button === 2 ? "move" : "rotate";
  drag.startX = event.clientX;
  drag.startY = event.clientY;
  drag.rotationX = currentModelRoot.rotation.x;
  drag.rotationY = currentModelRoot.rotation.y;
  drag.position.copy(currentModelRoot.position);
  renderer.domElement.setPointerCapture(event.pointerId);
  renderer.domElement.style.cursor = drag.mode === "move" ? "move" : "grabbing";
});

viewerElement.addEventListener("pointermove", (event) => {
  if (!drag.active || !currentModelRoot) {
    return;
  }

  const dx = event.clientX - drag.startX;
  const dy = event.clientY - drag.startY;

  if (drag.mode === "move") {
    currentModelRoot.position.x = drag.position.x + dx * 0.008;
    currentModelRoot.position.y = drag.position.y - dy * 0.008;
  } else {
    currentModelRoot.rotation.y = drag.rotationY + dx * 0.008;
    currentModelRoot.rotation.x = drag.rotationX + dy * 0.008;
  }
});

viewerElement.addEventListener("pointerup", (event) => {
  drag.active = false;
  renderer.domElement.releasePointerCapture(event.pointerId);
  renderer.domElement.style.cursor = "grab";
});

viewerElement.addEventListener("contextmenu", (event) => event.preventDefault());

viewerElement.addEventListener("wheel", (event) => {
  if (!currentModelRoot) {
    return;
  }

  event.preventDefault();
  zoomScale = THREE.MathUtils.clamp(zoomScale * (event.deltaY > 0 ? 0.9 : 1.1), 0.18, 7.5);
  currentModelRoot.scale.setScalar(baseScale * zoomScale);
}, { passive: false });

window.addEventListener("keydown", (event) => {
  if (movementKeyCodes.has(event.code)) {
    event.preventDefault();
    movementKeys.add(event.code);
  }
});

window.addEventListener("keyup", (event) => {
  movementKeys.delete(event.code);
});

window.addEventListener("blur", () => {
  movementKeys.clear();
});

modelInput.addEventListener("change", () => loadModelFromFileList(modelInput.files).catch((error) => {
  console.error(error);
  setStatus("Could not load the selected model.");
}));

loadSampleButton.addEventListener("click", () => loadSample().catch((error) => {
  console.error(error);
  setStatus("Could not load the sample.");
}));

loadSceneButton.addEventListener("click", () => {
  const path = scenePathInput.value.trim();
  if (!path) return;
  loadSceneFromUrl(path).catch((error) => {
    console.error(error);
    setStatus("Could not load the scene.");
  });
});

resetViewButton.addEventListener("click", resetView);

toggleSpinButton.addEventListener("click", () => {
  autoSpin = !autoSpin;
  toggleSpinButton.textContent = autoSpin ? "Stop spin" : "Spin";
});

toggleWireframeButton.addEventListener("click", () => {
  wireframe = !wireframe;
  if (currentModelContent) {
    applyWireframe(currentModelContent, wireframe);
  }
});

clearSceneButton.addEventListener("click", () => {
  clearCurrentModel();
  setStatus("Scene cleared.");
});

screenshotButton.addEventListener("click", () => {
  renderer.render(scene, camera);
  renderer.domElement.toBlob((blob) => {
    if (!blob) {
      setStatus("Could not create screenshot.");
      return;
    }

    const link = document.createElement("a");
    link.href = URL.createObjectURL(blob);
    link.download = `objectview-${new Date().toISOString().replace(/[:.]/g, "-")}.png`;
    link.click();
    URL.revokeObjectURL(link.href);
    setStatus("Screenshot saved.");
  }, "image/png");
});

playPauseButton.addEventListener("click", () => playback?.toggle());

scrubInput.addEventListener("input", () => {
  if (!playback) return;
  scrubbing = true;
  playback.pause();
  playback.seek(Number(scrubInput.value) * playback.duration);
});
scrubInput.addEventListener("change", () => {
  scrubbing = false;
});

picker.onHover((object, event) => {
  if (!object || object.userData?.kind !== "marker") {
    markerTooltip.hidden = true;
    renderer.domElement.style.cursor = drag.active ? "grabbing" : "grab";
    return;
  }

  const marker = object.userData.marker;
  const label = marker.label ?? marker.id ?? "marker";
  const rect = viewerElement.getBoundingClientRect();
  markerTooltip.textContent = label;
  markerTooltip.style.left = `${event.clientX - rect.left}px`;
  markerTooltip.style.top = `${event.clientY - rect.top}px`;
  markerTooltip.hidden = false;
  renderer.domElement.style.cursor = "pointer";
});

["dragenter", "dragover"].forEach((eventName) => {
  dropzoneElement.addEventListener(eventName, (event) => {
    event.preventDefault();
    dropzoneElement.classList.add("dragover");
  });
});

["dragleave", "drop"].forEach((eventName) => {
  dropzoneElement.addEventListener(eventName, (event) => {
    event.preventDefault();
    dropzoneElement.classList.remove("dragover");
  });
});

dropzoneElement.addEventListener("drop", (event) => {
  const files = [...event.dataTransfer.files];
  const sceneFile = files.find((file) => file.name.toLowerCase().endsWith(".json"));

  if (sceneFile && files.length === 1) {
    sceneFile.text().then((text) => {
      clearCurrentModel();
      const parsed = parseScene(JSON.parse(text));
      return buildScene(parsed, "/", loadModelAsset).then((built) => {
        centerAndFitScene(built);
        activeScene = built;
        picker.setPickables(built.pickables);
        renderLegend(built.legend);
        setStatus(`${parsed.name} loaded.`);
      });
    }).catch((error) => {
      console.error(error);
      setStatus("Could not load the dropped scene (its asset paths must be reachable from this page).");
    });
    return;
  }

  loadModelFromFileList(event.dataTransfer.files).catch((error) => {
    console.error(error);
    setStatus("Could not load the dropped model.");
  });
});

window.addEventListener("resize", resizeRenderer);
new ResizeObserver(resizeRenderer).observe(viewerElement);

fetch("/api/health")
  .then((response) => response.json())
  .then(() => {
    backendStatus.textContent = "C++ backend connected.";
  })
  .catch(() => {
    backendStatus.textContent = "C++ backend unavailable. The web viewer can still run in Vite.";
  });

resizeRenderer();
animate();
loadSample().catch((error) => {
  console.error(error);
  setStatus("Load a sample or open your own files.");
});

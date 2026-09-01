import "./style.css";

import * as THREE from "three";
import { GLTFLoader } from "three/examples/jsm/loaders/GLTFLoader.js";
import { MTLLoader } from "three/examples/jsm/loaders/MTLLoader.js";
import { OBJLoader } from "three/examples/jsm/loaders/OBJLoader.js";
import { PLYLoader } from "three/examples/jsm/loaders/PLYLoader.js";
import { STLLoader } from "three/examples/jsm/loaders/STLLoader.js";

const viewerElement = document.querySelector("#viewer");
const dropzoneElement = document.querySelector("#dropzone");
const sampleSelect = document.querySelector("#sample-select");
const loadSampleButton = document.querySelector("#load-sample");
const modelInput = document.querySelector("#model-files");
const resetViewButton = document.querySelector("#reset-view");
const toggleSpinButton = document.querySelector("#toggle-spin");
const toggleWireframeButton = document.querySelector("#toggle-wireframe");
const clearModelButton = document.querySelector("#clear-model");
const screenshotButton = document.querySelector("#screenshot-view");
const statusText = document.querySelector("#status-text");
const statFormat = document.querySelector("#stat-format");
const statMeshes = document.querySelector("#stat-meshes");
const statTriangles = document.querySelector("#stat-triangles");
const statSize = document.querySelector("#stat-size");

const assetUrl = (path) => `${import.meta.env.BASE_URL}${path}`;

const sampleCatalog = {
  "perseverance-rover": {
    label: "Perseverance Rover",
    files: [assetUrl("samples/perseverance-rover/perseverance_rover.glb")],
  },
  "craft-racer": {
    label: "Craft Racer",
    files: [assetUrl("samples/craft-racer/craft_racer.glb")],
  },
  "toy-car": {
    label: "Toy Car",
    files: [assetUrl("samples/toy-car/ToyCar.glb")],
  },
  "calibration-cube": {
    label: "Calibration cube",
    files: [assetUrl("samples/calibration-cube/calibration_cube.obj"), assetUrl("samples/calibration-cube/calibration_cube.mtl")],
  },
};

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x101315);

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

let currentModelRoot = null;
let currentModelContent = null;
let currentObjectUrls = [];
let autoSpin = false;
let wireframe = false;
let baseScale = 1;
let zoomScale = 1;
let lastFrameTime = 0;

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

function buildCoordinateSystem(length) {
  const group = new THREE.Group();
  group.add(new THREE.AxesHelper(length));

  const x = createAxisLabel("X", "#ff775f", length * 0.15);
  x.position.set(length + 0.18, 0, 0);
  group.add(x);

  const y = createAxisLabel("Y", "#a8df5f", length * 0.15);
  y.position.set(0, length + 0.18, 0);
  group.add(y);

  const z = createAxisLabel("Z", "#49bfff", length * 0.15);
  z.position.set(0, 0, length + 0.18);
  group.add(z);

  return group;
}

function disposeCurrentObjectUrls() {
  currentObjectUrls.forEach((url) => URL.revokeObjectURL(url));
  currentObjectUrls = [];
}

function clearCurrentModel() {
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

function countGeometry(object) {
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

function centerAndFit(object, formatLabel = "3D") {
  const root = new THREE.Group();
  const box = new THREE.Box3().setFromObject(object);
  const center = box.getCenter(new THREE.Vector3());
  const size = box.getSize(new THREE.Vector3());
  const maxDimension = Math.max(size.x, size.y, size.z, 0.001);

  object.position.sub(center);
  root.add(object);
  root.add(buildCoordinateSystem(Math.max(maxDimension * 0.48, 0.65)));

  baseScale = 3.15 / maxDimension;
  zoomScale = 1;
  root.scale.setScalar(baseScale);
  root.position.set(0, 0, 0);
  root.rotation.set(-0.08, 0.72, 0);

  currentModelRoot = root;
  currentModelContent = object;
  scene.add(root);

  const geometryStats = countGeometry(object);
  updateStats({
    format: formatLabel,
    meshes: geometryStats.meshes,
    triangles: geometryStats.triangles,
    sizeLabel: `${size.x.toFixed(2)} x ${size.y.toFixed(2)} x ${size.z.toFixed(2)}`,
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

async function loadModelAsset(url, mtlUrl, manager = createLoadingManager()) {
  const extension = url.split(".").pop().toLowerCase();
  if (extension === "obj") {
    return loadObjWithMaterials(url, mtlUrl, manager);
  }
  if (extension === "glb" || extension === "gltf") {
    const result = await new GLTFLoader(manager).loadAsync(url);
    return result.scene;
  }
  if (extension === "stl") {
    const geometry = await new STLLoader(manager).loadAsync(url);
    return new THREE.Mesh(geometry, new THREE.MeshStandardMaterial({ color: 0xd6d2c8, roughness: 0.72 }));
  }
  if (extension === "ply") {
    const geometry = await new PLYLoader(manager).loadAsync(url);
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
  let loadedObject = null;

  if (extension === "obj") {
    const materialFile = files.find((file) => file.name.toLowerCase().endsWith(".mtl"));
    const materialUrl = materialFile ? urlMap.get(materialFile.name.toLowerCase()) : null;
    loadedObject = await loadObjWithMaterials(primaryUrl, materialUrl, manager);
  } else {
    loadedObject = await loadModelAsset(primaryUrl, null, manager);
  }

  centerAndFit(loadedObject, extension.toUpperCase());
  applyWireframe(currentModelContent, wireframe);
  setStatus(`${primary.name} loaded.`);
}

async function loadSample() {
  const sample = sampleCatalog[sampleSelect.value];
  clearCurrentModel();
  setStatus(`Loading ${sample.label}...`);

  const extension = sample.files[0].split(".").pop().toLowerCase();
  const object = await loadModelAsset(sample.files[0], sample.files[1]);
  centerAndFit(object, extension.toUpperCase());
  applyWireframe(currentModelContent, wireframe);

  setStatus(`${sample.label} loaded.`);
}

function resizeRenderer() {
  const { clientWidth, clientHeight } = viewerElement;
  camera.aspect = clientWidth / clientHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(clientWidth, clientHeight, false);
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

function animate(frameTime = 0) {
  requestAnimationFrame(animate);
  const deltaSeconds = Math.min((frameTime - lastFrameTime) / 1000 || 0, 0.05);
  lastFrameTime = frameTime;

  if (autoSpin && currentModelRoot) {
    currentModelRoot.rotation.y += 0.008;
  }

  updateContinuousMovement(deltaSeconds);
  renderer.render(scene, camera);
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

clearModelButton.addEventListener("click", () => {
  clearCurrentModel();
  setStatus("Viewer cleared.");
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
  loadModelFromFileList(event.dataTransfer.files).catch((error) => {
    console.error(error);
    setStatus("Could not load the dropped model.");
  });
});

window.addEventListener("resize", resizeRenderer);
new ResizeObserver(resizeRenderer).observe(viewerElement);

resizeRenderer();
animate();
loadSample().catch((error) => {
  console.error(error);
  setStatus("Load a sample or open your own files.");
});

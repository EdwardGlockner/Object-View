// A minimal, self-contained Three.js viewport for a single ObjectView scene:
// camera, renderer, drag-to-rotate/pan, wheel-zoom, resize, and scene
// loading/playback. Used by pages that host more than one viewport at once
// (see web/linked.html), where main.js's single-viewport, raw-model-aware
// logic doesn't apply.

import * as THREE from "three";
import { GLTFLoader } from "three/examples/jsm/loaders/GLTFLoader.js";
import { MTLLoader } from "three/examples/jsm/loaders/MTLLoader.js";
import { OBJLoader } from "three/examples/jsm/loaders/OBJLoader.js";
import { PLYLoader } from "three/examples/jsm/loaders/PLYLoader.js";
import { STLLoader } from "three/examples/jsm/loaders/STLLoader.js";

import { parseScene } from "./parseScene.js";
import { buildScene } from "./buildScene.js";
import { createPlayback } from "./playback.js";
import { createPicker } from "./picking.js";
import { toFetchablePath } from "./sceneAssets.js";

async function loadModelAsset(url, mtlUrl) {
  const extension = url.split(".").pop().toLowerCase();
  if (extension === "obj") {
    let materials = null;
    if (mtlUrl) {
      try {
        materials = await new MTLLoader().loadAsync(mtlUrl);
        materials.preload();
      } catch {
        materials = null;
      }
    }
    const loader = new OBJLoader();
    if (materials) loader.setMaterials(materials);
    return loader.loadAsync(url);
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

export function createViewport(container) {
  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0x101315);
  scene.fog = new THREE.Fog(0x101315, 38, 84);

  const camera = new THREE.PerspectiveCamera(42, 1, 0.1, 1000);
  camera.position.set(5.2, 3.6, 6.2);
  camera.lookAt(0, 0, 0);

  const renderer = new THREE.WebGLRenderer({ antialias: true });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.domElement.style.cursor = "grab";
  container.appendChild(renderer.domElement);

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

  let currentRoot = null;
  let activeScene = null;
  let playback = null;
  let baseScale = 1;
  let zoomScale = 1;

  const picker = createPicker(renderer.domElement, camera);

  const drag = {
    active: false,
    mode: "rotate",
    startX: 0,
    startY: 0,
    rotationX: 0,
    rotationY: 0,
    position: new THREE.Vector3(),
  };

  function resize() {
    const width = container.clientWidth;
    const height = container.clientHeight;
    if (width === 0 || height === 0) return;
    camera.aspect = width / height;
    camera.updateProjectionMatrix();
    renderer.setSize(width, height);
  }
  new ResizeObserver(resize).observe(container);
  resize();

  function clear() {
    if (currentRoot) scene.remove(currentRoot);
    currentRoot = null;
    activeScene = null;
    playback = null;
    picker.setPickables([]);
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

    currentRoot = root;
    scene.add(root);
  }

  async function loadScene(pathOrUrl, { onPlaybackUpdate } = {}) {
    clear();
    const fetchUrl = toFetchablePath(pathOrUrl);
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

    if (built.duration > 0) {
      playback = createPlayback(built.duration, (time, duration, playing) => {
        built.update(time);
        onPlaybackUpdate?.(time, duration, playing);
      });
    }

    return { parsed, built };
  }

  function resetView() {
    if (!currentRoot) return;
    zoomScale = 1;
    currentRoot.position.set(0, 0, 0);
    currentRoot.rotation.set(-0.08, 0.72, 0);
    currentRoot.scale.setScalar(baseScale);
  }

  renderer.domElement.addEventListener("pointerdown", (event) => {
    if (!currentRoot) return;
    drag.active = true;
    drag.mode = event.shiftKey || event.button === 2 ? "move" : "rotate";
    drag.startX = event.clientX;
    drag.startY = event.clientY;
    drag.rotationX = currentRoot.rotation.x;
    drag.rotationY = currentRoot.rotation.y;
    drag.position.copy(currentRoot.position);
    renderer.domElement.setPointerCapture(event.pointerId);
    renderer.domElement.style.cursor = drag.mode === "move" ? "move" : "grabbing";
  });

  renderer.domElement.addEventListener("pointermove", (event) => {
    if (!drag.active || !currentRoot) return;
    const dx = event.clientX - drag.startX;
    const dy = event.clientY - drag.startY;
    if (drag.mode === "move") {
      currentRoot.position.x = drag.position.x + dx * 0.008;
      currentRoot.position.y = drag.position.y - dy * 0.008;
    } else {
      currentRoot.rotation.y = drag.rotationY + dx * 0.008;
      currentRoot.rotation.x = drag.rotationX + dy * 0.008;
    }
  });

  renderer.domElement.addEventListener("pointerup", (event) => {
    drag.active = false;
    renderer.domElement.releasePointerCapture(event.pointerId);
    renderer.domElement.style.cursor = "grab";
  });

  renderer.domElement.addEventListener("contextmenu", (event) => event.preventDefault());

  renderer.domElement.addEventListener(
    "wheel",
    (event) => {
      if (!currentRoot) return;
      event.preventDefault();
      zoomScale = THREE.MathUtils.clamp(zoomScale * (event.deltaY > 0 ? 0.9 : 1.1), 0.18, 7.5);
      currentRoot.scale.setScalar(baseScale * zoomScale);
    },
    { passive: false }
  );

  let lastFrameTime = 0;
  function animate(frameTime = 0) {
    requestAnimationFrame(animate);
    const deltaSeconds = Math.min((frameTime - lastFrameTime) / 1000 || 0, 0.05);
    lastFrameTime = frameTime;

    if (playback) playback.tick(deltaSeconds);

    if (activeScene && currentRoot) {
      const inverseDrag = currentRoot.quaternion.clone().invert();
      for (const globalGroup of activeScene.globalGroups) {
        globalGroup.quaternion.copy(inverseDrag);
      }
    }

    renderer.render(scene, camera);
  }
  animate();

  return {
    scene,
    camera,
    renderer,
    picker,
    loadScene,
    resetView,
    clear,
    get playback() {
      return playback;
    },
    get activeScene() {
      return activeScene;
    },
  };
}

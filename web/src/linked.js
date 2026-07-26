import "./style.css";
import { createViewport } from "./scene/viewport.js";

const leftPathInput = document.querySelector("#left-path");
const leftLoadButton = document.querySelector("#left-load");
const leftViewerElement = document.querySelector("#left-viewer");
const leftStatus = document.querySelector("#left-status");
const leftLegendList = document.querySelector("#left-legend");

const rightPathInput = document.querySelector("#right-path");
const rightLoadButton = document.querySelector("#right-load");
const rightViewerElement = document.querySelector("#right-viewer");
const rightStatus = document.querySelector("#right-status");
const rightLegendList = document.querySelector("#right-legend");
const rightTransport = document.querySelector("#right-transport");
const rightPlayPause = document.querySelector("#right-play-pause");
const rightScrub = document.querySelector("#right-scrub");
const rightTimeLabel = document.querySelector("#right-time-label");

const tooltip = document.querySelector("#marker-tooltip");

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

function renderLegend(listEl, flags) {
  listEl.innerHTML = "";
  for (const row of LEGEND_ROWS) {
    if (!flags[row.key]) continue;
    const item = document.createElement("li");
    const swatch = document.createElement("span");
    swatch.className = "legend-swatch";
    swatch.style.background = row.color;
    const label = document.createElement("span");
    label.textContent = row.label;
    item.append(swatch, label);
    listEl.append(item);
  }
}

function formatSeconds(value) {
  return `${value.toFixed(1)}s`;
}

const left = createViewport(leftViewerElement);
const right = createViewport(rightViewerElement);

let rightScrubbing = false;
let selectedMesh = null;
let selectedOriginalColor = null;

function clearSelection() {
  if (selectedMesh && selectedOriginalColor) {
    selectedMesh.material.color.copy(selectedOriginalColor);
  }
  selectedMesh = null;
  selectedOriginalColor = null;
}

async function loadLeft(path) {
  leftStatus.textContent = `Loading ${path}...`;
  clearSelection();
  try {
    const { parsed, built } = await left.loadScene(path);
    renderLegend(leftLegendList, built.legend);
    leftStatus.textContent = `${parsed.name} loaded.`;
  } catch (error) {
    console.error(error);
    leftStatus.textContent = "Could not load the scene. Check the path and console for details.";
  }
}

async function loadRight(path) {
  rightStatus.textContent = `Loading ${path}...`;
  try {
    const { parsed, built } = await right.loadScene(path, {
      onPlaybackUpdate(time, duration, playing) {
        if (!rightScrubbing) rightScrub.value = String(time / duration);
        rightTimeLabel.textContent = `${formatSeconds(time)} / ${formatSeconds(duration)}`;
        rightPlayPause.textContent = playing ? "Pause" : "Play";
        rightPlayPause.classList.toggle("is-active", playing);
      },
    });
    renderLegend(rightLegendList, built.legend);
    rightTransport.hidden = built.duration <= 0;
    rightStatus.textContent = `${parsed.name} loaded.`;
  } catch (error) {
    console.error(error);
    rightStatus.textContent = "Could not load the scene. Check the path and console for details.";
  }
}

leftLoadButton.addEventListener("click", () => {
  const path = leftPathInput.value.trim();
  if (path) loadLeft(path);
});

rightLoadButton.addEventListener("click", () => {
  const path = rightPathInput.value.trim();
  if (path) loadRight(path);
});

rightPlayPause.addEventListener("click", () => right.playback?.toggle());

rightScrub.addEventListener("input", () => {
  if (!right.playback) return;
  rightScrubbing = true;
  right.playback.pause();
  right.playback.seek(Number(rightScrub.value) * right.playback.duration);
});
rightScrub.addEventListener("change", () => {
  rightScrubbing = false;
});

function wireHover(viewport) {
  viewport.picker.onHover((object, event) => {
    if (!object || object.userData?.kind !== "marker") {
      tooltip.hidden = true;
      return;
    }
    const marker = object.userData.marker;
    tooltip.textContent = marker.label ?? marker.id ?? "marker";
    tooltip.style.left = `${event.clientX}px`;
    tooltip.style.top = `${event.clientY}px`;
    tooltip.hidden = false;
  });
}
wireHover(left);
wireHover(right);

// ?left=<path>&right=<path> query params auto-load both panels on page
// load -- what lets a CLI (e.g. latentworld's `view` command) open this
// page pre-loaded with both scenes instead of requiring two paths to be
// pasted in and Load clicked twice by hand.
const initialParams = new URLSearchParams(window.location.search);
const initialLeft = initialParams.get("left");
const initialRight = initialParams.get("right");
if (initialLeft) {
  leftPathInput.value = initialLeft;
  loadLeft(initialLeft);
}
if (initialRight) {
  rightPathInput.value = initialRight;
  loadRight(initialRight);
}

// ?watch=<ms> re-fetches and rebuilds whichever of left/right had an
// initial path, on an interval -- see main.js's identical mechanism for
// why (pointing this at a filename latentworld.callbacks.generic's
// SnapshotCallback keeps overwriting during a real training run). Same
// tradeoff: each refresh re-fits the camera from scratch.
const watchIntervalMs = Number(initialParams.get("watch"));
if (watchIntervalMs > 0) {
  if (initialLeft) setInterval(() => loadLeft(initialLeft), watchIntervalMs);
  if (initialRight) setInterval(() => loadRight(initialRight), watchIntervalMs);
}

left.picker.onClick((object) => {
  if (!object || object.userData?.kind !== "marker") return;
  const marker = object.userData.marker;
  if (typeof marker.time !== "number") {
    leftStatus.textContent = `Marker "${marker.id}" has no "time" field, nothing to seek to.`;
    return;
  }
  if (!right.playback) {
    leftStatus.textContent = "Load a scene with a timeline on the right first.";
    return;
  }

  clearSelection();
  selectedMesh = object;
  selectedOriginalColor = object.material.color.clone();
  object.material.color.set(0xffffff);

  right.playback.pause();
  right.playback.seek(marker.time);
});

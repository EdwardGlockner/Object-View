// A general-purpose scene picker: load a manifest JSON (a list of labeled
// entries, each a single scene or a linked left/right pair) and click one
// to open it. Doesn't know anything about any specific project's scenes --
// the manifest is just data, the same way a scene path typed into index.html
// or linked.html is.
import "./style.css";
import { toFetchablePath } from "./scene/sceneAssets.js";

const manifestInput = document.querySelector("#manifest-path");
const loadButton = document.querySelector("#load-manifest");
const entriesContainer = document.querySelector("#entries");
const status = document.querySelector("#gallery-status");

function entryUrl(entry) {
  if (entry.kind === "linked") {
    return `linked.html?left=${encodeURIComponent(entry.left)}&right=${encodeURIComponent(entry.right)}`;
  }
  return `index.html?scene=${encodeURIComponent(entry.scene)}`;
}

function renderEntries(manifest) {
  entriesContainer.innerHTML = "";
  for (const entry of manifest.entries ?? []) {
    const card = document.createElement("div");
    card.className = "gallery-card";

    const title = document.createElement("h3");
    title.textContent = entry.label ?? "Untitled";

    const desc = document.createElement("p");
    desc.textContent = entry.description ?? "";

    const button = document.createElement("button");
    button.className = "primary-button";
    button.textContent = entry.kind === "linked" ? "Open linked view" : "Open scene";
    button.addEventListener("click", () => {
      window.location.href = entryUrl(entry);
    });

    card.append(title, desc, button);
    entriesContainer.append(card);
  }
}

async function loadManifest(path) {
  status.textContent = `Loading manifest ${path}...`;
  try {
    const response = await fetch(toFetchablePath(path));
    if (!response.ok) throw new Error(`Could not fetch manifest: ${response.status}`);
    const manifest = await response.json();
    renderEntries(manifest);
    status.textContent = `${manifest.entries?.length ?? 0} demos loaded.`;
  } catch (error) {
    console.error(error);
    status.textContent = "Could not load the manifest. Check the path and console for details.";
  }
}

loadButton.addEventListener("click", () => {
  const path = manifestInput.value.trim();
  if (path) loadManifest(path);
});

const initialManifest = new URLSearchParams(window.location.search).get("manifest");
if (initialManifest) {
  manifestInput.value = initialManifest;
  loadManifest(initialManifest);
}

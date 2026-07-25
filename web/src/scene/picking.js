import * as THREE from "three";

export function createPicker(domElement, camera) {
  const raycaster = new THREE.Raycaster();
  const pointer = new THREE.Vector2();
  let pickables = [];
  let hoverHandler = () => {};
  let clickHandler = () => {};

  function updatePointer(event) {
    const rect = domElement.getBoundingClientRect();
    pointer.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
    pointer.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;
  }

  function pick() {
    if (pickables.length === 0) return null;
    raycaster.setFromCamera(pointer, camera);
    const hits = raycaster.intersectObjects(pickables, false);
    return hits.length > 0 ? hits[0].object : null;
  }

  domElement.addEventListener("pointermove", (event) => {
    updatePointer(event);
    hoverHandler(pick(), event);
  });

  domElement.addEventListener("click", (event) => {
    updatePointer(event);
    clickHandler(pick(), event);
  });

  return {
    setPickables(list) {
      pickables = list;
    },
    onHover(fn) {
      hoverHandler = fn;
    },
    onClick(fn) {
      clickHandler = fn;
    },
  };
}

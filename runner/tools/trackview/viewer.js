if (!window.THREE) {
  document.getElementById("status").textContent = "Could not load the viewer library.";
  throw new Error("THREE");
}
const THREE = window.THREE;

const PALETTE = [0xd7c4a3, 0x8fb7d6, 0x8fbf9a, 0xe0b0c8, 0xe2c56e, 0xaeb6c4, 0xc4a48a];
const PICKED = 0xff2f6e;
const DIM = 0.45;

const view = document.getElementById("view");
const list = document.getElementById("list");
const meta = document.getElementById("meta");

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
renderer.setSize(window.innerWidth, window.innerHeight);
view.appendChild(renderer.domElement);

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x12151a);
const camera = new THREE.PerspectiveCamera(45, window.innerWidth / window.innerHeight, 1, 200000);
const controls = makeOrbit(camera, renderer.domElement);

scene.add(new THREE.AmbientLight(0xffffff, 0.55));
const sun = new THREE.DirectionalLight(0xfff4e5, 1.1);
sun.position.set(0.4, 1, 0.2);
scene.add(sun);

const root = new THREE.Group();
scene.add(root);

let catalog = null;
let current = 0;
/* The deck mesh and, per quad, its piece and base color, so a picked piece
 * can be recolored without rebuilding the track. */
let deck = null;

function add(a, b) {
  return [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
}
function mul(v, s) {
  return [v[0] * s, v[1] * s, v[2] * s];
}
function v3(p) {
  return new THREE.Vector3(p[0], p[1], p[2]);
}

function pushTri(pos, col, a, b, c, color) {
  pos.push(a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]);
  const r = ((color >> 16) & 255) / 255;
  const g = ((color >> 8) & 255) / 255;
  const bl = (color & 255) / 255;
  for (let i = 0; i < 3; i++) col.push(r, g, bl);
}

function laneCenters(n) {
  const out = [];
  const half = (n - 1) / 2;
  for (let i = 0; i < n; i++) out.push((i - half) * catalog.lane);
  return out;
}

function edgesOf(seg, end) {
  const stored = end ? seg.edge1 : seg.edge;
  if (stored) return stored;
  const w = (end ? (seg.width1 == null ? seg.width : seg.width1) : seg.width) / 2;
  return [-w, w];
}

function deckQuad(pos, col, seg, color, lift) {
  const up = seg.up || [0, 1, 0];
  const e0 = edgesOf(seg, false);
  const e1 = edgesOf(seg, true);
  const l0 = add(add(seg.p, mul(seg.right, e0[0])), mul(up, lift));
  const r0 = add(add(seg.p, mul(seg.right, e0[1])), mul(up, lift));
  const l1 = add(add(seg.p1, mul(seg.right1, e1[0])), mul(up, lift));
  const r1 = add(add(seg.p1, mul(seg.right1, e1[1])), mul(up, lift));
  pushTri(pos, col, l0, r0, l1, color);
  pushTri(pos, col, r0, r1, l1, color);
}

function meshFrom(pos, col, opacity) {
  const geom = new THREE.BufferGeometry();
  geom.setAttribute("position", new THREE.Float32BufferAttribute(pos, 3));
  geom.setAttribute("color", new THREE.Float32BufferAttribute(col, 3));
  geom.computeVertexNormals();
  const mat = new THREE.MeshLambertMaterial({
    vertexColors: true,
    transparent: opacity < 1,
    opacity,
    side: THREE.DoubleSide,
  });
  return new THREE.Mesh(geom, mat);
}

function label(text, at, scale) {
  const canvas = document.createElement("canvas");
  canvas.width = 512;
  canvas.height = 96;
  const g = canvas.getContext("2d");
  g.font = "44px Georgia";
  g.fillStyle = "#f3ecdf";
  g.fillText(text, 12, 62);
  const sprite = new THREE.Sprite(new THREE.SpriteMaterial({
    map: new THREE.CanvasTexture(canvas),
    transparent: true,
    depthTest: false,
  }));
  sprite.position.copy(v3(at));
  sprite.scale.set(scale * 4.2, scale * 0.8, 1);
  return sprite;
}

function marker(at, text, scale) {
  const g = new THREE.Group();
  const dot = new THREE.Mesh(
    new THREE.SphereGeometry(scale * 0.08, 16, 12),
    new THREE.MeshLambertMaterial({ color: 0xf3ecdf })
  );
  dot.position.copy(v3(at));
  g.add(dot);
  const up = [at[0], at[1] + scale * 0.35, at[2]];
  g.add(label(text, up, scale));
  return g;
}

function show(index) {
  current = index;
  const track = catalog.tracks[index];
  history.replaceState(null, "", "#" + track.id);
  while (root.children.length) root.remove(root.children[0]);

  const colors = {};
  track.pieces.forEach((p, i) => { colors[p.id] = PALETTE[i % PALETTE.length]; });

  const pos = [];
  const col = [];
  const quads = [];
  const gapPts = [];
  track.segments.forEach((seg) => {
    if (!seg.deck) {
      gapPts.push(v3(seg.p), v3(seg.p1));
      return;
    }
    const base = colors[seg.piece] || 0xcccccc;
    quads.push({ piece: seg.piece, base });
    deckQuad(pos, col, seg, base, 0);
    const us = seg.markU != null ? seg.markU : laneCenters(Math.max(seg.lanes - 1, 0));
    const us1 = seg.markU1 != null ? seg.markU1 : us;
    const lanePos = [];
    const laneCol = [];
    for (let i = 0; i < Math.max(us.length, us1.length); i++) {
      const u = us[Math.min(i, us.length - 1)];
      const u1 = us1[Math.min(i, us1.length - 1)];
      const a = add(add(seg.p, mul(seg.right, u)), mul(seg.up, 3));
      const b = add(add(seg.p1, mul(seg.right1, u1)), mul(seg.up, 3));
      lanePos.push(a[0], a[1], a[2], b[0], b[1], b[2]);
      for (let k = 0; k < 6; k++) laneCol.push(0.15);
    }
    if (lanePos.length) {
      const lg = new THREE.BufferGeometry();
      lg.setAttribute("position", new THREE.Float32BufferAttribute(lanePos, 3));
      const laneMat = new THREE.LineBasicMaterial({ color: 0x2a241c });
      laneMat.polygonOffset = true;
      laneMat.polygonOffsetFactor = -2;
      laneMat.polygonOffsetUnits = -2;
      const lanes = new THREE.LineSegments(lg, laneMat);
      lanes.renderOrder = 2;
      root.add(lanes);
    }
    if (seg.obstacle !== null && seg.obstacle !== undefined) {
      const mid = mul(add(seg.p, seg.p1), 0.5);
      const u = seg.obstacle * catalog.lane;
      const posv = add(add(mid, mul(seg.right, u)), mul(seg.up, 26));
      const box = new THREE.Mesh(
        new THREE.BoxGeometry(128, 52, 112),
        new THREE.MeshLambertMaterial({ color: 0xd4652f })
      );
      const basis = new THREE.Matrix4().makeBasis(v3(seg.right), v3(seg.up), v3(seg.fwd));
      basis.setPosition(v3(posv));
      box.applyMatrix4(basis);
      root.add(box);
    }
  });
  deck = null;
  if (pos.length) {
    const mesh = meshFrom(pos, col, 1);
    root.add(mesh);
    deck = { mesh, quads };
  }

  if (track.ghost) {
    const gpos = [];
    const gcol = [];
    deckQuad(gpos, gcol, track.ghost, 0xf3ecdf, 0);
    const m = meshFrom(gpos, gcol, 0.28);
    root.add(m);
  }

  if (gapPts.length) {
    const gg = new THREE.BufferGeometry().setFromPoints(gapPts);
    root.add(new THREE.LineSegments(gg, new THREE.LineDashedMaterial({
      color: 0xe0a070, dashSize: 80, gapSize: 50,
    })));
    gg.computeBoundingSphere();
    root.children[root.children.length - 1].computeLineDistances();
  }

  const box = new THREE.Box3().setFromObject(root);
  const size = box.getSize(new THREE.Vector3());
  const span = Math.max(size.x, size.y, size.z, 256);
  const grid = new THREE.GridHelper(span * 1.4, 14, 0x3a342c, 0x2a2620);
  grid.position.y = Math.min(0, box.min.y) - 8;
  grid.renderOrder = -1;
  [].concat(grid.material).forEach((m) => {
    m.polygonOffset = true;
    m.polygonOffsetFactor = 4;
    m.polygonOffsetUnits = 4;
  });
  root.add(grid);

  const main = track.segments.filter((s) => s.chain === "main");
  const start = main[0].p;
  const end = main[main.length - 1].p1;
  const scale = Math.max(span * 0.04, 80);
  const strategy = track.span.repeat || (track.span.closes ? "closes" : track.span.repeats ? "continues" : "");
  if (strategy === "closes") {
    root.add(marker(start, "start · closes", scale));
  } else if (strategy === "ends") {
    root.add(marker(start, "start", scale));
    root.add(marker(end, "ends", scale));
  } else if (strategy) {
    root.add(marker(start, "start", scale));
    root.add(marker(end, strategy === "turns" ? "turns" : "repeats", scale));
    if (track.span.repeatFrom > track.span.s0) {
      const seam = track.segments.find((s) => s.s === track.span.repeatFrom && s.chain === "main");
      if (seam) root.add(marker(seam.p, seam.piece, scale));
    }
  } else {
    root.add(marker(start, "start", scale));
    track.segments.filter((s) => s.next < 0).forEach((s) => {
      root.add(marker(s.p1, s.piece + " end", scale));
    });
  }

  fit(box);
  writeMeta(track);
  [...list.querySelectorAll("button.track")].forEach((b, i) => {
    b.classList.toggle("on", i === index);
  });
}

function fit(box) {
  const size = box.getSize(new THREE.Vector3());
  const center = box.getCenter(new THREE.Vector3());
  const dist = Math.max(size.x, size.y, size.z, 400) * 1.35;
  controls.target.copy(center);
  camera.position.set(center.x + dist * 0.7, center.y + dist * 0.48, center.z + dist * 0.7);
  camera.lookAt(center);
  camera.updateProjectionMatrix();
  controls.sync();
}

function makeOrbit(cam, dom) {
  const target = new THREE.Vector3();
  const offset = new THREE.Vector3();
  const spherical = new THREE.Spherical();
  const drag = { on: false, x: 0, y: 0, button: 0 };
  function apply() {
    offset.setFromSpherical(spherical);
    cam.position.copy(target).add(offset);
    cam.lookAt(target);
  }
  function sync() {
    offset.copy(cam.position).sub(target);
    spherical.setFromVector3(offset);
  }
  dom.addEventListener("pointerdown", (event) => {
    drag.on = true;
    drag.button = event.button;
    drag.x = event.clientX;
    drag.y = event.clientY;
    sync();
    dom.setPointerCapture(event.pointerId);
  });
  dom.addEventListener("pointermove", (event) => {
    if (!drag.on) return;
    const dx = event.clientX - drag.x;
    const dy = event.clientY - drag.y;
    drag.x = event.clientX;
    drag.y = event.clientY;
    if (drag.button === 2 || event.shiftKey) {
      const right = new THREE.Vector3().setFromMatrixColumn(cam.matrix, 0);
      const up = new THREE.Vector3().setFromMatrixColumn(cam.matrix, 1);
      const scale = spherical.radius * 0.0015;
      target.addScaledVector(right, -dx * scale);
      target.addScaledVector(up, dy * scale);
    } else if (drag.button === 1) {
      spherical.radius = Math.max(20, spherical.radius * Math.exp(dy * 0.01));
    } else {
      spherical.theta -= dx * 0.005;
      spherical.phi = Math.max(0.05, Math.min(Math.PI - 0.05, spherical.phi - dy * 0.005));
    }
    apply();
  });
  dom.addEventListener("pointerup", () => { drag.on = false; });
  dom.addEventListener("wheel", (event) => {
    event.preventDefault();
    sync();
    spherical.radius = Math.max(20, spherical.radius * Math.exp(event.deltaY * 0.001));
    apply();
  }, { passive: false });
  dom.addEventListener("contextmenu", (event) => event.preventDefault());
  return { target, sync, update() {} };
}

/* The picked piece goes saturated; the rest dim, so the pick reads at once. */
function paintDeck(picked) {
  if (!deck) return;
  const attr = deck.mesh.geometry.getAttribute("color");
  deck.quads.forEach((q, i) => {
    const c = q.piece === picked ? PICKED : q.base;
    const k = picked && q.piece !== picked ? DIM : 1;
    const r = ((c >> 16) & 255) / 255 * k;
    const g = ((c >> 8) & 255) / 255 * k;
    const b = (c & 255) / 255 * k;
    for (let v = i * 6; v < i * 6 + 6; v++) attr.setXYZ(v, r, g, b);
  });
  attr.needsUpdate = true;
}

function focusPiece(id) {
  const track = catalog.tracks[current];
  const segs = track.segments.filter((s) => s.piece === id);
  if (!segs.length) return;
  paintDeck(id);
  history.replaceState(null, "", "#" + track.id + "/" + id);
  const box = new THREE.Box3();
  segs.forEach((s) => {
    box.expandByPoint(v3(s.p));
    box.expandByPoint(v3(s.p1));
  });
  box.expandByScalar(200);
  fit(box);
  [...meta.querySelectorAll("button.piece")].forEach((b) => {
    b.classList.toggle("on", b.dataset.piece === id);
  });
}

function writeMeta(track) {
  const caps = track.capabilities.length ? track.capabilities.join(", ") : "none";
  const span = track.span;
  const strategy = span.repeat || (span.closes ? "closes" : "");
  const seam = span.repeats
    ? "repeats from s " + span.repeatFrom + (strategy ? " · " + strategy : "")
    : "ends";
  const close = "";
  const warnings = track.warnings.map((w) => `<li class="warn">${w}</li>`).join("");
  const pieces = track.pieces.map((p) =>
    `<button class="piece" data-piece="${p.id}">${p.id}<em>s ${p.s0}–${p.s1}</em></button>`
  ).join("");
  meta.innerHTML = `
    <p>${track.summary}</p>
    <p id="span">s ${span.s0}–${span.s1} · ${seam}${close}<br>${track.status} · ${caps}</p>
    ${pieces}
    <ul>${warnings}</ul>`;
  meta.querySelectorAll("button.piece").forEach((b) => {
    b.addEventListener("click", () => focusPiece(b.dataset.piece));
  });
}

function renderList() {
  list.innerHTML = catalog.tracks.map((t, i) =>
    `<button class="track" data-i="${i}"><strong>${t.id}</strong><em>${t.status}</em></button>`
  ).join("");
  list.querySelectorAll("button.track").forEach((b) => {
    b.addEventListener("click", () => show(Number(b.dataset.i)));
  });
}

function setStatus(text) {
  document.getElementById("status").textContent = text || "";
}

function useCatalog(data) {
  if (!data || !Array.isArray(data.tracks))
    throw new Error("Open baked.json. That is the file the viewer draws.");
  catalog = data;
  setStatus("");
  renderList();
  const [trackId, pieceId] = decodeURIComponent(location.hash.slice(1)).split("/");
  show(Math.max(0, catalog.tracks.findIndex((t) => t.id === trackId)));
  if (pieceId) focusPiece(pieceId);
}

async function openJsonFile(file) {
  useCatalog(JSON.parse(await file.text()));
}

const fileInput = document.getElementById("json-file");
document.getElementById("load").addEventListener("click", () => fileInput.click());
fileInput.addEventListener("change", async (event) => {
  const file = event.target.files[0];
  if (!file) return;
  try {
    await openJsonFile(file);
  } catch (error) {
    setStatus(error.message);
  }
});
document.addEventListener("dragover", (event) => event.preventDefault());
document.addEventListener("drop", async (event) => {
  event.preventDefault();
  const file = [...event.dataTransfer.files].find((item) => /\.json$/i.test(item.name));
  if (!file) return;
  try {
    await openJsonFile(file);
  } catch (error) {
    setStatus(error.message);
  }
});

function includedBake() {
  const node = document.getElementById("baked");
  const text = node ? node.textContent.trim() : "";
  if (!text) return null;
  return JSON.parse(text);
}

try {
  const included = includedBake();
  if (included) useCatalog(included);
  else setStatus("Open baked.json with Load.");
} catch (error) {
  setStatus(error.message);
}

window.addEventListener("resize", () => {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
});

(function frame() {
  requestAnimationFrame(frame);
  controls.update();
  renderer.render(scene, camera);
})();

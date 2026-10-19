/* Local stand-in for the GitHub Pages site in this folder.
 * The published site is this folder plus the web disc and the mp3s.
 *
 *   npm start
 */
const http = require("node:http");
const fs = require("node:fs");
const path = require("node:path");
const os = require("node:os");
const { execFileSync } = require("node:child_process");
const { gamePage, gameIds } = require("./game-pages.js");

const port = Number(process.argv[2] || process.env.PORT || 8643);
const site = __dirname;
const repo = path.resolve(site, "..");

const types = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".mp3": "audio/mpeg",
  ".bin": "application/octet-stream",
  ".zip": "application/zip",
  ".css": "text/css; charset=utf-8"
};

function headers(extra) {
  return Object.assign({
    "Cross-Origin-Opener-Policy": "same-origin",
    "Cross-Origin-Embedder-Policy": "credentialless",
    "Cache-Control": "no-cache"
  }, extra);
}

function send(res, status, body, type) {
  const data = Buffer.from(body);
  res.writeHead(status, headers({
    "Content-Type": type || "text/plain; charset=utf-8",
    "Content-Length": data.length
  }));
  res.end(data);
}

function inside(root, file) {
  return file === root || file.startsWith(root + path.sep);
}

/* The Pages build zips runner.cue and runner.bin. Rebuild it here when the disc changes. */
function discZip() {
  const dir = path.join(repo, "build");
  const zip = path.join(os.tmpdir(), "parascene-runner.zip");
  const newest = Math.max(...["runner.cue", "runner.bin"].map((f) => fs.statSync(path.join(dir, f)).mtimeMs));
  if (!fs.existsSync(zip) || fs.statSync(zip).mtimeMs < newest) {
    fs.rmSync(zip, { force: true });
    execFileSync("zip", ["-q", "-9", zip, "runner.cue", "runner.bin"], { cwd: dir });
  }
  return zip;
}

function resolve(urlPath) {
  if (urlPath === "/build/runner-web.bin")
    return path.join(repo, "build", "runner-web.bin");
  if (urlPath === "/build/runner.zip") {
    try { return discZip(); } catch { return null; }
  }
  if (urlPath.startsWith("/runner/audio/")) {
    const name = urlPath.slice("/runner/audio/".length);
    if (!name || name.includes("/") || name.includes("\\") || name.includes(".."))
      return null;
    const file = path.resolve(repo, "runner", "audio", name);
    return inside(path.resolve(repo, "runner", "audio"), file) ? file : null;
  }
  const rel = urlPath.replace(/^\/+/, "");
  const file = path.resolve(site, rel);
  return inside(site, file) ? file : null;
}

function stream(req, res, file) {
  fs.stat(file, (err, st) => {
    if (err || !st.isFile())
      return send(res, 404, "Not found");
    const type = types[path.extname(file).toLowerCase()] || "application/octet-stream";
    const base = headers({
      "Content-Type": type,
      "Accept-Ranges": "bytes"
    });
    let start = 0;
    let end = st.size - 1;
    let status = 200;
    const range = req.headers.range;
    if (range) {
      const match = /^bytes=(\d*)-(\d*)$/.exec(range);
      if (!match) {
        res.writeHead(416, headers({ "Content-Range": `bytes */${st.size}` }));
        return res.end();
      }
      if (match[1] === "") {
        const n = Number(match[2]);
        start = Math.max(0, st.size - n);
      } else {
        start = Number(match[1]);
        if (match[2] !== "")
          end = Number(match[2]);
      }
      if (!Number.isFinite(start) || !Number.isFinite(end) || start > end || start >= st.size) {
        res.writeHead(416, headers({ "Content-Range": `bytes */${st.size}` }));
        return res.end();
      }
      end = Math.min(end, st.size - 1);
      status = 206;
      base["Content-Range"] = `bytes ${start}-${end}/${st.size}`;
    }
    base["Content-Length"] = end - start + 1;
    res.writeHead(status, base);
    if (req.method === "HEAD")
      return res.end();
    fs.createReadStream(file, { start, end }).pipe(res);
  });
}

const server = http.createServer((req, res) => {
  if (req.method !== "GET" && req.method !== "HEAD")
    return send(res, 405, "Method not allowed");
  let urlPath;
  try {
    urlPath = decodeURIComponent(new URL(req.url, "http://127.0.0.1").pathname);
  } catch {
    return send(res, 400, "Bad request");
  }
  const bare = /^\/([a-z0-9-]+)$/.exec(urlPath);
  try {
    if (bare && gameIds(site).includes(bare[1])) {
      res.writeHead(301, headers({ Location: urlPath + "/" }));
      return res.end();
    }
  } catch { /* no manifest; fall through */ }
  if (urlPath.endsWith("/"))
    urlPath += "index.html";
  const page = /^\/([a-z0-9-]+)\/index\.html$/.exec(urlPath);
  if (page) {
    try {
      if (gameIds(site).includes(page[1])) {
        const html = fs.readFileSync(path.join(site, "index.html"), "utf8");
        return send(res, 200, req.method === "HEAD" ? "" : gamePage(html, page[1]), types[".html"]);
      }
    } catch {
      return send(res, 500, "No manifest");
    }
  }
  const file = resolve(urlPath);
  if (!file)
    return send(res, 404, "Not found");
  stream(req, res, file);
});

server.listen(port, "127.0.0.1", () => {
  console.log(`Parascene on http://127.0.0.1:${port}/`);
  console.log(`Runner on http://127.0.0.1:${port}/?game=runner`);
});

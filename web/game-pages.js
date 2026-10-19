/* Each game gets its own address, /<id>/, served by a copy of the player.
 * The copy names its game and points relative URLs back at the site root.
 *
 *   node game-pages.js <site-dir>
 */
const fs = require("node:fs");
const path = require("node:path");

function gamePage(html, id) {
  return html
    .replace('<html lang="en">', `<html lang="en" data-game="${id}">`)
    .replace('<meta charset="utf-8">', '<meta charset="utf-8">\n  <base href="../">');
}

function gameIds(site) {
  const manifest = JSON.parse(fs.readFileSync(path.join(site, "manifest.json"), "utf8"));
  return (manifest.games || []).map((g) => g.id).filter((id) => /^[a-z0-9-]+$/.test(id));
}

module.exports = { gamePage, gameIds };

if (require.main === module) {
  const site = path.resolve(process.argv[2] || __dirname);
  const html = fs.readFileSync(path.join(site, "index.html"), "utf8");
  for (const id of gameIds(site)) {
    fs.mkdirSync(path.join(site, id), { recursive: true });
    fs.writeFileSync(path.join(site, id, "index.html"), gamePage(html, id));
  }
}

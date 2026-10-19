/* Each game gets its own address, /<id>/, served by a copy of the player.
 * The copy names its game, points relative URLs back at the site root,
 * and carries the game's own link preview tags, since unfurl bots do not run scripts.
 *
 *   node game-pages.js <site-dir>
 */
const fs = require("node:fs");
const path = require("node:path");

const SITE = "https://games.parascene.com/";

function attr(s) {
  return String(s).replace(/&/g, "&amp;").replace(/"/g, "&quot;").replace(/</g, "&lt;");
}

function shareTags(game) {
  const title = `${game.title} | Parascene Games`;
  const blurb = game.blurb || `Play ${game.title} in your browser. A PlayStation game from Parascene Games.`;
  const url = `${SITE}${game.id}/`;
  const image = new URL(game.share || "brand/share.png", SITE).href;
  return `<!-- share -->
  <title>${attr(title)}</title>
  <meta name="description" content="${attr(blurb)}">
  <link rel="canonical" href="${attr(url)}">
  <meta property="og:type" content="website">
  <meta property="og:site_name" content="Parascene Games">
  <meta property="og:title" content="${attr(title)}">
  <meta property="og:description" content="${attr(blurb)}">
  <meta property="og:url" content="${attr(url)}">
  <meta property="og:image" content="${attr(image)}">
  <meta property="og:image:width" content="1200">
  <meta property="og:image:height" content="630">
  <meta property="og:image:alt" content="${attr(game.title)} title screen">
  <meta name="twitter:card" content="summary_large_image">
  <!-- /share -->`;
}

function gamePage(html, game) {
  return html
    .replace('<html lang="en">', `<html lang="en" data-game="${game.id}">`)
    .replace('<meta charset="utf-8">', '<meta charset="utf-8">\n  <base href="../">')
    .replace(/<!-- share -->[\s\S]*?<!-- \/share -->/, shareTags(game));
}

function games(site) {
  const manifest = JSON.parse(fs.readFileSync(path.join(site, "manifest.json"), "utf8"));
  return (manifest.games || []).filter((g) => /^[a-z0-9-]+$/.test(g.id));
}

module.exports = { gamePage, games };

if (require.main === module) {
  const site = path.resolve(process.argv[2] || __dirname);
  const html = fs.readFileSync(path.join(site, "index.html"), "utf8");
  for (const game of games(site)) {
    fs.mkdirSync(path.join(site, game.id), { recursive: true });
    fs.writeFileSync(path.join(site, game.id, "index.html"), gamePage(html, game));
  }
}

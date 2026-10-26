const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const {test} = require('node:test');
const vm = require('node:vm');
const {downloadRedirect} = require('./game-pages.js');
const html = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8');
const source = html.slice(html.indexOf('    async function offerDisc('), html.indexOf('    function showPlayer('));
const manifest = JSON.parse(fs.readFileSync(path.join(__dirname, 'manifest.json'), 'utf8'));
const game = manifest.games.find(game => game.id === 'runner');

async function offer(download, fetch) {
  const elements = new Map();
  const element = id => {
    if (!elements.has(id)) elements.set(id, {
      hidden: true, setAttribute(key, value) { this[key] = value; }, addEventListener() {}
    });
    return elements.get(id);
  };
  const context = vm.createContext({URL, fetch, location: {origin: 'https://games.parascene.com'},
    document: {baseURI: 'https://games.parascene.com/', getElementById: element}});
  vm.runInContext(source, context);
  await context.offerDisc({...game, download});
  return element;
}

test('release download stays available without a cross-origin HEAD', async () => {
  const element = await offer(game.download, () => { throw Error('Cross-origin request must not be made'); });
  assert.equal(element('getdisc').hidden, false);
  assert.equal(element('getdisc-link').href, game.download);
  assert.equal(element('disc-link').href, game.download);
  assert.equal(game.download, 'https://github.com/crosshj/parascene-games/releases/latest/download/runner.zip');
});

test('same-origin download keeps its size check and hides a missing ZIP', async () => {
  const missing = await offer('build/runner.zip', async () => ({ok: false}));
  assert.equal(missing('getdisc').hidden, true);
  const present = await offer('build/runner.zip', async () => ({ok: true, headers: {get: () => '10485760'}}));
  assert.equal(present('getdisc').hidden, false);
  assert.equal(present('disc-link').textContent, 'Download BIN/CUE (10 MB)');
});

test('generated pages preserve the game address and old ROM links lead to latest', () => {
  const folder = fs.mkdtempSync(path.join(os.tmpdir(), 'runner-site-'));
  try {
    fs.writeFileSync(path.join(folder, 'index.html'), html);
    fs.writeFileSync(path.join(folder, 'manifest.json'), JSON.stringify(manifest));
    execFileSync(process.execPath, [path.join(__dirname, 'game-pages.js'), folder]);
    const page = fs.readFileSync(path.join(folder, 'runner/index.html'), 'utf8');
    assert.ok(page.includes('<base href="../">'));
    const redirect = fs.readFileSync(path.join(folder, 'build/runner.zip/index.html'), 'utf8');
    assert.equal(redirect, downloadRedirect(game));
    assert.ok(redirect.includes(`content="0;url=${game.download}"`));
    assert.ok(redirect.includes(`href="${game.download}"`));
  } finally {
    fs.rmSync(folder, {recursive: true, force: true});
  }
});

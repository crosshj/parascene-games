#!/usr/bin/env python3
"""Publish a complete Runner release, then retain two managed builds."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

MARKER = '<!-- runner-ci-release -->'
TAG = re.compile(r'runner-build-\d+-\d+-[0-9a-f]{12}')
ASSETS = ('runner.zip', 'runner.zip.sha256', 'runner-build.json')


class GitHub:
    def run(self, *args, data=None):
        result = subprocess.run(['gh', *args], input=data, text=True, capture_output=True)
        if result.returncode:
            raise RuntimeError(result.stderr.strip())
        return result.stdout

    def api(self, method, endpoint, payload=None):
        args = ['api', '--method', method, endpoint]
        if payload is not None:
            args += ['--input', '-']
        raw = self.run(*args, data=json.dumps(payload) if payload is not None else None)
        return json.loads(raw) if raw.strip() else None

    def releases(self, repo):
        result = []
        page = 1
        while True:
            batch = self.api('GET', f'repos/{repo}/releases?per_page=100&page={page}')
            result += batch
            if len(batch) < 100:
                return result
            page += 1


def managed(release):
    return bool(TAG.fullmatch(release['tag_name']) and MARKER in (release.get('body') or ''))


def retention(releases, current):
    candidates = [r for r in releases if managed(r)]
    previous = sorted((r for r in candidates if r['id'] != current and not r['draft']),
                      key=lambda r: r['id'], reverse=True)
    keep = {current, *(r['id'] for r in previous[:1])}
    return [r for r in candidates if r['id'] not in keep]


def validate_assets(folder, commit):
    metadata = json.loads((folder / 'runner-build.json').read_text())
    if metadata['commit'] != commit:
        raise ValueError('Release artifacts do not match the source commit')
    with (folder / 'runner.zip').open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    if (digest != metadata['sha256'] or (folder / 'runner.zip').stat().st_size != metadata['zip_bytes']
            or (folder / 'runner.zip.sha256').read_text() != f'{digest}  runner.zip\n'):
        raise ValueError('Release archive checksum/size mismatch')
    return metadata


def publish(repo, folder, commit, run_number, attempt, github=None):
    if not re.fullmatch(r'[0-9a-f]{40}', commit) or not re.fullmatch(r'[\w.-]+/[\w.-]+', repo):
        raise ValueError('Invalid repository or source commit')
    github = github or GitHub()
    metadata = validate_assets(folder, commit)
    tag = f'runner-build-{int(run_number)}-{int(attempt)}-{commit[:12]}'
    endpoint = f'repos/{repo}/releases'
    # Avoid promoting an older queued build after main has advanced.
    head = github.api('GET', f'repos/{repo}/git/ref/heads/main')['object']['sha']
    if head != commit:
        raise ValueError('Source commit is no longer main HEAD; leaving existing releases unchanged')
    release = github.api('POST', endpoint, {
        'tag_name': tag, 'target_commitish': commit, 'name': f'Runner build {commit[:12]}',
        'draft': True, 'prerelease': False,
        'body': f'{MARKER}\nSource: `{commit}`\n\n[Build log]({metadata["run_url"]})\n\n'
                'Download runner.zip, extract both files, and open runner.cue in DuckStation.\n'
                'runner.zip.sha256 contains the SHA-256 checksum.\n'
    })
    github.run('release', 'upload', tag, *(str(folder / name) for name in ASSETS), '--repo', repo)
    uploaded = github.api('GET', f'{endpoint}/{release["id"]}')
    assets = {a['name']: a for a in uploaded['assets']}
    for name in ASSETS:
        asset = assets.get(name)
        if not asset or asset['state'] != 'uploaded' or asset['size'] != (folder / name).stat().st_size:
            raise ValueError(f'Uploaded release asset is incomplete: {name}')
    remote_digest = assets['runner.zip'].get('digest')
    if remote_digest and remote_digest != 'sha256:' + metadata['sha256']:
        raise ValueError('GitHub upload digest does not match the ROM archive')
    # Drafts keep the existing download live until all assets are uploaded.
    github.api('PATCH', f'{endpoint}/{release["id"]}', {'draft': False, 'make_latest': 'true'})
    latest = github.api('GET', f'{endpoint}/latest')
    if latest['id'] != release['id']:
        raise ValueError('Latest release was not updated; refusing retention cleanup')
    for old in retention(github.releases(repo), release['id']):
        print('Removing old automated ROM release:', old['tag_name'])
        github.run('release', 'delete', old['tag_name'], '--cleanup-tag', '--yes', '--repo', repo)
    url = f'https://github.com/{repo}/releases/latest/download/runner.zip'
    print('Latest ROM:', url)
    return url


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', required=True)
    parser.add_argument('--artifacts', type=Path, required=True)
    parser.add_argument('--commit', required=True)
    parser.add_argument('--run-number', type=int, required=True)
    parser.add_argument('--attempt', type=int, required=True)
    args = parser.parse_args()
    publish(args.repo, args.artifacts, args.commit, args.run_number, args.attempt)

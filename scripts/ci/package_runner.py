#!/usr/bin/env python3
"""Validate the generated CD image and package the downloadable BIN/CUE."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile

SECTOR = 2352


def validate_disc(folder):
    binary = folder / 'runner.bin'
    cue = (folder / 'runner.cue').read_text()
    size = binary.stat().st_size
    if size < 17 * SECTOR or size % SECTOR:
        raise ValueError('ROM must contain complete 2352-byte CD sectors')
    with binary.open('rb') as stream:
        stream.seek(16 * SECTOR + 24)
        if stream.read(7) != b'\x01CD001\x01':
            raise ValueError('ROM does not contain an ISO9660 primary volume descriptor')
    files = re.findall(r'^FILE "([^"]+)" BINARY$', cue, re.M)
    tracks = re.findall(r'^\s*TRACK (\d+) (MODE2/2352|AUDIO)$', cue, re.M)
    if files != ['runner.bin'] or not tracks or tracks[0] != ('01', 'MODE2/2352'):
        raise ValueError('CUE must reference runner.bin with a MODE2/2352 data track')
    if len(tracks) < 2 or any(kind != 'AUDIO' for _, kind in tracks[1:]):
        raise ValueError('Full disc must include its CD audio tracks')
    if [int(number) for number, _ in tracks] != list(range(1, len(tracks) + 1)):
        raise ValueError('CUE track numbers are not consecutive')
    indexes = re.findall(r'^\s*INDEX (\d+) (\d+):(\d+):(\d+)$', cue, re.M)
    starts = [int(m) * 60 * 75 + int(s) * 75 + int(f) for _, m, s, f in indexes]
    if (not starts or starts[0] != 0 or starts != sorted(starts)
            or starts[-1] >= size // SECTOR
            or sum(index == '01' for index, *_ in indexes) != len(tracks)
            or any(int(s) >= 60 or int(f) >= 75 for _, _, s, f in indexes)):
        raise ValueError('CUE indexes are invalid or exceed the ROM')
    return {'bin_bytes': size, 'tracks': len(tracks)}


def package(folder, out, commit, run_url=''):
    if not re.fullmatch(r'[0-9a-f]{40}', commit):
        raise ValueError('A full source commit SHA is required')
    details = validate_disc(folder)
    out.mkdir(parents=True, exist_ok=True)
    archive = out / 'runner.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as target:
        for name in ('runner.cue', 'runner.bin'):
            target.write(folder / name, name)
    with zipfile.ZipFile(archive) as source:
        if source.testzip() is not None:
            raise ValueError('ROM archive failed its CRC check')
    with archive.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    (out / 'runner.zip.sha256').write_text(f'{digest}  runner.zip\n')
    metadata = {**details, 'commit': commit, 'run_url': run_url, 'sha256': digest,
                'zip_bytes': archive.stat().st_size,
                'toolchain': json.loads(Path(__file__).with_name('runner-toolchain.json').read_text())}
    (out / 'runner-build.json').write_text(json.dumps(metadata, indent=2) + '\n')
    return metadata


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--disc-dir', type=Path, default=Path('build'))
    parser.add_argument('--out', type=Path, default=Path('runner/dist'))
    parser.add_argument('--commit', required=True)
    parser.add_argument('--run-url', default='')
    args = parser.parse_args()
    print(json.dumps(package(args.disc_dir, args.out, args.commit, args.run_url), indent=2))

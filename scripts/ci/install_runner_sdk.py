#!/usr/bin/env python3
"""Install the checksum-pinned Linux SDK, including its MIPS compiler."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import tempfile
import urllib.request
import zipfile

MANIFEST = Path(__file__).with_name('runner-toolchain.json')


def install(destination, archive=None):
    config = json.loads(MANIFEST.read_text())
    sdk = destination / config['directory']
    marker = destination / 'installed.sha256'
    if not marker.exists() or marker.read_text().strip() != config['sha256']:
        destination.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory() as folder:
            package = Path(folder) / 'sdk.zip'
            if archive:
                shutil.copyfile(archive, package)
            else:
                with urllib.request.urlopen(config['url'], timeout=120) as response, package.open('wb') as out:
                    shutil.copyfileobj(response, out)
            with package.open('rb') as stream:
                digest = hashlib.file_digest(stream, 'sha256').hexdigest()
            if digest != config['sha256']:
                raise ValueError('SDK archive checksum mismatch')
            with zipfile.ZipFile(package) as source:
                for entry in source.infolist():
                    target = (destination / entry.filename).resolve()
                    if not target.is_relative_to(destination.resolve()):
                        raise ValueError('SDK archive contains an invalid path')
                    if stat.S_ISLNK(entry.external_attr >> 16):
                        link = source.read(entry).decode()
                        if not (target.parent / link).resolve().is_relative_to(destination.resolve()):
                            raise ValueError('SDK archive contains an invalid symbolic link')
                        target.parent.mkdir(parents=True, exist_ok=True)
                        target.unlink(missing_ok=True)
                        target.symlink_to(link)
                    else:
                        source.extract(entry, destination)
                    if not entry.is_dir() and not target.is_symlink():
                        target.chmod((entry.external_attr >> 16) & 0o777)
            marker.write_text(config['sha256'] + '\n')
    for name in ('elf2x', 'mkpsxiso', 'mipsel-none-elf-gcc'):
        if not os.access(sdk / 'bin' / name, os.X_OK):
            raise ValueError(f'SDK tool missing or not executable: {name}')
    if not (sdk / 'lib/libpsn00b/cmake/sdk.cmake').is_file():
        raise ValueError('SDK CMake toolchain missing')
    return sdk


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination', type=Path, required=True)
    parser.add_argument('--archive', type=Path, help='Verify/install an already downloaded archive')
    args = parser.parse_args()
    root = install(args.destination.resolve(), args.archive)
    print(root)
    if os.environ.get('GITHUB_ENV'):
        with open(os.environ['GITHUB_ENV'], 'a') as out:
            out.write(f'PSN00BSDK={root}\n')
        with open(os.environ['GITHUB_PATH'], 'a') as out:
            out.write(f'{root / "bin"}\n')

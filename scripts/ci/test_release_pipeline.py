"""Exercise artifact validation and destructive release-retention decisions."""
import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

from package_runner import SECTOR, package, validate_disc
from publish_runner import ASSETS, MARKER, publish, retention

COMMIT = 'a' * 40


def release(number, draft=False, managed=True):
    return {'id': number, 'tag_name': f'runner-build-{number}-1-' + 'a' * 12,
            'draft': draft, 'body': MARKER if managed else ''}


class FakeGitHub:
    def __init__(self, folder, fail_upload=False, incomplete=False):
        self.calls = []
        self.folder = folder
        self.fail_upload = fail_upload
        self.incomplete = incomplete
        self.old = [release(9), release(8), release(7, draft=True),
                    {'id': 6, 'tag_name': 'v1.0', 'draft': False, 'body': ''}]

    def api(self, method, endpoint, payload=None):
        self.calls.append((method, endpoint, payload))
        if endpoint.endswith('/git/ref/heads/main'):
            return {'object': {'sha': COMMIT}}
        if method == 'POST':
            return release(10, draft=True)
        if method == 'PATCH':
            self.published = True
            return release(10)
        if endpoint.endswith('/latest'):
            return release(10)
        names = ASSETS[:-1] if self.incomplete else ASSETS
        return {**release(10, draft=True), 'assets': [
            {'name': name, 'state': 'uploaded', 'size': (self.folder / name).stat().st_size,
             'digest': 'sha256:' + hashlib.sha256((self.folder / name).read_bytes()).hexdigest()}
            for name in names]}

    def run(self, *args):
        self.calls.append(args)
        if args[:2] == ('release', 'upload') and self.fail_upload:
            raise RuntimeError('simulated upload failure')

    def releases(self, repo):
        return [release(10), *self.old]


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.disc = self.root / 'disc'
        self.disc.mkdir()
        data = bytearray(30 * SECTOR)
        data[16 * SECTOR + 24:16 * SECTOR + 31] = b'\x01CD001\x01'
        (self.disc / 'runner.bin').write_bytes(data)
        (self.disc / 'runner.cue').write_text(
            'FILE "runner.bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n'
            '  TRACK 02 AUDIO\n    INDEX 01 00:00:20\n')
        self.out = self.root / 'release'
        package(self.disc, self.out, COMMIT, 'https://example.test/build/1')

    def test_archive_contains_matching_pair_and_checksum(self):
        with zipfile.ZipFile(self.out / 'runner.zip') as archive:
            self.assertEqual(set(archive.namelist()), {'runner.cue', 'runner.bin'})
            self.assertEqual(archive.read('runner.bin'), (self.disc / 'runner.bin').read_bytes())
        self.assertEqual(validate_disc(self.disc)['tracks'], 2)

    def test_rejects_cue_pointing_to_a_different_binary(self):
        cue = self.disc / 'runner.cue'
        cue.write_text(cue.read_text().replace('runner.bin', 'old.bin'))
        with self.assertRaisesRegex(ValueError, 'CUE must reference'):
            validate_disc(self.disc)

    def test_rejects_truncated_disc(self):
        with (self.disc / 'runner.bin').open('ab') as stream:
            stream.write(b'x')
        with self.assertRaisesRegex(ValueError, 'complete.*sectors'):
            validate_disc(self.disc)

    def test_upload_failure_preserves_previous_releases(self):
        github = FakeGitHub(self.out, fail_upload=True)
        with self.assertRaisesRegex(RuntimeError, 'upload failure'):
            publish('crosshj/parascene-games', self.out, COMMIT, 10, 1, github)
        self.assertFalse(any(call[0] == 'PATCH' or call[:2] == ('release', 'delete') for call in github.calls))

    def test_missing_asset_cannot_promote_or_prune(self):
        github = FakeGitHub(self.out, incomplete=True)
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            publish('crosshj/parascene-games', self.out, COMMIT, 10, 1, github)
        self.assertFalse(any(call[0] == 'PATCH' or call[:2] == ('release', 'delete') for call in github.calls))

    def test_corrupt_artifact_cannot_create_a_release(self):
        (self.out / 'runner.zip').write_bytes(b'broken')
        github = FakeGitHub(self.out)
        with self.assertRaisesRegex(ValueError, 'checksum/size mismatch'):
            publish('crosshj/parascene-games', self.out, COMMIT, 10, 1, github)
        self.assertEqual(github.calls, [])

    def test_remote_checksum_mismatch_preserves_previous_releases(self):
        github = FakeGitHub(self.out)
        api = github.api
        def bad_digest(method, endpoint, payload=None):
            response = api(method, endpoint, payload)
            if method == 'GET' and endpoint.endswith('/10'):
                response['assets'][0]['digest'] = 'sha256:' + '0' * 64
            return response
        github.api = bad_digest
        with self.assertRaisesRegex(ValueError, 'digest'):
            publish('crosshj/parascene-games', self.out, COMMIT, 10, 1, github)
        self.assertFalse(any(call[0] == 'PATCH' or call[:2] == ('release', 'delete') for call in github.calls))

    def test_superseded_commit_cannot_create_a_release(self):
        github = FakeGitHub(self.out)
        github.api = lambda *args: {'object': {'sha': 'b' * 40}}
        with self.assertRaisesRegex(ValueError, 'no longer main HEAD'):
            publish('crosshj/parascene-games', self.out, COMMIT, 10, 1, github)
        self.assertEqual(github.calls, [])

    def test_unconfirmed_latest_cannot_prune_existing_releases(self):
        github = FakeGitHub(self.out)
        api = github.api
        def old_latest(method, endpoint, payload=None):
            response = api(method, endpoint, payload)
            return release(9) if endpoint.endswith('/latest') else response
        github.api = old_latest
        with self.assertRaisesRegex(ValueError, 'refusing retention cleanup'):
            publish('crosshj/parascene-games', self.out, COMMIT, 10, 1, github)
        self.assertFalse(any(call[:2] == ('release', 'delete') for call in github.calls))

    def test_success_keeps_current_and_previous_and_preserves_manual_releases(self):
        github = FakeGitHub(self.out)
        publish('crosshj/parascene-games', self.out, COMMIT, 10, 1, github)
        deleted = [call[2] for call in github.calls if call[:2] == ('release', 'delete')]
        self.assertEqual(deleted, [release(8)['tag_name'], release(7)['tag_name']])
        promoted = next(i for i, call in enumerate(github.calls) if call[0] == 'PATCH')
        removed = next(i for i, call in enumerate(github.calls) if call[:2] == ('release', 'delete'))
        self.assertLess(promoted, removed)
        self.assertIn('--cleanup-tag', github.calls[removed])

    def test_retention_does_not_depend_on_api_order(self):
        entries = [release(7), release(10), release(9), release(8)]
        self.assertEqual({r['id'] for r in retention(entries, 10)}, {7, 8})
        other = copy.deepcopy(release(6))
        other['tag_name'] = 'runner-build-not-ours'
        self.assertEqual(retention([release(10), other], 10), [])


if __name__ == '__main__':
    unittest.main()

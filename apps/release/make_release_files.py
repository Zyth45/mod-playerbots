#!/usr/bin/env python3
"""The machine-readable files of a release, for the servers and launchers that ship SquidBots. Run it from the
repository at each release, on the release commit, then commit what it writes:

    python apps/release/make_release_files.py --version 3.1.5 --since-rev v3.1.4 --since-version 3.1.4

It writes:
- conf/playerbots.conf.settings.json (apps/settings/gen_settings_json.py): the options a server owner changes;
- squidbots-release.json: version, tag, the SQL files by database and stage with their sha256 and size, the keys
  added or whose default changed since the previous release, the optional core headers the module looks for, and
  the versions of the addon, Dungeon Clear and the dashboard tested with it (when given).

A SQL file listed in an earlier manifest must never change: --since-rev reports one that did (and stops), since an
installer that records what it applied refuses to start on a changed file.
"""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SQL = ROOT / 'data' / 'sql'
DIST = 'conf/playerbots.conf.dist'
OUT = ROOT / 'squidbots-release.json'
LARGE = 30 * 1024 * 1024  # GitHub refuses files over 100 MB and warns from 50 MB; some mirrors drop files over 30 MB
KEY = re.compile(r'^#?\s*((?:AiPlayerbot|Playerbots|PlayerbotsDatabase|CoA)\.[\w.]*)\s*=\s*(.*?)\s*$')


def git(*args, check=True):
    r = subprocess.run(['git', '-C', str(ROOT), *args], capture_output=True, text=True, encoding='utf-8')
    if check and r.returncode:
        sys.exit(f'git {" ".join(args)}: {r.stderr.strip()}')
    return r.stdout


def dist_keys(text):
    """{key: default} of the .dist, commented-out keys included (they are read by the code all the same)."""
    keys = {}
    for line in text.splitlines():
        m = KEY.match(line.strip())
        if m and not m.group(1).endswith('.'):
            # AiPlayerbot.BotRace.<id>: one key per race a server adds, shown once by its pattern.
            key = re.sub(r'\.BotRace\.\d+$', '.BotRace.<id>', m.group(1))
            keys.setdefault(key, '' if key != m.group(1) else m.group(2).strip('"'))
    return keys


def sql_files():
    files = []
    for path in sorted(SQL.rglob('*.sql')):
        rel = path.relative_to(SQL).parts
        data = path.read_bytes()
        files.append({
            'database': rel[0],                 # characters, world, playerbots
            'stage': rel[1] if len(rel) > 2 else '',  # base, updates, custom, create, archive
            'path': path.relative_to(ROOT).as_posix(),
            'sha256': hashlib.sha256(data).hexdigest(),
            'size': len(data),
            'large': len(data) > LARGE,
            'immutable': True,
        })
    return files


def changed_since(rev, files):
    """SQL files of rev whose bytes differ now (a published file must never change)."""
    changed = []
    for f in files:
        old = subprocess.run(['git', '-C', str(ROOT), 'show', f'{rev}:{f["path"]}'], capture_output=True)
        if old.returncode == 0 and hashlib.sha256(old.stdout).hexdigest() != f['sha256']:
            changed.append(f['path'])
    return changed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--version', required=True)
    ap.add_argument('--tag', default='')
    ap.add_argument('--since-rev', default='', help='tag or commit of the previous release')
    ap.add_argument('--since-version', default='')
    ap.add_argument('--addon', default='', help='addon version tested with this release')
    ap.add_argument('--dungeon-clear', default='', help='Dungeon Clear version tested with this release')
    ap.add_argument('--dashboard', default='', help='dashboard version tested with this release')
    ap.add_argument('--no-settings', action='store_true', help='do not regenerate playerbots.conf.settings.json')
    a = ap.parse_args()
    tag = a.tag or f'v{a.version}'

    if not a.no_settings:
        cmd = [sys.executable, str(ROOT / 'apps' / 'settings' / 'gen_settings_json.py'), '--version', a.version,
               '--tag', tag]
        if a.since_rev:
            cmd += ['--since-rev', a.since_rev, '--since-version', a.since_version]
        subprocess.run(cmd, check=True)

    files = sql_files()
    keys = dist_keys((ROOT / DIST).read_text(encoding='utf-8'))
    new_keys, changed_defaults = [], []
    if a.since_rev:
        changed = changed_since(a.since_rev, files)
        if changed:
            sys.exit('published SQL files changed since ' + a.since_rev + ' (add a new file instead): ' +
                     ', '.join(changed))
        before = dist_keys(git('show', f'{a.since_rev}:{DIST}'))
        new_keys = sorted(k for k in keys if k not in before)
        changed_defaults = [{'key': k, 'before': before[k], 'now': keys[k]}
                            for k in sorted(keys) if k in before and before[k] != keys[k]]

    headers = set()
    for path in (ROOT / 'src').rglob('*'):
        if path.suffix in ('.h', '.cpp'):
            headers.update(re.findall(r'__has_include\("([^"]+)"\)', path.read_text(encoding='utf-8', errors='replace')))

    doc = {
        'format': 1,
        'module': 'SquidBots (mod-playerbots for Conquest of Azeroth)',
        'version': a.version,
        'tag': tag,
        'commit': git('rev-parse', '--short=8', 'HEAD').strip(),
        'previous_version': a.since_version,
        'notes': 'sql: every file of data/sql, applied per database in the stages base, updates, custom, each in name '
                 'order (create: the database itself; archive: kept for history, never applied). A published file '
                 'never changes; a change comes as a new file. large: over 30 MB, also attached to the release. '
                 'new_keys / changed_defaults: playerbots.conf.dist against the previous release; a key missing '
                 'from a server\'s playerbots.conf takes the default of the code, equal to the .dist. '
                 'optional_core_headers: headers the module uses when the core has them, with a fallback otherwise: '
                 'the module builds on the official core without any of them. '
                 'commit = the commit the files were read from; the build itself is identified by its tag.',
        'settings_file': 'conf/playerbots.conf.settings.json',
        'sql': files,
        'new_keys': new_keys,
        'changed_defaults': changed_defaults,
        'optional_core_headers': sorted(headers),
        'tested_with': {k: v for k, v in (('addon', a.addon), ('dungeon_clear', a.dungeon_clear),
                                          ('dashboard', a.dashboard)) if v},
    }
    OUT.write_text(json.dumps(doc, ensure_ascii=False, indent=1) + '\n', encoding='utf-8', newline='\n')
    print(f'{OUT.name}: {len(files)} SQL files ({sum(f["large"] for f in files)} large), {len(new_keys)} new keys, '
          f'{len(changed_defaults)} changed defaults')


if __name__ == '__main__':
    main()

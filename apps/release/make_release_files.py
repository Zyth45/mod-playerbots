#!/usr/bin/env python3
"""The machine-readable files of a release, for the servers and launchers that ship SquidBots. Run it from the
repository at each release, on the release commit, then commit what it writes:

    python apps/release/make_release_files.py --version 3.1.5 --since-rev v3.1.4 --since-version 3.1.4

It writes:
- conf/playerbots.conf.settings.json (apps/settings/gen_settings_json.py): the options a server owner changes;
- squidbots-release.json: version, tag, the SQL files by database and stage with their sha256 and size, the keys
  added or whose default changed since the previous release, the optional core headers the module looks for, and
  the components of a full SquidBots install (this module, Dungeon Clear, the addon, the dashboard) with the tags
  that go together.

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


def race_rules():
    """The race rules of the module, read from its code, for launchers that list races: the keys a race page writes
    and the module's own defaults. The races themselves (names, sides, ids) belong to the server's races module."""
    header = (ROOT / 'src' / 'CoaBotRaceRules.h').read_text(encoding='utf-8')
    body = header[header.index('inline Gender DefaultGender'):]
    body = body[:body.index('\n}\n')]
    female_part, male_part = body.split('return Gender::Female;', 1)

    def ids(text):
        found = {int(n) for n in re.findall(r'race == (\d+)', text)}
        for lo, hi in re.findall(r'race >= (\d+) && race <= (\d+)', text):
            found.update(range(int(lo), int(hi) + 1))
        return sorted(found)

    config = (ROOT / 'src' / 'PlayerbotAIConfig.cpp').read_text(encoding='utf-8')
    crashing = re.search(r'"AiPlayerbot\.CrashingBotRaces",\s*"([\d,]*)"', config)
    if not crashing:
        sys.exit('AiPlayerbot.CrashingBotRaces default not found in PlayerbotAIConfig.cpp')
    return {
        'base_races': [1, 2, 3, 4, 5, 6, 7, 8, 10, 11],
        'added_races_switch': 'CoACustomRaces.BotsEnable',
        'excluded_key': 'AiPlayerbot.ExcludedBotRaces',
        'crashing_key': 'AiPlayerbot.CrashingBotRaces',
        'crashing_default': [int(n) for n in crashing.group(1).split(',') if n],
        'race_line_key': 'AiPlayerbot.BotRace.<id>',
        'race_line_ids': '"85", or "20/29" for a race with one id per side',
        'race_line_fields': {
            'bots': ['1', '0'],
            'gender': ['male', 'female', 'any'],
            'side': ['alliance', 'horde', 'dbc'],
            'look': ['full', 'simple'],
            'names': ['human', 'orc', 'dwarf', 'nightelf', 'undead', 'tauren', 'gnome', 'troll', 'bloodelf',
                      'draenei', 'generic'],
            'classes': 'class ids separated by commas',
        },
        'single_gender_default': {'male': ids(male_part), 'female': ids(female_part)},
        'notes': 'Random bots take the base races, and the added races of ChrRaces.dbc when the switch is 1. A race '
                 'in excluded_key or crashing_key gets no new bot, and its existing bots stay offline (their '
                 'characters are kept). A race_line_key line sets the rest for one race. Any race id of the server '
                 'is accepted; the module does not need to know it.',
    }


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
        # A new action log line must be written on a player's server (check_log_categories.py).
        subprocess.run([sys.executable, str(ROOT / 'apps' / 'release' / 'check_log_categories.py'), '--since-rev',
                        a.since_rev], check=True)
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
                 'components: the parts of a full SquidBots install and the tags released together (empty tag: '
                 'not given for this release). '
                 'races: the keys a race page writes and the module\'s own race defaults. '
                 'optional_core_headers: headers the module uses when the core has them, with a fallback otherwise: '
                 'the module builds on the official core without any of them. '
                 'commit = the commit the files were read from; the build itself is identified by its tag.',
        'settings_file': 'conf/playerbots.conf.settings.json',
        'sql': files,
        'new_keys': new_keys,
        'changed_defaults': changed_defaults,
        'optional_core_headers': sorted(headers),
        'races': race_rules(),
        'components': [
            {'name': 'SquidBots module (mod-playerbots)', 'kind': 'server module', 'required': True, 'tag': tag,
             'repository': 'https://github.com/Zyth45/mod-playerbots'},
            {'name': 'Dungeon Clear (mod-dungeon-clear)', 'kind': 'server module', 'required': False,
             'tag': a.dungeon_clear and f'v{a.dungeon_clear.lstrip("v")}',
             'note': 'Built next to this module in modules/. Without it the bots play, but the Dungeon features '
                     '(bot groups clearing dungeons and raids with a player) are unavailable; the log and the addon '
                     'say so.'},
            {'name': 'SquidBots addon', 'kind': 'client addon', 'required': False,
             'tag': a.addon and f'v{a.addon.lstrip("v")}',
             'repository': 'https://github.com/Zyth45/squidbots-addon'},
            {'name': 'SquidBots dashboard', 'kind': 'server tool', 'required': False,
             'tag': a.dashboard and f'v{a.dashboard.lstrip("v")}'},
        ],
    }
    OUT.write_text(json.dumps(doc, ensure_ascii=False, indent=1) + '\n', encoding='utf-8', newline='\n')
    print(f'{OUT.name}: {len(files)} SQL files ({sum(f["large"] for f in files)} large), {len(new_keys)} new keys, '
          f'{len(changed_defaults)} changed defaults')


if __name__ == '__main__':
    main()

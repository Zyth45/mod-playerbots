#!/usr/bin/env python3
"""Refuses a new action log line that a player's server would not write.

The worldserver.conf of a CoA install writes "playerbots.coa" (CoaBots.log, level 4) and "server" (Server.log), but
keeps "playerbots" at level 3 (no INFO) and has no "module" logger (root, level 2: lost). The official core's .dist has
no "playerbots.coa": such a line goes to its parent "playerbots" (level 5, Playerbots.log). So a new LOG_INFO or
LOG_DEBUG line goes on "playerbots.coa" (an action of the bots) or "server.loading" (startup), never on "playerbots"
or "module". Lines that were already there are left alone.

    python apps/release/check_log_categories.py --since-rev <previous release>

Exit 1 with the lines to move. make_release_files.py runs it with its --since-rev.
"""
import argparse
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
REFUSED = re.compile(r'LOG_(INFO|DEBUG)\(\s*"(playerbots|module)"\s*,')


def added_lines(rev):
    diff = subprocess.run(['git', '-C', str(ROOT), 'diff', '-U0', rev, '--', 'src'], capture_output=True,
                          text=True, encoding='utf-8')
    if diff.returncode:
        sys.exit(f'git diff {rev}: {diff.stderr.strip()}')
    path, line_no = None, 0
    for line in diff.stdout.splitlines():
        if line.startswith('+++ '):
            path = line[6:] if line.startswith('+++ b/') else None
        elif line.startswith('@@'):
            line_no = int(re.search(r'\+(\d+)', line).group(1))
        elif line.startswith('+'):
            yield path, line_no, line[1:]
            line_no += 1


def check(rev):
    bad = [(p, n, l.strip()) for p, n, l in added_lines(rev) if p and REFUSED.search(l)]
    for p, n, l in bad:
        print(f'{p}:{n}: new INFO/DEBUG line on "playerbots" or "module", not written on a player\'s server: {l[:100]}')
    return not bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--since-rev', required=True)
    a = ap.parse_args()
    if not check(a.since_rev):
        sys.exit('move these lines to "playerbots.coa" (bot actions) or "server.loading" (startup)')
    print('log categories: ok')


if __name__ == '__main__':
    main()

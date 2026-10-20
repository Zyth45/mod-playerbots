#!/usr/bin/env python3
"""Writes conf/playerbots.conf.settings.json: the options of playerbots.conf that matter to a server owner (bot
count, levels, gear, players' own bots, the Conquest of Azeroth options, the ones support asks to change), with a
title, a one-line description, their group, type, default and range, for tools that edit playerbots.conf.
Run it from the repository root at each release:

    python apps/settings/gen_settings_json.py --version 1.8 --tag v1.8 --since-rev <commit of the previous release> --since-version 1.7

Type, default and range come from conf/playerbots.conf.dist and from the code (sConfigMgr->GetOption<T> in src/);
titles, descriptions and groups from the list below. A listed option missing from the .dist stops the script.
"""
import argparse
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
DIST = ROOT / 'conf' / 'playerbots.conf.dist'
OUT = ROOT / 'conf' / 'playerbots.conf.settings.json'

GROUPS = {
    'population': 'Population',
    'party': 'Groups and players\' bots',
    'combat': 'Combat',
    'activities': 'Activities: dungeons, PvP, cities',
    'chat': 'Chat',
    'other': 'Other',
}

# key (without "AiPlayerbot."), group, title, description
SETTINGS = [
    ('RandomBotAutologin', 'population', 'Populate the world automatically',
     'Random bots log in when the world server starts. Off: only the bots players add themselves.'),
    ('MinRandomBots', 'population', 'Minimum online bots',
     'Lower bound of the random bot population. 500 is what 16 GB of memory holds comfortably; 5,000 bots use about 9 GB of server memory.'),
    ('MaxRandomBots', 'population', 'Maximum online bots',
     'Upper bound of the random bot population; more bots use more CPU and memory. Keep it at or above the minimum.'),
    ('RandomBotAccountCount', 'population', 'Random bot accounts',
     'Accounts that hold the random bots; 0 works out how many are needed.'),
    ('DisabledWithoutRealPlayer', 'population', 'Only while players are online',
     'Random bots log out when no real player is online and come back when one logs in.'),
    ('RandomBotMinLevel', 'population', 'Lowest bot level', 'Lowest level of the random bots.'),
    ('RandomBotMaxLevel', 'population', 'Highest bot level', 'Highest level of the random bots (60 is the Conquest of Azeroth level cap).'),
    ('CoaClassesOnly', 'population', 'Conquest of Azeroth classes only',
     'Random bots are created only in the 21 Conquest of Azeroth classes, never in the nine WotLK classes.'),
    ('GatherOnlySkillUp', 'activities', 'Gather only for skill-ups',
     'Gathering bots (herbalism, mining, skinning) skip herbs, veins and corpses that are grey for them and no longer '
     'raise their skill. One bot can gather everything again with the chat command "nc +gather all".'),
    ('ExcludedBotRaces', 'population', 'Races bots never use',
     'Comma-separated race ids of the custom races module, added to the always-excluded crashing races below. Only '
     'matters when CoACustomRaces.BotsEnable = 1; bots already created keep their race.'),
    ('CrashingBotRaces', 'population', 'Custom races that crash the client',
     'Always excluded from bots, on top of the list above. Default 65, 72, 77, 80, 81 (Test Race, Skyborne, '
     'Darkfallen), per the custom races module author.'),
    ('CoaBotSurname', 'population', '"Bot" surname',
     'Random bots carry the surname "Bot", so players tell them from real players at a glance.'),
    ('RandomGearQualityLimit', 'population', 'Best gear quality',
     'Highest item quality random bots get: 2 uncommon, 3 rare, 4 epic.'),
    ('RandomGearScoreLimit', 'population', 'Item level cap',
     'Highest item level random bots wear: 78 matches Molten Core, Onyxia and Zul\'Gurub gear; 0 = no limit.'),
    ('CoaEpicGearMinLevel', 'population', 'Level for epic gear',
     'With epic quality allowed, the level from which bots may wear epic gear; below it they keep to rare.'),
    ('RandomGearLoweringChance', 'population', 'Chance of lower gear',
     'Chance (0 to 1) that an item of a random bot is one quality lower, so not every bot wears the best gear.'),

    ('AllowAccountBots', 'party', 'Your own characters as bots',
     'Players may log their own characters in as bots (.playerbots bot add <name>); they keep their own level and gear.'),
    ('AllowGuildBots', 'party', 'Guild members\' characters as bots',
     'Players may also add the characters of their guild members as bots.'),
    ('MaxAddedBots', 'party', 'Bots per player', 'How many bots one player may control at the same time.'),
    ('AutoInitOnly', 'party', 'Class bots: automatic setup only',
     'Players may only use init=auto on their class bots (rebuilt at their level with matching gear); game masters keep every init= command.'),
    ('CoaRecruitSameFaction', 'party', 'Recruited bots of the player\'s faction',
     'Bots recruited by a player (lfg bot, .playerbots coa tank|heal|dps, the SquidBots Lite buttons) are of the player\'s faction.'),
    ('CoaRaidPlayerMax', 'party', 'Player-built raids at a time',
     '".playerbots coa raid" fills the raid of whoever types it with bots; players may only build one while the realm has fewer such raids than this. Game masters have no limit.'),
    ('CoaRaidPlayerCooldown', 'party', 'Minutes between two player raids',
     'A player may build one raid of bots every so many minutes.'),

    ('CoaSpecRotations', 'combat', 'Conquest of Azeroth rotations',
     'Each Conquest of Azeroth specialization plays its authored rotation and position; off, bots pick spells on their own only.'),
    ('CoaSmartHeal', 'combat', 'Smart healing',
     'Healers heal whoever is under 25% health first and the tank before the others, and pick the heal for how urgent it is.'),
    ('CoaSmartTank', 'combat', 'Smart tanking',
     'Tanks take first the enemy on the healer, then the one on whoever is lowest, rather than the nearest.'),
    ('CoaTankThreatBonus', 'combat', 'Tank bots threat bonus (%)',
     'Tank bots cause this much more threat, so that players dealing heavy damage keep under them. 0 = none.'),
    ('CoaThreatHold', 'combat', 'Damage dealers keep under the tank (%)',
     'Damage dealer bots hold their attacks at this share of the tank threat (20 more at range). 0 = no hold.'),
    ('CoaInterruptCoordination', 'combat', 'Coordinated interrupts',
     'In a group, one bot kicks each enemy cast and the others keep their interrupt for the next one.'),
    ('CoaDungeonPriorities', 'combat', 'Dungeon priorities',
     'Bots kick heals and crowd control first; in dungeons they purge boss buffs and enrages, and damage dealers kill healing adds and totems before the boss.'),
    ('CoaBossKnowledge', 'combat', 'Boss mechanics',
     'Bots read dungeon and raid casts: they step out of point blank casts, cones and areas while the cast bar runs.'),
    ('CoaKeepPassiveAuras', 'combat', 'Keep passive auras',
     'A bot whose level or gear is rebuilt keeps its class passives; without it, it lost them until its next login and did far less damage.'),
    ('CoaHealerManaReserve', 'combat', 'Healer mana reserve (%)',
     'Share of mana a bot that can heal keeps for healing; under it, it stops spending mana on damage. 0 = no reserve.'),
    ('CoaAttackLoop', 'combat', 'Reliable attack casting',
     'Attacks go through the same cast path as heals: a spell refused for a passing reason is retried soon instead of set aside.'),
    ('CoaExcludedSpecializations', 'combat', 'Specializations bots never play',
     'Conquest of Azeroth specialization ids, comma separated; a bot holding one switches to another of its role.'),
    ('TellWhenAvoidAoe', 'combat', 'Say which spell is avoided',
     'Bots say in chat which enemy spell they step out of ("avoiding Rain of Fire"). Off by default: noisy.'),

    ('RandomBotJoinLfg', 'activities', 'Join players\' dungeon queues',
     'Random bots fill the dungeon finder groups of players.'),
    ('RandomBotJoinBG', 'activities', 'Join players\' battleground queues',
     'Random bots fill the battleground and arena queues of players.'),
    ('RandomBotAutoJoinBG', 'activities', 'Battlegrounds between bots',
     'Random bots also start battlegrounds and arenas by themselves, without players.'),
    ('RandomBotAutoJoinBGRatedArena2v2Count', 'activities', 'Rated 2v2 arenas between bots',
     'How many rated 2v2 arenas the bot teams keep going at once. Bot teams also queue when a player joins a rated queue. 0 = none.'),
    ('RandomBotAutoJoinBGRatedArena3v3Count', 'activities', 'Rated 3v3 arenas between bots',
     'How many rated 3v3 arenas the bot teams keep going at once. Bot teams also queue when a player joins a rated queue. 0 = none.'),
    ('RandomBotAutoJoinBGRatedArena5v5Count', 'activities', 'Rated 5v5 arenas between bots',
     'How many rated 5v5 arenas the bot teams keep going at once. Bot teams also queue when a player joins a rated queue. 0 = none.'),
    ('RandomBotArenaTeam2v2Count', 'activities', 'Bot arena teams (2v2)',
     'Number of 2v2 arena teams of bots, each with a rating from 1000 to 2000 kept between restarts. A bot is in one team at most.'),
    ('RandomBotArenaTeam3v3Count', 'activities', 'Bot arena teams (3v3)',
     'Number of 3v3 arena teams of bots, each with a rating from 1000 to 2000 kept between restarts. A bot is in one team at most.'),
    ('RandomBotArenaTeam5v5Count', 'activities', 'Bot arena teams (5v5)',
     'Number of 5v5 arena teams of bots, each with a rating from 1000 to 2000 kept between restarts. A bot is in one team at most.'),
    ('CoaRulesetForBots', 'activities', 'High Risk and War Mode bots',
     'A share of the random bots plays in High Risk or War Mode (PvP flag on; anyone may attack them, and they attack '
     'High Risk and War Mode characters). Off: every bot in PvE.'),
    ('CoaRulesetHighRiskPct', 'activities', 'High Risk bots (%)', 'Share of the random bots in High Risk, when the option above is on.'),
    ('CoaRulesetWarModePct', 'activities', 'War Mode bots (%)', 'Share of the random bots in War Mode, when the option above is on.'),
    ('CoaGearByContent', 'activities', 'Gear by activity',
     'Random bots wear PvP gear in battlegrounds, arenas and High Risk, PvE gear otherwise, and the item level of the player who groups them.'),
    ('CoaGearMatchGroup', 'activities', 'Grouped bots match your item level',
     'A bot grouped with a player is geared at the item level of that player. Off: it keeps the gear it has.'),
    ('CoaGearHighRiskPvp', 'activities', 'High Risk bots always in PvP gear',
     'High Risk bots wear PvP gear all the time. Off: only in a battleground or an arena, like the other bots.'),
    ('RpgStatusProbWeight.GoCity', 'activities', 'City life',
     'Weight of the city visit among the activities of the random bots (10 = about one bot in ten): the bot goes to a '
     'capital of its faction, visits the bank, auction house, inn and trainers, then goes back. 0 = never.'),
    ('CoaCityStayMinMinutes', 'activities', 'City visit, shortest (minutes)', 'Shortest stay of a bot visiting a capital.'),
    ('CoaCityStayMaxMinutes', 'activities', 'City visit, longest (minutes)', 'Longest stay of a bot visiting a capital.'),
    ('CoaLfgBots', 'activities', '"lfg bot" in chat',
     'A player who says "lfg bot heal", "lfg bot tank" or "lfg bot dps" in chat is whispered by free bots of those roles to invite.'),
    ('CoaLfgLevelRange', 'activities', '"lfg bot" level range',
     'A bot within this many levels of the player keeps its level and gear; one further away is rebuilt at the player\'s level.'),

    ('BotTextLocale', 'chat', "Language of the bots' chat",
     'Language of what the bots say in chat (without AI): Auto follows the client language of the players online, '
     'or one language for everybody. Nearly all texts exist in French and German; about half in Spanish and Russian.'),
    ('RandomBotTalk', 'chat', 'Bots talk', 'Random bots say things in chat now and then.'),
    ('EnableBroadcasts', 'chat', 'Bot announcements',
     'Random bots announce what they do (loot, quests, levels) in the chat channels.'),
    ('CoaLfgChannels', 'chat', '"lfg bot" channels', 'Chat channels where bots listen for "lfg bot", comma separated.'),
    ('CoaLfgAnnounceMinutes', 'chat', '"lfg bot" reminder (minutes)',
     'Every so many minutes, a message to the realm on how to ask the bots. 0 = never.'),

    ('CoaStatusFile', 'other', 'Dashboard status file',
     'Path of the bot-status.json snapshot the SquidBots dashboard reads (position, health, activity of every bot). Empty = not written.'),
    ('CoaArenaReady', 'activities', 'Arena ready pylon',
     'A pylon in each arena starting room, as on the original CoA realm: players click it, bots get ready on their own, and the gates open a few seconds after everyone is ready.'),
    ('CoaArenaReadyCountdown', 'activities', 'Arena start once everyone is ready',
     'Seconds left before the gates open once every combatant is ready (5-4-3-2-1 shown on screen).'),
    ('CoaArenaPreparation', 'activities', 'Bots prepare before the gates open',
     'In the starting room bots buff, call pets and take a spot by role (healers behind, melee in front) facing the gate.'),
    ('CoaArenaKite', 'activities', 'Arena kiting',
     'Ranged bots and healers step away from melee in arenas instead of standing still, unless they are about to win.'),
    ('CoaArenaFocus', 'activities', 'Arena focus',
     'Damage bots go for the enemy healer, or the lowest enemy in sight, in arenas.'),
    ('CoaArenaCover', 'activities', 'Arena pillars',
     'Bots under pressure hide behind the arena pillars (Nagrand, Blade’s Edge, Ruins of Lordaeron, Dalaran, Ring of Valor).'),
    ('CoaDuelOpening', 'activities', 'Duel opening positions',
     'During a duel countdown bots take a distance that suits them: close for melee, about 20 yards for ranged.'),
    ('CoaPvpRecovery', 'activities', 'PvP recovery',
     'In duels, arenas and battlegrounds a cornered bot can crowd control its attacker, then break away; bandage when safe (CoaPvpBandage).'),
]

# Bounds the code does not enforce but the meaning does (key without "AiPlayerbot.": min, max)
RANGES = {'RandomGearQualityLimit': (1, 5), 'RandomBotMinLevel': (1, 80), 'RandomBotMaxLevel': (1, 80),
          'BotTextLocale': (-1, 8), 'CoaRulesetHighRiskPct': (0, 100), 'CoaRulesetWarModePct': (0, 100)}
# Settings that take one of a few values: shown as a list (value, label).
CHOICES = {'BotTextLocale': [(-1, "Auto (players' client language)"), (0, 'English'), (2, 'Français'), (3, 'Deutsch'),
                             (6, 'Español'), (8, 'Русский')]}

# Commented out in the .dist, read anyway; and CoA options whose name does not start with Coa.
COMMENTED = {'AiPlayerbot.ExcludedBotRaces'}
COA_ALSO = {'ExcludedBotRaces', 'CrashingBotRaces', 'GatherOnlySkillUp'}
KEY = re.compile(r'^((?:AiPlayerbot|Playerbots|PlayerbotsDatabase)[\w.]*)\s*=\s*(.*?)\s*$')
OPTION = re.compile(r'(?:std::(min|max)<[\w:]+>\(\s*(-?[\d.]+)f?\s*,\s*)?'
                    r'sConfigMgr->GetOption<([\w:]+)>\(\s*"([\w.]+)"\s*,\s*([^;]*?)\)\s*(\))?\s*;')
TYPES = {'bool': 'bool', 'int': 'int', 'int32': 'int', 'int32_t': 'int', 'uint32': 'int', 'uint32_t': 'int',
         'uint8': 'int', 'uint16': 'int', 'float': 'float', 'double': 'float', 'std::string': 'string'}
UNSIGNED = ('uint32', 'uint32_t', 'uint8', 'uint16')


def git(*args):
    return subprocess.run(['git', '-C', str(ROOT), *args], capture_output=True, text=True, encoding='utf-8').stdout


def dist_values(text):
    found = {m.group(1): m.group(2) for m in (KEY.match(l.strip()) for l in text.splitlines()) if m}
    # A key left commented out on purpose (another module writes it, and the first copy of a key wins) counts too.
    for line in text.splitlines():
        m = KEY.match(line.strip().lstrip('#').strip()) if line.strip().startswith('#') else None
        if m and m.group(1) in COMMENTED:
            found.setdefault(m.group(1), m.group(2))
    return found


def parse_code():
    """{key: (type, code default, min, max)} from the GetOption calls of src/."""
    found = {}
    for path in (ROOT / 'src').rglob('*.cpp'):
        text = path.read_text(encoding='utf-8', errors='replace')
        for clamp, bound, ctype, key, default, _ in OPTION.findall(text):
            kind = TYPES.get(ctype.strip())
            if not kind:
                continue
            lo = 0 if ctype.strip() in UNSIGNED else None
            hi = None
            if clamp == 'min':
                hi = float(bound) if '.' in bound else int(bound)
            elif clamp == 'max':
                lo = float(bound) if '.' in bound else int(bound)
            found.setdefault(key, (kind, default.strip().strip('"'), lo, hi))
    return found


def value(raw, kind):
    v = raw.strip().strip('"')
    try:
        if kind == 'bool':
            return v.lower() not in ('0', 'false', 'no', 'off', '')
        if kind == 'int':
            return int(v)
        if kind == 'float':
            return float(v.rstrip('fF'))
    except ValueError:
        pass
    return v


def infer(raw):
    v = raw.strip()
    if re.fullmatch(r'-?\d+', v):
        return 'int'
    if re.fullmatch(r'-?\d*\.\d+f?', v):
        return 'float'
    return 'string'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--version', required=True)
    ap.add_argument('--tag', default='')
    ap.add_argument('--since-rev', default='')
    ap.add_argument('--since-version', default='')
    a = ap.parse_args()

    dist = dist_values(DIST.read_text(encoding='utf-8'))
    code = parse_code()
    before = dist_values(git('show', f'{a.since_rev}:conf/playerbots.conf.dist')) if a.since_rev else {}

    missing = [k for k, *_ in SETTINGS if f'AiPlayerbot.{k}' not in dist]
    if missing:
        sys.exit('missing from playerbots.conf.dist: ' + ', '.join(missing))

    settings = []
    for short, group, title, description in SETTINGS:
        key = f'AiPlayerbot.{short}'
        raw = dist[key]
        kind, code_default, lo, hi = code.get(key, (infer(raw), None, None, None))
        if kind == 'int' and re.search(r'(Pct|Percent|Chance|Reserve)$', key):
            lo, hi = 0 if lo is None else lo, 100 if hi is None else hi
        if kind == 'float' and key.endswith('Chance'):
            lo, hi = 0.0, 1.0
        if short in RANGES:
            lo, hi = RANGES[short]
        item = {'key': key, 'group': group, 'title': title, 'description': description, 'type': kind,
                'default': value(raw, kind)}
        if lo is not None:
            item['min'] = lo
        if hi is not None:
            item['max'] = hi
        if kind != 'string' and code_default is not None and value(code_default, kind) != item['default']:
            item['default_if_missing'] = value(code_default, kind)
        if short in CHOICES:
            item['choices'] = [{'value': v, 'label': label} for v, label in CHOICES[short]]
        item['coa'] = short.startswith('Coa') or short in COA_ALSO
        if before:
            if key not in before:
                item['new_in'] = a.version
            elif value(before[key], kind) != item['default']:
                item['changed_in'] = a.version
                item['previous_default'] = value(before[key], kind)
        settings.append(item)

    doc = {
        'format': 1,
        'module': 'SquidBots (mod-playerbots for Conquest of Azeroth)',
        'version': a.version,
        'tag': a.tag,
        'commit': git('rev-parse', '--short=8', 'HEAD').strip(),
        'file': 'configs/modules/playerbots.conf',
        'notes': 'The options a server owner usually changes, not all of playerbots.conf.dist. default = value in '
                 'playerbots.conf.dist; default_if_missing = what the code uses when the key is absent; min/max '
                 'where the code or the meaning bounds the value; coa = option of the Conquest of Azeroth fork; '
                 f'new_in / changed_in compare with {a.since_version or "the previous release"}. Every option '
                 'needs a world server restart, except those whose description in the .dist says ".reload config". '
                 'commit = the commit the options were read from; the build itself is identified by its tag.',
        'groups': GROUPS,
        'settings': settings,
    }
    OUT.write_text(json.dumps(doc, ensure_ascii=False, indent=1) + '\n', encoding='utf-8')
    print(f'{OUT.relative_to(ROOT)}: {len(settings)} options, {sum(s["coa"] for s in settings)} CoA, '
          f'{sum("new_in" in s for s in settings)} new, {sum("changed_in" in s for s in settings)} changed')


if __name__ == '__main__':
    main()

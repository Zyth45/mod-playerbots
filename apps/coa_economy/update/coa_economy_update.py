"""CoA Bots 3.0.1 update: two one-time database steps of CoA Economy, run by coa_update.py on every realm.

1. phantomAuctions: auctions whose item no longer exists (item_instance), left by bots deleted while they sold. The
   core never loads them (CHAR_SEL_AUCTIONS joins item_instance): nobody sees them, nobody can bid, they never expire.
   They are removed; a bid on one (none seen so far) goes back to the bidder by mail first, the way the core refunds a
   cancelled auction (auction mail from its house, subject item:0:4, money = the bid).
2. botsGold: each RANDOM bot keeps min(its gold, the sum of the BotLevelGold steps up to its level), read from the
   realm's own playerbots.conf (its .dist value when the key is missing; an empty value: the step does nothing). Gold is
   only lowered. Only characters.money of random bots changes: account name starting with the realm's
   RandomBotAccountPrefix AND playerbots_account_type = 1 in the realm's playerbots database. Players' characters and
   alts, bots of type 0 and 2, guild banks, mail and auctions are never touched.

Both run once per realm: a mark in the realm's playerbots database (coa_update_marks), written in the same
transaction as the change, so an undo of the update (the database copied before it) also takes the mark back.
The amounts before are also written to <backup>/economy/<realm>-*.json. MySQL runs; the realms' worldservers do not
(an online bot would write its gold back from memory).

Entry point for coa_update.py: run_steps(repack, backup, journal, emit, save_journal).
"""
import json
import re
import subprocess
import time
import uuid
from pathlib import Path

HIDDEN = getattr(subprocess, "CREATE_NO_WINDOW", 0)
DEFAULT_PREFIX = "rndbot"
LEVEL_GOLD_KEY = "AiPlayerbot.CoaEconomy.BotLevelGold"
PREFIX_KEY = "AiPlayerbot.RandomBotAccountPrefix"
NAME_RE = re.compile(r"^[A-Za-z0-9_]+$")
BATCH = 500


# ------------------------------------------------------------------------------------------- pure rules (tested)
def parse_level_gold(text):
    """"10:5000,15:10000" -> {10: 5000, 15: 10000}; the module's rule (CoaEconomyRules::ParseLevelGold): a level from
    2 to 255, copper from 1 to 2,000,000,000, anything else skipped."""
    table = {}
    for entry in (text or "").split(","):
        level, sep, copper = entry.partition(":")
        level, copper = level.strip(), copper.strip()
        if not sep or not level.isdigit():
            continue
        digits = re.match(r"\d+", copper)
        if not digits:
            continue
        lvl, cu = int(level), int(digits.group())
        if 1 < lvl <= 255 and 0 < cu <= 2_000_000_000:
            table[lvl] = cu
    return table


def scale_for(level, table):
    """What the scale pays a bot that rose from level 1 to this level: the sum of its steps up to it."""
    return sum(copper for lvl, copper in table.items() if lvl <= level)


def new_gold(money, level, table):
    """Only lowered: a bot poorer than the scale keeps what it has."""
    return min(money, scale_for(level, table))


def is_random_bot(username, account_type, prefix):
    """Both marks of a random bot: its account name starts with the realm's prefix and the playerbots database says
    random (type 1). A player's account, an addclass bot (2), an account no longer random (0): never."""
    return (bool(prefix) and username is not None and username.upper().startswith(prefix.upper())
            and account_type == 1)


def read_conf(path):
    """Key = value of an AzerothCore .conf (the last one wins), quotes removed; {} when the file is missing."""
    values = {}
    if not Path(path).is_file():
        return values
    for line in Path(path).read_text(encoding="utf-8-sig", errors="replace").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] == '"':
            value = value[1:-1]
        values[key.strip()] = value
    return values


def db_name(info):
    """'host;port;user;password;database' -> database (never the password)."""
    parts = (info or "").split(";")
    return parts[4].strip() if len(parts) >= 5 else ""


def realms(repack):
    """The realms with bots of this repack: CoA (CoA-Bots\\Core) and each Royaumes\\<key>. One entry per characters
    database; the BotLevelGold of the realm's conf, else of the .dist, None when the realm emptied it."""
    repack = Path(repack)
    dist = read_conf(repack / "CoA-Bots/Core/configs/modules/playerbots.conf.dist")
    found, seen = [], set()
    dirs = [("coa", repack / "CoA-Bots/Core/configs")]
    royaumes = repack / "Royaumes"
    if royaumes.is_dir():
        dirs += [(d.name, d / "configs") for d in sorted(royaumes.iterdir()) if d.is_dir()]
    for key, configs in dirs:
        world = read_conf(configs / "worldserver.conf")
        bots = read_conf(configs / "modules/playerbots.conf")
        chars, auth, pbots = db_name(world.get("CharacterDatabaseInfo")), db_name(world.get("LoginDatabaseInfo")), \
            db_name(bots.get("PlayerbotsDatabaseInfo"))
        if not bots or not chars or chars in seen:
            continue
        if not all(NAME_RE.match(n or "-") for n in (chars, auth, pbots)):
            raise RuntimeError(f"realm {key}: unexpected database names in its configs")
        prefix = bots.get(PREFIX_KEY, dist.get(PREFIX_KEY, DEFAULT_PREFIX))
        if not NAME_RE.match(prefix or "-"):
            raise RuntimeError(f"realm {key}: unexpected {PREFIX_KEY}")
        level_gold = bots[LEVEL_GOLD_KEY] if LEVEL_GOLD_KEY in bots else dist.get(LEVEL_GOLD_KEY)
        seen.add(chars)
        found.append({"key": key, "chars": chars, "auth": auth, "bots": pbots, "prefix": prefix,
                      "levelGold": level_gold if level_gold and level_gold.strip() else None})
    return found


# ------------------------------------------------------------------------------------------------- database
def mysql_runner(repack):
    """SQL through stdin (no command-line limit, one session: temporary tables live through the script)."""
    repack = Path(repack)

    def run(sql):
        command = [str(repack / "mysql/bin/mysql.exe"), f"--defaults-file={repack / 'mysql/admin-client.ini'}",
                   "--default-character-set=utf8mb4", "-N", "-B"]
        result = subprocess.run(command, input=sql.encode("utf-8"), capture_output=True, timeout=600,
                                creationflags=HIDDEN)
        if result.returncode:
            raise RuntimeError(result.stderr.decode("utf-8", errors="replace").strip())
        return result.stdout.decode("utf-8")
    return run


def rows(text):
    return [line.split("\t") for line in text.splitlines() if line.strip()]


# mysql.exe 8.4 sometimes gives back no output at all (exit 0, nothing on stderr; about 1 or 2 calls in 100 on the
# Builder's test copy): an empty answer would read as "nothing". Every script ends with this witness line; a read
# without it is run again, up to TRIES times, then the step fails (no mark, the update undoes itself).
WITNESS = "COA_FIN"
TRIES = 5


def read(run, sql, what):
    """The rows of a read-only script, the witness checked (and removed)."""
    for attempt in range(TRIES):
        lines = rows(run(sql + f"SELECT '{WITNESS}';\n"))
        if lines and lines[-1] == [WITNESS]:
            return lines[:-1]
        time.sleep(0.2 * (attempt + 1))
    raise RuntimeError(f"MySQL gave no complete answer to: {what} ({TRIES} tries); nothing was changed by this step")


def write(run, sql):
    """A writing script is never run twice: its answer may be lost, so its result is always read back afterwards."""
    run(sql + f"SELECT '{WITNESS}';\n")


def marks_sql(realm):
    return (f"CREATE TABLE IF NOT EXISTS `{realm['bots']}`.`coa_update_marks` (`name` VARCHAR(64) NOT NULL, "
            "`done_at` DATETIME NOT NULL, `detail` VARCHAR(255) NOT NULL DEFAULT '', PRIMARY KEY (`name`)) "
            "ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='CoA Bots update: one-time steps done on this realm';\n")


def mark_of(run, realm, name):
    """The detail of this realm's mark (e.g. 'removed=2 refunds=1 copper=5000'), None when the step was not done."""
    found = read(run, marks_sql(realm) + f"SELECT `detail` FROM `{realm['bots']}`.`coa_update_marks` "
                 f"WHERE `name` = '{name}';\n", f"the mark {name} of {realm['key']}")
    return found[0][0] if found else None


def detail_values(detail):
    return dict(part.split("=", 1) for part in detail.split() if "=" in part)


def run_token():
    """Written in the mark's detail by the run that did the step: a mark without it was there before."""
    return uuid.uuid4().hex[:12]


# The mark is the first write of the step's transaction, INSERT IGNORE: @fresh = 1 only when this run put it, and
# every other write of the step is gated on @fresh. A step already marked (a second run, a mark read wrong) changes
# nothing, and the mark exists only once the step's writes are committed with it (one transaction).
def mark_first_sql(realm, name):
    return [f"INSERT IGNORE INTO `{realm['bots']}`.`coa_update_marks` VALUES ('{name}', NOW(), 'pending');",
            "SET @fresh = ROW_COUNT();"]


def mark_detail_sql(realm, name, detail_expr):
    return (f"UPDATE `{realm['bots']}`.`coa_update_marks` SET `detail` = {detail_expr} "
            f"WHERE `name` = '{name}' AND @fresh = 1;")


def save_rows(backup, realm, what, header, data):
    folder = Path(backup) / "economy"
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / f"{realm['key']}-{what}.json"
    path.write_text(json.dumps({"realm": realm["key"], "database": realm["chars"], "columns": header, "rows": data},
                               indent=0), encoding="utf-8")
    return path


def phantom_sql(c):
    return (f"SELECT ah.id, ah.houseid, ah.itemguid, ah.itemowner, ah.buyoutprice, ah.time, ah.buyguid, ah.lastbid, "
            f"ah.startbid, ah.deposit FROM `{c}`.auctionhouse ah LEFT JOIN `{c}`.item_instance ii "
            f"ON ii.guid = ah.itemguid WHERE ii.guid IS NULL;\n")


def phantom_auctions(run, realm, backup):
    """Removes this realm's auctions without an item; bids refunded by mail first. Returns (removed, refunds, copper),
    None when already done."""
    c, name = realm["chars"], "phantomAuctions"
    if mark_of(run, realm, name) is not None:
        return None
    cols = ["id", "houseid", "itemguid", "itemowner", "buyoutprice", "time", "buyguid", "lastbid", "startbid", "deposit"]
    found = read(run, phantom_sql(c), f"the auctions without an item of {realm['key']}")
    save_rows(backup, realm, "phantom-auctions", cols, found)
    token = run_token()
    sql = [f"USE `{c}`;", "START TRANSACTION;"] + mark_first_sql(realm, name) + [
        "DROP TEMPORARY TABLE IF EXISTS coa_phantom;",
        "CREATE TEMPORARY TABLE coa_phantom AS SELECT ah.id, ah.houseid, ah.itemowner, ah.buyguid, ah.lastbid, "
        "ah.buyoutprice, ah.deposit FROM auctionhouse ah LEFT JOIN item_instance ii ON ii.guid = ah.itemguid "
        "WHERE ii.guid IS NULL AND @fresh = 1;",
        "DROP TEMPORARY TABLE IF EXISTS coa_refund;",
        "CREATE TEMPORARY TABLE coa_refund (n INT UNSIGNED AUTO_INCREMENT PRIMARY KEY) AS SELECT p.* "
        "FROM coa_phantom p JOIN characters b ON b.guid = p.buyguid WHERE p.buyguid <> 0 AND p.lastbid > 0 "
        "ORDER BY p.id;",
        "SET @base = (SELECT COALESCE(MAX(id), 0) FROM mail);",
        "INSERT INTO mail (id, messageType, stationery, mailTemplateId, sender, receiver, subject, body, has_items, "
        "expire_time, deliver_time, money, cod, checked) SELECT @base + n, 2, 62, 0, "
        "houseid, buyguid, CONCAT('0:0:4:', id, ':1'), CONCAT(LPAD(HEX(itemowner), 16, '0'), ':', lastbid, ':', "
        "buyoutprice, ':', deposit, ':0'), 0, UNIX_TIMESTAMP() + 2592000, UNIX_TIMESTAMP(), lastbid, 0, 4 "
        "FROM coa_refund WHERE @fresh = 1;",
        "DELETE ah FROM auctionhouse ah JOIN coa_phantom p ON p.id = ah.id WHERE @fresh = 1;",
        "SET @removed = (SELECT COUNT(*) FROM coa_phantom);",
        "SELECT COUNT(*), COALESCE(SUM(lastbid), 0) INTO @refunds, @copper FROM coa_refund;",
        mark_detail_sql(realm, name, f"CONCAT('removed=', @removed, ' refunds=', @refunds, ' copper=', @copper, "
                                     f"' run={token}')"),
        "COMMIT;"]
    write(run, "\n".join(sql) + "\n")
    # Read back: the mark committed with its numbers, and no auction without an item left.
    detail = mark_of(run, realm, name)
    if detail is None or detail == "pending":
        raise RuntimeError(f"{realm['key']}: the auctions without an item were not removed (no mark after the step)")
    values = detail_values(detail)
    if values.get("run") != token:
        return None                                     # done by an earlier run: nothing changed now
    left = read(run, phantom_sql(c), f"the auctions without an item of {realm['key']}, after the step")
    if left:
        raise RuntimeError(f"{realm['key']}: {len(left)} auction(s) without an item still there after the step")
    return int(values["removed"]), int(values["refunds"]), int(values["copper"])


def bots_gold_plan(run, realm, table):
    """(bots, changes [(guid, account, level, before, after)], copper before, copper after) of the random bots now."""
    c, a, b, prefix = realm["chars"], realm["auth"], realm["bots"], realm["prefix"]
    found = read(run, f"SELECT c.guid, c.account, c.level, c.money, acc.username, IFNULL(t.account_type, -1) "
                      f"FROM `{c}`.characters c JOIN `{a}`.account acc ON acc.id = c.account "
                      f"LEFT JOIN `{b}`.playerbots_account_type t ON t.account_id = c.account "
                      f"WHERE acc.username LIKE '{prefix}%';\n", f"the random bots' gold of {realm['key']}")
    bots, changes, before, after = 0, [], 0, 0
    for guid, account, level, money, username, account_type in found:
        if not is_random_bot(username, int(account_type), prefix):
            continue
        bots += 1
        money, level = int(money), int(level)
        target = new_gold(money, level, table)
        before += money
        after += target
        if target < money:
            changes.append((int(guid), int(account), level, money, target))
    return bots, changes, before, after


def bots_gold(run, realm, backup):
    """Brings each random bot's gold down to the scale. Returns (bots, lowered, copper before, copper after), None
    when already done, "no-scale" when the realm's scale is empty."""
    name = "botsGold"
    if mark_of(run, realm, name) is not None:
        return None
    table = parse_level_gold(realm["levelGold"]) if realm["levelGold"] else {}
    if not table:
        return "no-scale"
    bots, changes, before, after = bots_gold_plan(run, realm, table)
    save_rows(backup, realm, "bots-gold", ["guid", "account", "level", "before", "after"], changes)
    token = run_token()
    sql = [f"USE `{realm['chars']}`;", "START TRANSACTION;"] + mark_first_sql(realm, name) + [
        "DROP TEMPORARY TABLE IF EXISTS coa_gold;",
        "CREATE TEMPORARY TABLE coa_gold (guid INT UNSIGNED PRIMARY KEY, was INT UNSIGNED NOT NULL, "
        "now INT UNSIGNED NOT NULL);"]
    for i in range(0, len(changes), BATCH):
        sql.append("INSERT INTO coa_gold VALUES " + ",".join(f"({g},{m},{t})" for g, _, _, m, t in changes[i:i + BATCH])
                   + ";")
    # Only where the gold is still what was read: a character changed since is left as it is.
    sql += ["UPDATE characters ch JOIN coa_gold g ON g.guid = ch.guid AND ch.money = g.was SET ch.money = g.now "
            "WHERE @fresh = 1;",
            "SET @done = ROW_COUNT();",
            mark_detail_sql(realm, name, f"CONCAT('bots={bots} lowered=', @done, ' before={before} after={after} "
                                         f"run={token}')"),
            "COMMIT;"]
    write(run, "\n".join(sql) + "\n")
    detail = mark_of(run, realm, name)
    if detail is None or detail == "pending":
        raise RuntimeError(f"{realm['key']}: the bots' gold was not changed (no mark after the step)")
    values = detail_values(detail)
    if values.get("run") != token:
        return None                                     # done by an earlier run: nothing changed now
    # Read back: no random bot above the scale any more.
    _, still, _, _ = bots_gold_plan(run, realm, table)
    if still:
        raise RuntimeError(f"{realm['key']}: {len(still)} random bot(s) still above the scale after the step")
    return int(values["bots"]), int(values["lowered"]), int(values["before"]), int(values["after"])


def run_steps(repack, backup, journal, emit, save_journal, run=None):
    """Both steps on every realm of the repack; MySQL up, worldservers stopped. Journal: journal['economy'][realm]."""
    run = run or mysql_runner(repack)
    done = journal.setdefault("economy", {})
    for realm in realms(repack):
        entry = done.setdefault(realm["key"], {})
        if "phantomAuctions" not in entry:
            result = phantom_auctions(run, realm, backup)
            entry["phantomAuctions"] = list(result) if result else "done before"
            save_journal(backup, journal)
            if result:
                emit("info", f"{realm['key']}: {result[0]} auction(s) without an item removed "
                             f"({result[1]} bid(s) refunded by mail, {result[2]} copper)")
        if "botsGold" not in entry:
            result = bots_gold(run, realm, backup)
            entry["botsGold"] = list(result) if isinstance(result, tuple) else (result or "done before")
            save_journal(backup, journal)
            if result == "no-scale":
                emit("info", f"{realm['key']}: bots' gold left as it is ({LEVEL_GOLD_KEY} is empty in its settings)")
            elif result:
                emit("info", f"{realm['key']}: random bots' gold brought down to the level scale: {result[1]} of "
                             f"{result[0]} bots, {result[2] // 10000} gold -> {result[3] // 10000} gold "
                             "(players' characters and alts not touched)")
    return done

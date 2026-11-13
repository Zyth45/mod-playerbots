"""Tests of coa_economy_update.py.

python test_coa_economy_update.py                      the rules only
python test_coa_economy_update.py --mysql <dir>        also on a private MySQL 8 (<dir>\\bin\\mysqld.exe): two realms,
                                                       a player's main and alt, a bot of type 2 and of type 0, a mixed
                                                       guild, auctions without an item (one with a player's bid)
"""
import json
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import coa_economy_update as eco  # noqa: E402

SCALE = ("10:5000,15:10000,20:20000,25:30000,30:50000,35:70000,40:100000,45:120000,50:150000,55:200000,60:250000,"
         "65:300000,70:350000,75:400000,80:500000")


def test_rules():
    table = eco.parse_level_gold(SCALE)
    assert len(table) == 15
    assert eco.scale_for(9, table) == 0 and eco.scale_for(10, table) == 5000 and eco.scale_for(13, table) == 5000
    assert eco.scale_for(60, table) == 1_005_000 and eco.scale_for(80, table) == 2_555_000
    assert eco.new_gold(18_208_000, 60, table) == 1_005_000          # 1 821 g -> 100.5 g
    assert eco.new_gold(1_000, 10, table) == 1_000                    # poorer than the scale: kept
    assert eco.new_gold(296_000, 13, table) == 5_000                  # Squid World level 13: 29.6 g -> 0.5 g
    assert eco.parse_level_gold("") == {} and eco.parse_level_gold("x,1:5,10:0,10:abc,300:5") == {}
    assert eco.parse_level_gold(" 20 : 7 , 30:8") == {20: 7, 30: 8}
    assert eco.is_random_bot("RNDBOT123", 1, "rndbot") and eco.is_random_bot("rndlv7", 1, "rndlv")
    assert not eco.is_random_bot("RNDBOT123", 2, "rndbot")            # addclass
    assert not eco.is_random_bot("RNDBOT123", 0, "rndbot")            # no longer random
    assert not eco.is_random_bot("RNDBOT123", -1, "rndbot")           # no type row
    assert not eco.is_random_bot("SQUID", 1, "rndbot")                # a player's account, even typed 1
    assert not eco.is_random_bot("RNDLV1", 1, "rndbot")               # another realm's bot
    assert not eco.is_random_bot("RNDBOT1", 1, "")
    assert eco.db_name("127.0.0.1;3307;acore;secret;acore_characters") == "acore_characters"
    # mysql.exe sometimes answers nothing (exit 0): a read retries until the witness comes, else fails clearly.
    eco.TRIES, calls = 3, []
    def silent(sql):
        calls.append(sql)
        return ""
    try:
        eco.read(silent, "SELECT 1;\n", "a test read")
        raise AssertionError("an answer without its witness was taken")
    except RuntimeError as error:
        assert "no complete answer" in str(error) and len(calls) == 3
    answers = iter(["", "1\nCOA_FIN\n"])
    assert eco.read(lambda sql: next(answers), "SELECT 1;\n", "a test read") == [["1"]]
    assert eco.read(lambda sql: "COA_FIN\n", "SELECT 1;\n", "a test read") == []
    eco.TRIES = 5
    print("rules: OK")


# ------------------------------------------------------------------------------------------- private MySQL
PORT = 3399


def start_mysql(base, data):
    subprocess.run([str(base / "bin/mysqld.exe"), "--no-defaults", f"--basedir={base}", f"--datadir={data}",
                    "--initialize-insecure"], check=True, capture_output=True)
    proc = subprocess.Popen([str(base / "bin/mysqld.exe"), "--no-defaults", f"--basedir={base}", f"--datadir={data}",
                             f"--port={PORT}", "--bind-address=127.0.0.1", "--mysqlx=OFF",
                             f"--socket=coa_eco_test", "--named-pipe=OFF", "--shared-memory=OFF"],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=eco.HIDDEN)
    return proc


def runner(base):
    def run(sql):
        result = subprocess.run([str(base / "bin/mysql.exe"), "--no-defaults", "-h127.0.0.1", f"-P{PORT}", "-uroot",
                                 "-N", "-B", "--default-character-set=utf8mb4"], input=sql.encode(), capture_output=True,
                                timeout=120)
        if result.returncode:
            raise RuntimeError(result.stderr.decode(errors="replace"))
        return result.stdout.decode()
    return run


def seed(run, chars, pbots, botprefix):
    p = botprefix.upper()
    run(f"""
CREATE DATABASE IF NOT EXISTS acore_auth;
CREATE TABLE IF NOT EXISTS acore_auth.account (id INT UNSIGNED PRIMARY KEY, username VARCHAR(32) NOT NULL);
INSERT IGNORE INTO acore_auth.account VALUES (1,'SQUID'),(2,'RNDBOT1'),(3,'RNDBOT2'),(4,'RNDBOT3'),(5,'RNDLV1'),(6,'RNDLV2');
CREATE DATABASE {pbots};
CREATE TABLE {pbots}.playerbots_account_type (account_id INT UNSIGNED PRIMARY KEY, account_type INT UNSIGNED NOT NULL);
INSERT INTO {pbots}.playerbots_account_type VALUES (1,1),(2,1),(3,2),(4,0),(5,1),(6,2);
CREATE DATABASE {chars};
USE {chars};
CREATE TABLE characters (guid INT UNSIGNED PRIMARY KEY, account INT UNSIGNED, name VARCHAR(12), level TINYINT UNSIGNED,
  money INT UNSIGNED NOT NULL DEFAULT 0);
CREATE TABLE guild (guildid INT UNSIGNED PRIMARY KEY, BankMoney BIGINT UNSIGNED NOT NULL);
CREATE TABLE guild_member (guildid INT UNSIGNED, guid INT UNSIGNED PRIMARY KEY);
CREATE TABLE item_instance (guid INT UNSIGNED PRIMARY KEY, itemEntry MEDIUMINT UNSIGNED);
CREATE TABLE auctionhouse (id INT UNSIGNED PRIMARY KEY, houseid TINYINT UNSIGNED NOT NULL DEFAULT 7,
  itemguid INT UNSIGNED NOT NULL, itemowner INT UNSIGNED NOT NULL, buyoutprice INT UNSIGNED NOT NULL DEFAULT 0,
  time INT UNSIGNED NOT NULL DEFAULT 0, buyguid INT UNSIGNED NOT NULL DEFAULT 0, lastbid INT UNSIGNED NOT NULL DEFAULT 0,
  startbid INT UNSIGNED NOT NULL DEFAULT 0, deposit INT UNSIGNED NOT NULL DEFAULT 0);
CREATE TABLE mail (id INT UNSIGNED PRIMARY KEY, messageType TINYINT UNSIGNED, stationery TINYINT, mailTemplateId SMALLINT
  UNSIGNED, sender INT UNSIGNED, receiver INT UNSIGNED, subject LONGTEXT, body LONGTEXT, has_items TINYINT UNSIGNED,
  expire_time INT UNSIGNED, deliver_time INT UNSIGNED, money INT UNSIGNED, cod INT UNSIGNED, checked TINYINT UNSIGNED);
INSERT INTO characters VALUES
  (10,1,'Main',60,50000000),(11,1,'Alt',60,30000000),
  (20,{2 if p == 'RNDBOT' else 5},'Rich',60,20000000),(21,{2 if p == 'RNDBOT' else 5},'Poor',10,1000),
  (22,{2 if p == 'RNDBOT' else 5},'Guildy',60,10000000),
  (30,{3 if p == 'RNDBOT' else 6},'Addclass',60,9000000),(40,4,'Retired',60,9000000),
  (50,{5 if p == 'RNDBOT' else 2},'Other',60,9000000);
INSERT INTO guild VALUES (1,70000000);
INSERT INTO guild_member VALUES (1,10),(1,22);
INSERT INTO item_instance VALUES (500,2589);
INSERT INTO auctionhouse (id,itemguid,itemowner,buyoutprice,buyguid,lastbid,deposit) VALUES
  (100,900,999,100,0,0,5),(101,901,999,9000,10,5000,5),(102,500,20,300,0,0,5);
INSERT INTO mail VALUES (7,0,41,0,1,10,'hi','',0,0,0,0,0,0);
""")


def write_repack(root):
    def put(rel, text):
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    put("CoA-Bots/Core/configs/modules/playerbots.conf.dist",
        f'AiPlayerbot.RandomBotAccountPrefix = "rndbot"\nAiPlayerbot.CoaEconomy.BotLevelGold = "{SCALE}"\n')
    put("CoA-Bots/Core/configs/worldserver.conf",
        'LoginDatabaseInfo = "127.0.0.1;3399;acore;pw;acore_auth"\n'
        'CharacterDatabaseInfo = "127.0.0.1;3399;acore;pw;acore_characters"\n')
    put("CoA-Bots/Core/configs/modules/playerbots.conf",            # no BotLevelGold: the .dist value
        'PlayerbotsDatabaseInfo = "127.0.0.1;3399;acore;pw;acore_playerbots"\n')
    put("Royaumes/living/configs/worldserver.conf",
        'LoginDatabaseInfo = "127.0.0.1;3399;acore;pw;acore_auth"\n'
        'CharacterDatabaseInfo = "127.0.0.1;3399;acore;pw;acore_characters_living"\n')
    put("Royaumes/living/configs/modules/playerbots.conf",          # the player's own scale
        'PlayerbotsDatabaseInfo = "127.0.0.1;3399;acore;pw;acore_playerbots_living"\n'
        'AiPlayerbot.RandomBotAccountPrefix = "rndlv"\nAiPlayerbot.CoaEconomy.BotLevelGold = "10:100000,60:200000"\n')
    put("Royaumes/wildcard/configs/worldserver.conf",
        'LoginDatabaseInfo = "127.0.0.1;3399;acore;pw;acore_auth"\n'
        'CharacterDatabaseInfo = "127.0.0.1;3399;acore;pw;acore_characters_wildcard"\n')
    put("Royaumes/wildcard/configs/modules/playerbots.conf",        # emptied by the player: nothing
        'PlayerbotsDatabaseInfo = "127.0.0.1;3399;acore;pw;acore_playerbots_wildcard"\n'
        'AiPlayerbot.RandomBotAccountPrefix = "rndwc"\nAiPlayerbot.CoaEconomy.BotLevelGold = ""\n')


def money(real, db):
    return {int(g): int(m) for g, m in eco.rows(real(f"SELECT guid, money FROM {db}.characters;\n"))}


def test_mysql(base):
    base = Path(base)
    tmp = Path(tempfile.mkdtemp(prefix="coa_eco_", dir=str(base)))
    proc = start_mysql(base, tmp / "data")
    try:
        real = runner(base)
        count = [0]

        def flaky(sql):
            """The script runs, but one answer in two is lost (exit 0, no output), as mysql.exe 8.4 sometimes does."""
            out = real(sql)
            count[0] += 1
            return "" if count[0] % 2 else out
        run = flaky
        for _ in range(60):
            try:
                real("SELECT 1;\n")
                break
            except RuntimeError:
                time.sleep(1)
        seed(real, "acore_characters", "acore_playerbots", "rndbot")
        seed(real, "acore_characters_living", "acore_playerbots_living", "rndlv")
        real("CREATE DATABASE acore_playerbots_wildcard; CREATE DATABASE acore_characters_wildcard;\n"
            "CREATE TABLE acore_characters_wildcard.auctionhouse LIKE acore_characters.auctionhouse;\n"
            "CREATE TABLE acore_characters_wildcard.item_instance LIKE acore_characters.item_instance;\n"
            "CREATE TABLE acore_characters_wildcard.mail LIKE acore_characters.mail;\n"
            "CREATE TABLE acore_characters_wildcard.characters LIKE acore_characters.characters;\n"
            "INSERT INTO acore_characters_wildcard.characters VALUES (20,2,'Rich',60,20000000);\n")
        repack, backup = tmp / "repack", tmp / "backup"
        write_repack(repack)
        found = eco.realms(repack)
        assert [r["key"] for r in found] == ["coa", "living", "wildcard"], found
        assert found[2]["levelGold"] is None and found[1]["prefix"] == "rndlv"
        journal, lines = {}, []
        eco.run_steps(repack, backup, journal, lambda kind, text: lines.append(text), lambda *_: None, run=run)

        coa = money(real, "acore_characters")
        assert coa[10] == 50000000 and coa[11] == 30000000          # the player's main and alt
        assert coa[20] == 1005000 and coa[22] == 1005000            # random bots, the mixed guild's one too
        assert coa[21] == 1000                                      # poorer than the scale: kept
        assert coa[30] == 9000000 and coa[40] == 9000000            # type 2, type 0
        assert coa[50] == 9000000                                   # another realm's bot account (RNDLV1)
        living = money(real, "acore_characters_living")
        assert living[20] == 300000 and living[22] == 300000 and living[21] == 1000   # its own scale: 10 g + 20 g
        assert living[50] == 9000000 and living[10] == 50000000 and living[30] == 9000000
        assert money(real, "acore_characters_wildcard")[20] == 20000000  # scale emptied: nothing
        for db in ("acore_characters", "acore_characters_living"):
            assert eco.rows(real(f"SELECT BankMoney FROM {db}.guild;\n")) == [["70000000"]]
            assert eco.rows(real(f"SELECT id FROM {db}.auctionhouse ORDER BY id;\n")) == [["102"]]
            mails = eco.rows(real(f"SELECT id, receiver, money, subject, messageType, checked FROM {db}.mail ORDER BY id;\n"))
            assert mails == [["7", "10", "0", "hi", "0", "0"], ["8", "10", "5000", "0:0:4:101:1", "2", "4"]], mails
        assert journal["economy"]["coa"]["phantomAuctions"] == [2, 1, 5000]
        assert journal["economy"]["coa"]["botsGold"] == [3, 2, 30001000, 2011000]
        assert journal["economy"]["wildcard"]["botsGold"] == "no-scale"
        saved = json.loads((backup / "economy/coa-bots-gold.json").read_text())
        assert sorted(r[0] for r in saved["rows"]) == [20, 22] and saved["rows"][0][3] in (20000000, 10000000)
        assert len(json.loads((backup / "economy/living-phantom-auctions.json").read_text())["rows"]) == 2

        # Once: a new update (new journal) finds the marks and changes nothing.
        real("UPDATE acore_characters.characters SET money = 5000000 WHERE guid = 20;\n"
            "INSERT INTO acore_characters.auctionhouse (id,itemguid,itemowner) VALUES (103,903,999);\n")
        again = {}
        eco.run_steps(repack, backup, again, lambda *_: None, lambda *_: None, run=run)
        assert money(real, "acore_characters")[20] == 5000000
        assert eco.rows(real("SELECT COUNT(*) FROM acore_characters.auctionhouse;\n")) == [["2"]]
        assert again["economy"]["coa"] == {"phantomAuctions": "done before", "botsGold": "done before"}
        # A cut journal: the step already marked is not run twice either.
        again["economy"]["coa"].pop("botsGold")
        eco.run_steps(repack, backup, again, lambda *_: None, lambda *_: None, run=run)
        assert money(real, "acore_characters")[20] == 5000000
        # A mark read wrong (its answer lost every time but not the witness): the step's writes are gated on its
        # INSERT IGNORE and change nothing.
        real("UPDATE acore_characters_living.characters SET money = 7000000 WHERE guid = 20;\n"
             "DELETE FROM acore_playerbots_living.coa_update_marks WHERE name = 'botsGold';\n"
             "INSERT INTO acore_playerbots_living.coa_update_marks VALUES ('botsGold', NOW(), "
             "'bots=0 lowered=0 before=0 after=0');\n")
        hidden = [True]

        def hide_mark(sql):
            if hidden[0] and "coa_update_marks` WHERE `name` = 'botsGold'" in sql:
                hidden[0] = False
                real(sql)
                return "COA_FIN\n"
            return real(sql)
        living_realm = [r for r in found if r["key"] == "living"][0]
        assert eco.bots_gold(hide_mark, living_realm, backup) is None
        assert money(real, "acore_characters_living")[20] == 7000000
        print(f"mysql: OK ({count[0]} calls, one answer in two lost)")
        for line in lines:
            print("  " + line)
    finally:
        try:
            subprocess.run([str(base / "bin/mysqladmin.exe"), "--no-defaults", "-h127.0.0.1", f"-P{PORT}", "-uroot",
                            "shutdown"], capture_output=True, timeout=60)
            proc.wait(timeout=60)
        except Exception:  # noqa: BLE001
            proc.kill()
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    test_rules()
    if "--mysql" in sys.argv:
        test_mysql(sys.argv[sys.argv.index("--mysql") + 1])

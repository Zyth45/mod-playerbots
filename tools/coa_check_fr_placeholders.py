"""Checks that every French text keeps the placeholders (%name) of its English text, in the playerbots SQL.
- ai_playerbot_texts: text (EN) vs text_loc2 (FR), row by row, the last INSERT/REPLACE/UPDATE of an id wins.
- CoA Live bank and night bank: rows are not translations of each other, so a FR line may only use placeholders
  that the EN lines of the same situation use.
Prints the faulty rows; with --sql OUT writes UPDATEs that fix ai_playerbot_texts rows whose FR placeholder is a
translated English one (mapped back by position)."""
import glob, os, re, sys
from collections import defaultdict

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "sql", "playerbots")
PH = re.compile(r"%[^\s,.!?;:'\"()\[\]{}<>/|]+")
COLS_TEXTS = ["id", "name", "text", "say_type", "reply_type"] + ["text_loc%d" % i for i in range(1, 9)]


def tuples(body):
    """The value tuples of an INSERT body: lists of python values (str or number text)."""
    out, i, n = [], 0, len(body)
    while i < n:
        if body[i] != "(":
            i += 1
            continue
        i += 1
        row, cur, s = [], None, False
        while i < n:
            c = body[i]
            if s:
                if c == "\\" and i + 1 < n:
                    cur += body[i + 1]; i += 2; continue
                if c == "'":
                    if i + 1 < n and body[i + 1] == "'":
                        cur += "'"; i += 2; continue
                    s = False; i += 1; continue
                cur += c; i += 1; continue
            if c == "'":
                s = True; cur = "" if cur is None else cur; i += 1; continue
            if c == ",":
                row.append(cur.strip() if isinstance(cur, str) else cur); cur = None; i += 1; continue
            if c == ")":
                row.append(cur.strip() if isinstance(cur, str) else cur); out.append(row); i += 1; break
            cur = (cur or "") + c; i += 1
    return out


def statements(text):
    """INSERT/REPLACE statements: (table, columns or None, body)."""
    for m in re.finditer(r"(?:INSERT(?:\s+IGNORE)?|REPLACE)\s+INTO\s+`?(\w+)`?\s*(\(([^)]*)\))?\s*VALUES\s*(.*?);\s*$",
                         text, re.S | re.M | re.I):
        cols = [c.strip(" `") for c in m.group(3).split(",")] if m.group(3) else None
        yield m.group(1), cols, m.group(4)


def phs(s):
    return sorted(PH.findall(s or ""))


def main():
    files = sorted(glob.glob(os.path.join(ROOT, "base", "*.sql"))) + sorted(glob.glob(os.path.join(ROOT, "updates", "*.sql"))) \
        + sorted(glob.glob(os.path.join(ROOT, "custom", "*.sql")))
    texts = {}
    live = defaultdict(lambda: defaultdict(list))   # situation -> locale -> texts
    night = defaultdict(lambda: defaultdict(list))  # (channel, event) -> locale -> texts
    for f in files:
        t = open(f, encoding="utf-8", errors="replace").read()
        for table, cols, body in statements(t):
            if table == "ai_playerbot_texts":
                cols = cols or COLS_TEXTS
                for row in tuples(body):
                    if len(row) != len(cols):
                        continue
                    r = dict(zip(cols, row))
                    texts[r.get("id") or ("n:" + r.get("name", "") + ":" + r.get("text", ""))] = r
            elif table == "playerbots_coa_live_lines" and cols:
                for row in tuples(body):
                    if len(row) == len(cols):
                        r = dict(zip(cols, row))
                        live[r["situation"]][r["locale"]].append(r["text"])
            elif table == "playerbots_coa_night_lines" and cols:
                for row in tuples(body):
                    if len(row) == len(cols):
                        r = dict(zip(cols, row))
                        night[(r["channel"], r["event"])][r["locale"]].append(r["text"])
        for m in re.finditer(r"UPDATE\s+`?ai_playerbot_texts`?\s+SET\s+`?text_loc2`?\s*=\s*'((?:[^'\\]|\\.|'')*)'\s+WHERE\s+`?id`?\s*=\s*(\d+)", t, re.I):
            if m.group(2) in texts:
                texts[m.group(2)]["text_loc2"] = m.group(1).replace("''", "'")

    bad_texts = []
    for i, r in texts.items():
        fr = r.get("text_loc2") or ""
        if fr and set(PH.findall(fr)) - set(PH.findall(r.get("text") or "")):
            bad_texts.append((i, r.get("name"), r.get("text"), fr))
    print("ai_playerbot_texts rows:", len(texts), "FR with other placeholders:", len(bad_texts))
    for b in bad_texts[:200]:
        print("  id %s %s\n    EN %s\n    FR %s" % b)

    for label, bank in (("CoA Live bank", live), ("night bank", night)):
        bad = []
        for key, by in bank.items():
            allowed = set(p for t in by.get("0", []) for p in PH.findall(t))
            for t in by.get("2", []):
                extra = set(PH.findall(t)) - allowed
                if extra and by.get("0"):
                    bad.append((key, sorted(extra), t))
        print(label, "FR lines with a placeholder no EN line of the situation has:", len(bad))
        for b in bad[:50]:
            print("  ", b)

    if "--sql" in sys.argv:
        out = sys.argv[sys.argv.index("--sql") + 1]
        lines = ["-- CoA: French texts of ai_playerbot_texts whose placeholders were translated (\"%Quête\" for",
                 "-- %quest_link): each FR placeholder that is not one of the EN text's is set back to the EN one at the",
                 "-- same place. Rows whose placeholders cannot be matched one to one are left for a review.", ""]
        fixed = 0
        for i, name, en, fr in bad_texts:
            e, f = PH.findall(en), PH.findall(fr)
            if len(e) != len(f):
                continue
            new = fr
            for a, b in zip(f, e):
                if a != b:
                    new = new.replace(a, b, 1)
            if phs(new) != phs(en):
                continue
            lines.append("UPDATE `ai_playerbot_texts` SET `text_loc2` = '%s' WHERE `id` = %s;" %
                         (new.replace("\\", "\\\\").replace("'", "''"), i))
            fixed += 1
        open(out, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
        print("fix SQL:", fixed, "rows ->", out)


main()

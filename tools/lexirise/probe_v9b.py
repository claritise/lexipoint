#!/usr/bin/env python3
"""What v0.2 V9b's A5 (above-level marks, page-annotations.md §2) is decided on, measured read-only from the Mac:

1. analyze/text today (one call, text written for this probe): does entryMetaById or an occurrence carry a level
   (system_tags, JLPT/HSK) now? (2026-09-24: no.)
2. GET /v1/vocabulary (read-only pages, both languages): does each item carry the dictionary entry's system tags at
   its top level (`dictionary_entry_system_tags`, which the mirror's visitor could keep), and how many saved words
   have a JLPT/HSK tag? Only tag values and counts are printed (no ids).
3. Rank as a proxy for the level: every word-like entry on V7b's two probe pages (research/v7b, written for that
   probe) looked up by its lemma (dictionary/lookup, one kept-alive connection), its tag against its rank; how well a
   rank threshold says "above a target level" (N3, N2 / HSK-3, HSK-4, HSK-5); whether the lookup's entry is the
   page's lemma entry (so a level could be cached by entry id).
4. Offline (no calls): on V7b's pages and a real volume's pages (research/spike/api-cache, private), how many
   word-like occurrences and distinct entries a page has, and how many are rarer than each rank threshold: what A1
   marks today against what A5 would leave.

Key from ~/.lexirise_key (never printed). Raw answers go to research/v9b/ (gitignored); the summary printed is what
the docs record (lexirise-api-notes.md, "Levels for A5 (V9b), measured").

  python3 tools/lexirise/probe_v9b.py            # 1-4 (~250 calls)
  python3 tools/lexirise/probe_v9b.py --only 4   # 4 alone, no network
"""

import glob
import http.client
import json
import os
import sys
import time

HOST = "api.lexirise.app"
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
OUT = os.path.join(ROOT, "research", "v9b")
PROBE_TEXT = ("ja", "駅前の古い本屋で、表紙の擦り切れた辞書を見つけた。値段は驚くほど安かった。")
THRESHOLDS = (1000, 2000, 3000, 5000, 8000, 12000, 20000)
JA_ORDER = ["JLPT-N5", "JLPT-N4", "JLPT-N3", "JLPT-N2", "JLPT-N1"]
ZH_ORDER = ["HSK-1", "HSK-2", "HSK-3", "HSK-4", "HSK-5", "HSK-6", "HSK-7+"]


def load_key():
    with open(os.path.expanduser("~/.lexirise_key")) as f:
        return f.read().strip()


class Api:
    def __init__(self, key):
        self.key, self.conn, self.calls = key, None, 0

    def req(self, method, path, body=None):
        for attempt in range(2):
            if self.conn is None:
                self.conn = http.client.HTTPSConnection(HOST, timeout=60)
            try:
                data = None if body is None else json.dumps(body, ensure_ascii=False).encode()
                self.conn.request(method, path, body=data, headers={"Authorization": f"Bearer {self.key}",
                                                                    "Content-Type": "application/json"})
                r = self.conn.getresponse()
                raw = r.read()
                self.calls += 1
                if r.status != 200:
                    raise RuntimeError(f"{method} {path.split('?')[0]}: HTTP {r.status}")
                return json.loads(raw)
            except (OSError, http.client.HTTPException):
                self.conn = None
                if attempt:
                    raise


def level_of(tags):
    return next((t for t in (tags or []) if t.startswith(("JLPT-", "HSK-"))), "")


def analyze_today(api, raw):
    lang, text = PROBE_TEXT
    d = api.req("POST", "/v1/analyze/text", {"text": text, "language": lang})
    raw["analyze"] = d
    meta = next(iter((d.get("entryMetaById") or {}).values()), {})
    occ = (d.get("occurrences") or [{}])[0]
    found = [k for k in list(meta) + list(occ) if "tag" in k.lower() or "level" in k.lower() or "jlpt" in k.lower()]
    print(f"1. analyze/text: meta keys {sorted(meta)}; occurrence keys {sorted(occ)}; level-like keys {found}")
    return dict(meta_keys=sorted(meta), occ_keys=sorted(occ), level_keys=found)


def vocabulary(api, raw):
    out = {}
    for lang in ("ja", "zh"):
        items, offset = [], 0
        while True:
            d = api.req("GET", f"/v1/vocabulary?language={lang}&limit=200&offset={offset}")
            items += d.get("items") or []
            offset = d.get("nextOffset")
            if not offset or not d.get("items"):
                break
        raw[f"vocab-{lang}"] = items
        words = [i for i in items if i.get("unit_type", "word") == "word"]
        top = sum(1 for i in words if "dictionary_entry_system_tags" in i)
        same = sum(1 for i in words if (i.get("dictionary_entry_system_tags") or []) ==
                   ((i.get("dictionary_entry") or {}).get("system_tags") or []))
        levels = {}
        for i in words:
            lv = level_of(i.get("dictionary_entry_system_tags")) or "(none)"
            levels[lv] = levels.get(lv, 0) + 1
        out[lang] = dict(items=len(items), words=len(words), top_level_field=top, equal_to_embedded=same,
                         levels=levels)
        print(f"2. vocabulary {lang}: {out[lang]}")
    return out


def page_entries(page):
    """Word-like occurrences' lemma entries: {entryId: (lemma text, rank)} in first-seen order."""
    meta = {int(k): v for k, v in (page.get("entryMetaById") or {}).items()}
    seen = {}
    for o in page.get("occurrences") or []:
        if not o.get("isWordLike"):
            continue
        e = o.get("lemmaEntryId") or o.get("entryId")
        if e is None or e in seen:
            continue
        seen[e] = ((o.get("lemma") or o.get("word")), (meta.get(e) or {}).get("rank"))
    return seen


def rank_vs_level(api, raw):
    with open(os.path.join(ROOT, "research", "v7b", "probe-v7b-2026-09-28.json")) as f:
        pages = json.load(f)["raw"]
    rows = []
    for lang in ("ja", "zh"):
        for e, (text, rank) in page_entries(pages[f"{lang}-first"]).items():
            d = api.req("POST", "/v1/dictionary/lookup", {"text": text, "language": lang})
            raw[f"lookup-{lang}-{text}"] = d
            rows.append(dict(lang=lang, text=text, entry=e, page_rank=rank, lookup_entry=d.get("entry_id") or d.get("id"),
                             rank=d.get("rank"), level=level_of(d.get("system_tags"))))
    summary = {}
    for lang, order in (("ja", JA_ORDER), ("zh", ZH_ORDER)):
        rs = [r for r in rows if r["lang"] == lang]
        same_entry = sum(1 for r in rs if r["lookup_entry"] == r["entry"])
        by = {}
        for r in rs:
            by.setdefault(r["level"] or "(none)", []).append(r["rank"] or 0)
        dist = {k: dict(n=len(v), min=min(v), median=sorted(v)[len(v) // 2], max=max(v))
                for k, v in sorted(by.items(), key=lambda kv: (order + ["(none)"]).index(kv[0]))}
        print(f"3. {lang}: {len(rs)} entries, lookup entry == page lemma entry for {same_entry}")
        for k, v in dist.items():
            print(f"   {k:8} {v}")
        targets = {}
        for target in (order[2:4] if lang == "ja" else order[2:5]):
            above = set(order[order.index(target) + 1:])
            tagged = [r for r in rs if r["level"]]
            truth = [r["level"] in above for r in tagged]
            best = None
            for t in THRESHOLDS:
                guess = [(r["rank"] or 0) > t for r in tagged]
                tp = sum(1 for g, y in zip(guess, truth) if g and y)
                fp = sum(1 for g, y in zip(guess, truth) if g and not y)
                fn = sum(1 for g, y in zip(guess, truth) if not g and y)
                agree = sum(1 for g, y in zip(guess, truth) if g == y) / len(tagged)
                row = dict(threshold=t, agree=round(agree, 2), above_marked=tp, below_marked=fp, above_missed=fn)
                if best is None or agree > best["agree"]:
                    best = row
            targets[target] = dict(tagged=len(tagged), truly_above=sum(truth), best=best)
            print(f"   target {target}: {targets[target]}")
        untagged = sorted(r["rank"] or 0 for r in rs if not r["level"])
        summary[lang] = dict(entries=len(rs), same_entry=same_entry, dist=dist, targets=targets,
                             untagged=len(untagged), untagged_median_rank=untagged[len(untagged) // 2] if untagged else None,
                             untagged_examples=[r["text"] for r in rs if not r["level"]][:12])
        print(f"   untagged: {len(untagged)}, median rank {summary[lang]['untagged_median_rank']}, "
              f"e.g. {summary[lang]['untagged_examples']}")
    return rows, summary


def offline_counts():
    sets = {}
    with open(os.path.join(ROOT, "research", "v7b", "probe-v7b-2026-09-28.json")) as f:
        raw = json.load(f)["raw"]
    sets["v7b ja page"] = [raw["ja-first"]]
    sets["v7b zh page"] = [raw["zh-first"]]
    spike = sorted(glob.glob(os.path.join(ROOT, "research", "spike", "api-cache", "page*.json")))
    if spike:
        pages = []
        for p in spike:
            with open(p) as f:
                pages.append(json.load(f))
        sets[f"manga volume ({len(pages)} pages, ja)"] = pages
    out = {}
    for name, pages in sets.items():
        occ_n, ent_n, over = [], [], {t: [] for t in THRESHOLDS}
        for page in pages:
            meta = {int(k): v for k, v in (page.get("entryMetaById") or {}).items()}
            ents = {}
            n = 0
            for o in page.get("occurrences") or []:
                if not o.get("isWordLike"):
                    continue
                n += 1
                e = o.get("lemmaEntryId") or o.get("entryId")
                ents[e] = (meta.get(e) or meta.get(o.get("entryId")) or {}).get("rank") or 0
            occ_n.append(n)
            ent_n.append(len(ents))
            for t in THRESHOLDS:
                over[t].append(sum(1 for r in ents.values() if r > t))
        mean = lambda xs: round(sum(xs) / len(xs), 1) if xs else 0
        out[name] = dict(wordlike_per_page=mean(occ_n), entries_per_page=mean(ent_n),
                         rarer_than={t: mean(v) for t, v in over.items()})
        print(f"4. {name}: {out[name]}")
    return out


def main():
    only4 = "--only" in sys.argv and "4" in sys.argv
    os.makedirs(OUT, exist_ok=True)
    summary = {}
    if not only4:
        api = Api(load_key())
        raw = {}
        t = time.monotonic()
        summary["analyze"] = analyze_today(api, raw)
        summary["vocabulary"] = vocabulary(api, raw)
        rows, summary["rank"] = rank_vs_level(api, raw)
        raw["rows"] = rows
        print(f"   {api.calls} calls in {time.monotonic() - t:.0f} s")
        with open(os.path.join(OUT, f"probe-v9b-{time.strftime('%Y-%m-%d')}.json"), "w") as f:
            json.dump(dict(raw=raw, summary=summary), f, ensure_ascii=False, indent=1)
    summary["offline"] = offline_counts()
    with open(os.path.join(OUT, "summary.json"), "w") as f:
        json.dump(summary, f, ensure_ascii=False, indent=1)


if __name__ == "__main__":
    main()

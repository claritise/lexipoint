#!/usr/bin/env python3
"""What v0.2 V7c (C21's caches, 00-overview.md) is decided on, measured read-only from the Mac:

1. dictionary/lookup (phase B): its answer's size, and what the card keeps of it; its time on a fresh connection
   (TCP + TLS from the Mac) and on a kept-alive one (the server's own time plus one round trip); the same word again.
2. Whether a lookup's answer changes over days: today's answers against the ones kept in research/v4 (2026-09-27).
3. TLS: the server's protocol, whether it hands out session tickets and accepts them (resumption), the ticket's
   lifetime, and a full handshake's time against a resumed one's (the Mac's; the device's CPU share is a device check).
4. The server's keep-alive: how long an idle HTTP connection stays open (HEAD /, no key).
5. Lemma repetition: how often a text repeats its lemmas, from analyze/text answers already on disk (one JSON per
   page, in reading order; default research/spike/api-cache, a real volume's pages, private and gitignored). Counts
   word-like occurrences by lemma entry, for rarer words (rank above a threshold: the words a reader taps) and for
   all words (stepping on a card), over the whole text and over chapter-sized windows.

Key from ~/.lexirise_key (never printed). Raw answers go to research/v7c/ (gitignored); the summary printed is what the
docs record (lexirise-api-notes.md, "Caches (V7c), measured").

  python3 tools/lexirise/probe_v7c.py                 # 1-5
  python3 tools/lexirise/probe_v7c.py --only 5 [dir]  # 5 alone, no network
"""

import glob
import http.client
import json
import os
import socket
import ssl
import sys
import threading
import time

HOST = "api.lexirise.app"
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
OUT = os.path.join(ROOT, "research", "v7c")

# Common and rarer words, chosen for this probe (not from any book).
WORDS = [("ja", w) for w in ("勉強", "灯台", "喫茶店", "煩わしい", "懐かしい", "筆跡", "霧", "振り子", "見比べる",
                             "書きこむ")] + \
        [("zh", w) for w in ("学习", "照相馆", "沉默寡言", "眼镜", "屋檐", "似曾相识", "选择", "地图册")]
KEEPALIVE_WAITS_S = (5, 20, 40, 55, 65, 70, 80, 100, 130)
RANKS = (3000, 10000)
TAP_AGAIN = (1.0, 0.5, 0.25)  # the share of a rare word's later occurrences the reader taps again
CHAPTER_PAGES = 35


def load_key():
    with open(os.path.expanduser("~/.lexirise_key")) as f:
        return f.read().strip()


def post(conn, key, path, body):
    data = json.dumps(body, ensure_ascii=False).encode()
    t = time.monotonic()
    conn.request("POST", path, body=data, headers={"Authorization": f"Bearer {key}",
                                                   "Content-Type": "application/json"})
    r = conn.getresponse()
    ttfb = time.monotonic() - t
    raw = r.read()
    total = time.monotonic() - t
    if r.status != 200:
        raise RuntimeError(f"POST {path}: HTTP {r.status}")
    return ttfb, total, raw


def kept(d):
    """What the card keeps of an answer (api::LookupResult): word, reading, 2 senses, level, rank, frequency."""
    senses = [t for t in (d.get("translations") or []) if t.get("translation")][:2]
    level = next((t for t in (d.get("system_tags") or []) if t.startswith(("JLPT-", "HSK-"))), "")
    return dict(word=d.get("word"), reading=d.get("transliteration"),
                senses=[(s["translation"], (s.get("part_of_speech") or [""])[0]) for s in senses],
                level=level, rank=d.get("rank"), frequency=d.get("frequency_score"),
                ready=d.get("translation_status") == "ready")


def kept_bytes(k):
    strings = [k["word"] or "", k["reading"] or "", k["level"]] + [x for s in k["senses"] for x in s]
    return sum(len(s.encode()) for s in strings) + 16  # + rank, frequency, flags, lengths


def lookups(key, raw):
    print("1. dictionary/lookup")
    rows = []
    for lang, w in WORDS:  # a fresh connection each: the device's cold card
        conn = http.client.HTTPSConnection(HOST, timeout=60)
        t = time.monotonic()
        conn.connect()
        tls = time.monotonic() - t
        ttfb, total, body = post(conn, key, "/v1/dictionary/lookup", {"text": w, "language": lang})
        conn.close()
        d = json.loads(body)
        raw[f"lookup-{lang}-{w}"] = d
        k = kept(d)
        rows.append(dict(lang=lang, word=w, connect_s=tls, ttfb=ttfb, total=total, bytes=len(body),
                         kept=kept_bytes(k), ready=k["ready"], status=d.get("status")))
    conn = http.client.HTTPSConnection(HOST, timeout=60)  # one kept-alive connection: the server's time + an RTT
    conn.request("HEAD", "/")
    conn.getresponse().read()
    t = time.monotonic()
    conn.request("HEAD", "/")
    conn.getresponse().read()
    rtt = time.monotonic() - t
    for row in rows:
        row["warm_ttfb"], row["warm_total"], _ = post(conn, key, "/v1/dictionary/lookup",
                                                      {"text": row["word"], "language": row["lang"]})
        row["again_total"] = post(conn, key, "/v1/dictionary/lookup",
                                  {"text": row["word"], "language": row["lang"]})[1]
    conn.close()
    for r in rows:
        print(f"  {r['lang']} {r['word']:6} {r['bytes']:6} B (kept {r['kept']:4} B) ready {r['ready']!s:5} "
              f"fresh: connect {r['connect_s']:.2f} s + {r['total']:.2f} s; kept-alive {r['warm_total']:.2f} s, "
              f"again {r['again_total']:.2f} s")
    med = lambda xs: sorted(xs)[len(xs) // 2]
    summary = dict(rtt_head=rtt, median_bytes=med([r["bytes"] for r in rows]), max_bytes=max(r["bytes"] for r in rows),
                   median_kept=med([r["kept"] for r in rows]), max_kept=max(r["kept"] for r in rows),
                   median_connect=med([r["connect_s"] for r in rows]), median_fresh=med([r["total"] for r in rows]),
                   median_warm=med([r["warm_total"] for r in rows]), median_again=med([r["again_total"] for r in rows]))
    print(f"  kept-alive HEAD / round trip {rtt:.3f} s; medians: {summary}")
    return rows, summary


def stability(key, raw):
    print("2. The same lookups against research/v4 (2026-09-27)")
    changed = {}
    for name in ("compound-lookups-2026-09-27.json", "compound-lookups2-2026-09-27.json"):
        path = os.path.join(ROOT, "research", "v4", name)
        if not os.path.exists(path):
            continue
        with open(path) as f:
            old = json.load(f)
        for w in old:
            lang = old[w].get("lang") or "ja"
            conn = http.client.HTTPSConnection(HOST, timeout=60)
            _, _, body = post(conn, key, "/v1/dictionary/lookup", {"text": w, "language": lang})
            conn.close()
            new = json.loads(body)
            raw[f"again-{w}"] = new
            a, b = kept(old[w]), kept(new)
            diff = [f for f in a if a[f] != b[f]]
            if old[w].get("id") != new.get("id"):
                diff.append("id")
            changed[w] = diff
            print(f"  {w}: {'same' if not diff else 'changed: ' + ', '.join(diff)} "
                  f"(status {old[w].get('status')} -> {new.get('status')}, "
                  f"translation {old[w].get('translation_status')} -> {new.get('translation_status')})")
    return changed


def handshake(ctx, session=None):
    sock = socket.create_connection((HOST, 443), timeout=20)
    t = time.monotonic()
    s = ctx.wrap_socket(sock, server_hostname=HOST, session=session)
    took = time.monotonic() - t
    s.sendall(f"HEAD / HTTP/1.1\r\nHost: {HOST}\r\nConnection: close\r\n\r\n".encode())
    while s.recv(4096):  # read to the end: TLS 1.3 tickets arrive after the handshake
        pass
    out = (took, s.session, s.session_reused, s.version(), s.cipher()[0])
    s.close()
    return out


def tls():
    print("3. TLS resumption")
    ctx = ssl.create_default_context()
    ctx.set_ecdh_curve("X25519")
    full, resumed = [], []
    took, session, _, version, cipher = handshake(ctx)
    lifetime, has_ticket = session.timeout, session.has_ticket
    for _ in range(8):
        took, fresh, reused, _, _ = handshake(ctx)
        full.append(took)
        took, session, reused, _, _ = handshake(ctx, fresh)
        resumed.append(took if reused else None)
    ok = [x for x in resumed if x is not None]
    med = lambda xs: sorted(xs)[len(xs) // 2] if xs else None
    summary = dict(version=version, cipher=cipher, ticket=has_ticket, lifetime_s=lifetime,
                   resumed=f"{len(ok)} of {len(resumed)}", full_median=med(full), resumed_median=med(ok))
    print(f"  {summary}")
    return summary


def keepalive():
    print("4. The server's keep-alive (HEAD /, no key)")
    res = {}

    def run(wait):
        c = http.client.HTTPSConnection(HOST, timeout=30)
        c.request("HEAD", "/")
        c.getresponse().read()
        time.sleep(wait)
        try:
            c.request("HEAD", "/")
            c.getresponse().read()
            res[wait] = "open"
        except (OSError, http.client.HTTPException):
            res[wait] = "closed"

    threads = [threading.Thread(target=run, args=(w,)) for w in KEEPALIVE_WAITS_S]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    print("  " + ", ".join(f"{w} s {res[w]}" for w in KEEPALIVE_WAITS_S))
    return res


def repetition(folder):
    print(f"5. Lemma repetition over {os.path.relpath(folder, ROOT)}")
    meta, pages = {}, []
    for path in sorted(glob.glob(os.path.join(folder, "*.json"))):
        with open(path) as f:
            d = json.load(f)
        if "occurrences" not in d:
            continue
        meta.update({int(k): v for k, v in (d.get("entryMetaById") or {}).items()})
        pages.append([o.get("lemmaEntryId") or o["entryId"] for o in d["occurrences"]
                      if o.get("isWordLike") and o.get("entryId")])
    rank = lambda e: (meta.get(e) or {}).get("rank") or 0

    def hit(pgs, min_rank, again):
        seen, repeats = set(), 0
        for pg in pgs:
            for e in pg:
                if min_rank is not None and 0 < rank(e) <= min_rank:
                    continue
                repeats += e in seen
                seen.add(e)
        taps = len(seen) + again * repeats
        return (again * repeats / taps if taps else 0), len(seen)

    occurrences = sum(len(p) for p in pages)
    summary = dict(pages=len(pages), occurrences=occurrences, lemmas=len({e for p in pages for e in p}))
    print(f"  {summary}")
    for min_rank in RANKS + (None,):
        label = f"rank > {min_rank} (or unranked)" if min_rank else "all words"
        for again in TAP_AGAIN if min_rank else (1.0,):
            whole, distinct = hit(pages, min_rank, again)
            chapters = [hit(pages[i:i + CHAPTER_PAGES], min_rank, again)[0]
                        for i in range(0, len(pages), CHAPTER_PAGES)]
            summary[f"{label}, again {again}"] = dict(whole=whole, distinct=distinct, chapters=chapters)
            print(f"  {label}, taps again {again:.0%}: hit {whole:.0%} over the text ({distinct} lemmas), "
                  f"per {CHAPTER_PAGES} pages {', '.join(f'{c:.0%}' for c in chapters)}")
    return summary


def main():
    os.makedirs(OUT, exist_ok=True)
    if "--only" in sys.argv:
        rest = sys.argv[sys.argv.index("--only") + 2:]
        repetition(rest[0] if rest else os.path.join(ROOT, "research", "spike", "api-cache"))
        return
    key = load_key()
    raw, summary = {}, {}
    summary["lookups"], summary["lookup-medians"] = lookups(key, raw)
    summary["stability"] = stability(key, raw)
    summary["tls"] = tls()
    summary["keepalive"] = keepalive()
    summary["repetition"] = repetition(os.path.join(ROOT, "research", "spike", "api-cache"))
    stamp = time.strftime("%Y-%m-%d")
    with open(os.path.join(OUT, f"probe-v7c-{stamp}.json"), "w") as f:
        json.dump(dict(raw=raw, summary=summary), f, ensure_ascii=False, indent=1, default=str)


if __name__ == "__main__":
    main()

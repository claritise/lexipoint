#!/usr/bin/env python3
"""What v0.2 V7b (page analysis, page-annotations.md §1.1) is built on, measured read-only:

1. A page-sized analyze/text (default mode) on text written for this probe (so Lexirise hasn't seen it): time to
   the first byte and to the end, the answer's size and what takes it (occurrences, entryMetaById, stateByEntryId,
   grammar), the occurrence and entry counts (the compact per-page structure's size).
2. The same page with `fast: true`: time, size, and whether its split is the first answer's (V1's whole words).
3. Each sentence of the page sent alone (as the card's request ① sends it): does the page's split and every
   occurrence's entry and lemma agree with the sentence's, so a lookup on an analyzed page can skip ①? And the page's
   cut sentences (a page starts or ends mid-sentence).
4. The page polled until `morphoPending` is false: when the refined pass arrives at page scale, how the split changes,
   and V1's whole-word merge on it (a ranked `fast` word spanning several refined tokens).
5. GET /v1/me's rate-limit numbers and the rate-limit headers on an answer (names and values only, no ids).

Key from ~/.lexirise_key (never printed). Raw answers go to research/v7b/ (gitignored); the summary printed is what
the docs record (lexirise-api-notes.md, "Page analysis (V7b), measured").

  python3 tools/lexirise/probe_v7b.py            # 1-3, 5 (a few dozen calls), then 4 (polls, up to POLL_MAX_S)
  python3 tools/lexirise/probe_v7b.py --no-poll  # skip 4
"""

import http.client
import json
import os
import re
import sys
import time

HOST = "api.lexirise.app"
POLL_S, POLL_MAX_S = 20, 240

# Written for this probe (2026-09-28): new to Lexirise, so its first answer is a first pass. Each page starts and
# ends mid-sentence, as a reader's page does.
PAGES = [
    ("ja", "ったので、窓の外を眺めながら少しだけ待つことにした。"
           "港町の坂道は朝から霧に包まれていて、灯台の光がぼんやりと滲んで見えた。"
           "祖母が営む小さな喫茶店は、坂の途中にひっそりと建っている。"
           "扉を開けると、古い振り子時計が低い音で九時を告げた。"
           "カウンターの奥では、祖母が慣れた手つきで豆を挽いていた。"
           "「今日は一気に冷えたねえ」と祖母は笑い、湯気の立つ茶碗を差し出してくれた。"
           "私は冷えきった指先を温めながら、昨夜届いた手紙のことを考えていた。"
           "差出人の名前には見覚えがなかったが、筆跡はどこか懐かしかった。"
           "封筒の中には、色褪せた写真が一枚と、短い走り書きが入っていた。"
           "写真に写っていたのは、若い頃の祖母と、見知らぬ青年だった。"
           "裏には、震える文字で日付と地名が書き込まれていた。"
           "その地名を、私は一度も聞いたことがなかった。"
           "祖母に尋ねるべきかどうか迷ったまま、私は茶碗を両手で包み込み"),
    ("zh", "的时候，雨已经停了。"
           "老街两旁的店铺陆陆续续地打开了门，卖早点的摊子冒着白白的热气。"
           "奶奶一边整理着菜篮子，一边跟隔壁的阿姨聊着昨天的新闻。"
           "我站在屋檐下，看着积水里倒映出的灰色天空，心里空荡荡的。"
           "那封没有署名的信还放在我的口袋里，被雨水打湿了一个角。"
           "信上只有短短几行字，字迹却让我觉得似曾相识。"
           "表哥长得很像他的父亲，说话的语气也一模一样。"
           "他告诉我，这条街以前有一家很小的照相馆，老板是个沉默寡言的年轻人。"
           "后来照相馆关了门，那个年轻人也不知道去了哪里。"
           "我深深地吸了一口气，决定先去街尾的旧书店问一问。"
           "书店的老板戴着一副厚厚的眼镜，正低着头修补一本破旧的地图册。"
           "听到我的问题，他慢慢地抬起头，盯着我看了很久，才"),
]

SENTENCE_END = re.compile(r"(?<=[。！？」])")


def load_key():
    with open(os.path.expanduser("~/.lexirise_key")) as f:
        return f.read().strip()


def call(key, method, path, body=None):
    """(ttfb_s, total_s, bytes, json, headers); a fresh connection each time, as the device's first call is."""
    conn = http.client.HTTPSConnection(HOST, timeout=60)
    data = None if body is None else json.dumps(body, ensure_ascii=False).encode()
    t = time.monotonic()
    conn.request(method, path, body=data,
                 headers={"Authorization": f"Bearer {key}", "Content-Type": "application/json"})
    r = conn.getresponse()
    ttfb = time.monotonic() - t
    raw = r.read()
    total = time.monotonic() - t
    headers = {k.lower(): v for k, v in r.getheaders()}
    conn.close()
    if r.status != 200:
        raise RuntimeError(f"{method} {path}: HTTP {r.status}")
    return ttfb, total, len(raw), json.loads(raw), headers


def analyze(key, lang, text, fast=False):
    body = {"text": text, "language": lang}
    if fast:
        body["fast"] = True
    return call(key, "POST", "/v1/analyze/text", body)


def utf16_len(s):
    return len(s.encode("utf-16-le")) // 2


def shape(d, size):
    parts = {k: len(json.dumps(d.get(k), ensure_ascii=False).encode()) for k in
             ("occurrences", "entryMetaById", "stateByEntryId", "grammar")}
    occ = d.get("occurrences", [])
    return dict(bytes=size, parts=parts, occurrences=len(occ), wordlike=sum(1 for o in occ if o.get("isWordLike")),
                meta=len(d.get("entryMetaById") or {}), state=len(d.get("stateByEntryId") or {}),
                grammar=len(d.get("grammar") or []), morphoPending=d.get("morphoPending"),
                entries_used=len({o.get("entryId") for o in occ} | {o.get("lemmaEntryId") for o in occ} - {None}),
                max_word_bytes=max((len((o.get("word") or "").encode()) for o in occ), default=0),
                max_reading_bytes=max((len((o.get("transliteration") or "").encode()) for o in occ), default=0))


def split_of(d):
    return [(o["charStart"], o["charEnd"], o.get("word"), o.get("entryId"), o.get("lemmaEntryId"))
            for o in d.get("occurrences", [])]


def compare_sentences(key, lang, page, pd):
    """Each sentence alone vs the page's occurrences over the same span (shifted by the sentence's start)."""
    page_occ = split_of(pd)
    at, rows = 0, []
    pieces = [p for p in SENTENCE_END.split(page) if p]
    for i, s in enumerate(pieces):
        start, end = at, at + utf16_len(s)
        at = end
        _, total, _, sd, _ = analyze(key, lang, s)
        mine = [(a - start, b - start, w, e, le) for a, b, w, e, le in page_occ if a >= start and b <= end]
        theirs = split_of(sd)
        same_split = [x[:3] for x in mine] == [x[:3] for x in theirs]
        same_entries = mine == theirs
        cut = "first (cut)" if i == 0 else "last (cut)" if i == len(pieces) - 1 else ""
        rows.append(dict(i=i, units=end - start, took_s=round(total, 2), same_split=same_split,
                         same_entries=same_entries, pending=sd.get("morphoPending"), cut=cut,
                         diff=None if same_entries else {"page": mine, "sentence": theirs}))
    return rows


def whole_word_merges(refined, fast):
    """V1's rule at page scale: a ranked fast word that spans more than one refined token replaces them."""
    rank = {int(k): v.get("rank") for k, v in (fast.get("entryMetaById") or {}).items()}
    ref = split_of(refined)
    merges = []
    for a, b, w, e, _ in split_of(fast):
        inside = [x for x in ref if x[0] >= a and x[1] <= b]
        if len(inside) > 1 and rank.get(e):
            merges.append(f"{w} <- {'·'.join(x[2] for x in inside)}")
    return merges


def main():
    key = load_key()
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "research", "v7b")
    os.makedirs(out, exist_ok=True)
    raw, summary = {}, {}

    print("5. Rate limit")
    _, _, _, me, headers = call(key, "GET", "/v1/me")
    ak = me.get("apiKey") or {}
    rl = {k: v for k, v in ak.items() if k.lower().startswith("ratelimit")}
    print(f"  /v1/me apiKey rate-limit fields: {rl}")
    print(f"  rate-limit headers on /v1/me: { {k: v for k, v in headers.items() if 'rate' in k or 'retry' in k} }")
    summary["rate"] = rl

    for lang, page in PAGES:
        units = utf16_len(page)
        print(f"\n1. {lang} page, {units} UTF-16 units ({len(page.encode())} UTF-8 bytes)")
        ttfb, total, size, d, headers = analyze(key, lang, page)
        s = shape(d, size)
        raw[f"{lang}-first"] = d
        print(f"  default: first byte {ttfb:.2f} s, whole {total:.2f} s, {size} B (~{size / units:.0f} B/unit)")
        print(f"  {s}")
        print(f"  rate headers: { {k: v for k, v in headers.items() if 'rate' in k} }")
        summary[f"{lang}-first"] = dict(s, ttfb=ttfb, total=total, units=units)

        print("2. fast: true")
        fttfb, ftotal, fsize, f, _ = analyze(key, lang, page, fast=True)
        raw[f"{lang}-fast"] = f
        same = [x[:3] for x in split_of(f)] == [x[:3] for x in split_of(d)]
        print(f"  fast: first byte {fttfb:.2f} s, whole {ftotal:.2f} s, {fsize} B, {len(f.get('occurrences', []))} "
              f"occurrences, split same as the first answer: {same}")
        summary[f"{lang}-fast"] = dict(ttfb=fttfb, total=ftotal, bytes=fsize, same_split=same)

        print("  again (default, the same page)")
        attfb, atotal, asize, again, _ = analyze(key, lang, page)
        raw[f"{lang}-again"] = again
        print(f"  again: first byte {attfb:.2f} s, whole {atotal:.2f} s, {asize} B, morphoPending "
              f"{again.get('morphoPending')}, split same: {split_of(again) == split_of(d)}")

        print("3. Each sentence alone vs the page")
        rows = compare_sentences(key, lang, page, d)
        raw[f"{lang}-sentences"] = rows
        for r in rows:
            print(f"  #{r['i']:2} {r['units']:3}u {r['took_s']:.2f}s split {r['same_split']!s:5} "
                  f"entries {r['same_entries']!s:5} pending {r['pending']} {r['cut']}")
            if r["diff"]:
                print(f"      page:     {[x[2:] for x in r['diff']['page']]}")
                print(f"      sentence: {[x[2:] for x in r['diff']['sentence']]}")
        summary[f"{lang}-sentences"] = rows

    if "--no-poll" not in sys.argv:
        print(f"\n4. Polling each page every {POLL_S} s until refined (at most {POLL_MAX_S} s)")
        start = time.monotonic()
        pending = {lang: page for lang, page in PAGES}
        while pending and time.monotonic() - start < POLL_MAX_S:
            time.sleep(POLL_S)
            for lang, page in list(pending.items()):
                _, total, size, d, _ = analyze(key, lang, page)
                if not d.get("morphoPending"):
                    at = time.monotonic() - start
                    first = raw[f"{lang}-first"]
                    raw[f"{lang}-refined"] = d
                    _, ftotal, fsize, f, _ = analyze(key, lang, page, fast=True)
                    raw[f"{lang}-refined-fast"] = f
                    merges = whole_word_merges(d, f)
                    changed = sum(1 for x in split_of(first) if x[:3] not in {y[:3] for y in split_of(d)})
                    s = shape(d, size)
                    print(f"  {lang}: refined by ~{at:.0f} s (call {total:.2f} s, {size} B); {s}")
                    print(f"     first-pass tokens not in the refined split: {changed}; fast after refining: "
                          f"{ftotal:.2f} s, {fsize} B, same split as the first answer: "
                          f"{[x[:3] for x in split_of(f)] == [x[:3] for x in split_of(first)]}")
                    print(f"     V1 merges at page scale ({len(merges)}): {merges}")
                    summary[f"{lang}-refined"] = dict(s, at_s=at, merges=merges, changed=changed)
                    del pending[lang]
        for lang in pending:
            print(f"  {lang}: still pending after {POLL_MAX_S} s")

    with open(os.path.join(out, "probe-v7b-2026-09-28.json"), "w", encoding="utf-8") as f:
        json.dump({"raw": raw, "summary": summary}, f, ensure_ascii=False, indent=1)


if __name__ == "__main__":
    main()

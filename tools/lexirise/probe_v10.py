#!/usr/bin/env python3
"""What v0.2 V10 (the sense and reading from the sentence, C10) is designed on, measured read-only from the Mac:
analyze/context on the words whose dictionary reading or sense is wrong in context (四月一日 tsuitachi, 一枚上手 uwate,
长得 zhǎng), against the same words where the default is right, and dictionary/lookup's default for each.

For each (sentence, word): analyze/text for the word's offsets, analyze/context with them, dictionary/lookup for the
lemma. Printed: the fields analyze/context returns, whether it gave a reading and in what form, the concise and full
meanings' lengths, the lookup's default reading and first sense, and the context call's time. Sentences are written
for this probe; no ids are printed.

Key from ~/.lexirise_key (never printed). Raw answers go to research/v10/ (gitignored); the summary printed is what
the docs record (lexirise-api-notes.md, "analyze/context, measured").

  python3 tools/lexirise/probe_v10.py
"""

import http.client
import json
import os
import time

HOST = "api.lexirise.app"
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
OUT = os.path.join(ROOT, "research", "v10")

# (language, sentence, the word as analyze/text should split it, why it's here)
CASES = [
    ("ja", "四月一日に入学式がある。", "一日", "tsuitachi in a date"),
    ("ja", "今日は一日中雨だった。", "一日", "ichinichi, the default"),
    ("ja", "将棋では彼の方が一枚上手だった。", "上手", "uwate in 一枚上手"),
    ("ja", "彼女は歌が上手だ。", "上手", "jouzu, the default"),
    ("ja", "間違いにやっと気づいた。", "気づいた", "dzu"),
    ("ja", "明日、教室で会おう。", "教室", "a plain word"),
    ("zh", "他长得很高。", "长", "zhǎng in 长得"),
    ("zh", "这条路很长。", "长", "cháng, the default"),
    ("zh", "我们一起去银行吧。", "行", "háng in 银行"),
]


def load_key():
    with open(os.path.expanduser("~/.lexirise_key")) as f:
        return f.read().strip()


class Api:
    def __init__(self, key):
        self.key, self.conn = key, None

    def post(self, path, body):
        for attempt in range(2):
            if self.conn is None:
                self.conn = http.client.HTTPSConnection(HOST, timeout=90)
            try:
                start = time.time()
                self.conn.request("POST", path, body=json.dumps(body, ensure_ascii=False).encode(),
                                  headers={"Authorization": f"Bearer {self.key}", "Content-Type": "application/json"})
                r = self.conn.getresponse()
                raw = r.read()
                ms = int((time.time() - start) * 1000)
                if r.status != 200:
                    return {"_status": r.status, "_body": raw.decode("utf-8", "replace")[:300]}, ms
                return json.loads(raw), ms
            except (OSError, http.client.HTTPException):
                self.conn = None
                if attempt:
                    raise
        return {}, 0


def offsets(analysis, word):
    """The occurrence whose surface is `word` (or, failing that, one that contains it): (charStart, charEnd, lemma)."""
    occs = analysis.get("occurrences") or []
    for pick in (lambda o: o.get("word") == word, lambda o: word in (o.get("word") or "")):
        for o in occs:
            if pick(o):
                return o.get("charStart"), o.get("charEnd"), o.get("lemma") or o.get("word"), o.get("word")
    return None


def form(reading):
    if not reading:
        return "none"
    if all(ord(c) < 0x3000 for c in reading):
        return "latin"
    return "kana" if all(0x3040 <= ord(c) <= 0x30FF or c in "ー・ " for c in reading) else "other"


def main():
    api = Api(load_key())
    raw = []
    for lang, text, word, why in CASES:
        analysis, _ = api.post("/v1/analyze/text", {"text": text, "language": lang})
        found = offsets(analysis, word)
        entry = {"language": lang, "text": text, "word": word, "analyze": analysis}
        if not found:
            print(f"- {text} [{word}] ({why}): no occurrence for it; the split: "
                  f"{[o.get('word') for o in analysis.get('occurrences') or []]}")
            raw.append(entry)
            continue
        start, end, lemma, surface = found
        context, ms = api.post("/v1/analyze/context",
                               {"text": text, "language": lang, "charStart": start, "charEnd": end})
        lookup, _ = api.post("/v1/dictionary/lookup", {"text": lemma, "language": lang})
        entry.update(context=context, lookup=lookup)
        raw.append(entry)
        if "_status" in context:
            print(f"- {text} [{surface}] ({why}): analyze/context HTTP {context['_status']}: {context['_body']}")
            continue
        reading = context.get("reading") or ""
        concise = context.get("conciseMeaning") or ""
        meaning = context.get("meaning") or ""
        senses = lookup.get("senses") or lookup.get("translations") or []
        first = senses[0] if senses else {}
        first = first.get("translation") if isinstance(first, dict) else first
        print(f"- {text} [{surface}, lemma {lemma}] ({why}): {ms} ms; keys {sorted(context)}")
        print(f"    context reading: {reading or '(none)'} ({form(reading)}); concise ({len(concise)} ch): {concise}")
        print(f"    full meaning: {len(meaning)} characters")
        print(f"    lookup default: reading {lookup.get('transliteration') or lookup.get('reading') or '(none)'}; "
              f"first sense: {first}")
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, f"probe-v10-{time.strftime('%Y-%m-%d')}.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(raw, f, ensure_ascii=False, indent=1)
    print(f"raw answers: {os.path.relpath(path, ROOT)}")


if __name__ == "__main__":
    main()

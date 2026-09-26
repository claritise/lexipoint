#!/usr/bin/env python3
"""How does Lexirise's analyze/text split conjugated verbs? (v0.2 V4, C16's conjugation namer.) The card names a
form from the occurrence it's handed (its `word`, its `lemma`) and the page's next character, so a verb the server
cuts into a stem and an ending (書け · ない) would be named from the stem alone. This asks, for sentences written
here to hold the forms that risk that, what each pass of analyze/text returns for the verb:

- the first answer (the default call, as the card makes it);
- the refined answer (the same call once `morphoPending` is false; Lexirise caches it, so a sentence it has seen
  before comes back refined on the card's first call);
- the `fast: true` answer on the refined sentence (the word-level split the card rejoins with, v0.2 V1).

Read-only: analyze/text only. Key from ~/.lexirise_key (never printed). Raw responses go to research/v4-splits/
(gitignored); the summary printed is what the docs record (lexirise-api-notes.md, "How analyze/text splits
conjugated verbs").

  python3 tools/lexirise/probe_splits.py [--wait 150] [--only N]
"""

import argparse
import concurrent.futures as cf
import json
import os
import time
import urllib.request

BASE = "https://api.lexirise.app"
POLL_S = 15

# (sentence, the verb form in it the card would name). Written for this probe: no book text.
SENTENCES = [
    ("この字はもう書けない。", "書けない"),
    ("時間があれば手紙を書けばいい。", "書けば"),
    ("雨が降れば早く帰ればいい。", "帰れば"),
    ("明日、一緒に手紙を書こう。", "書こう"),
    ("この道なら駅まで歩いて行けそうだ。", "行けそう"),
    ("急いでいたので名前を書けずに出てきた。", "書けず"),
    ("子供は甘いものばかり食べたがる。", "食べたがる"),
    ("弟はいつも私の真似をしたがる。", "したがる"),
    ("あの強いチームには勝てっこない。", "勝てっこない"),
    ("本当はもっと映画を見ていたかった。", "見ていたかった"),
    ("明日は雨が降るかもしれない。", "しれない"),
    ("朝ごはんを食べないで学校へ行った。", "食べない"),
    ("どんなに練習しても先生にはなれない。", "なれない"),
    ("そのケーキ、全部食べちゃうよ。", "食べちゃう"),
    ("駅に着いたら電話してください。", "着いたら"),
    ("休みの日は本を読んだり寝たりする。", "読んだり"),
    ("彼は日本語が話せ、英語も少しわかる。", "話せ"),
    ("彼女は何も食べさせられていませんでした。", "食べさせられていませんでした"),
    ("早くここに名前を書け！", "書け"),
    ("野菜も残さず食べろ。", "食べろ"),
    ("もう遅いから、そろそろ帰ろう。", "帰ろう"),
    ("漢字がたくさん書けるようになった。", "書ける"),
    ("このりんごは食べられる。", "食べられる"),
    ("先生に名前を呼ばれた。", "呼ばれた"),
    ("母に部屋を掃除させられた。", "掃除させられた"),
    ("もっと早く起きればよかった。", "起きれば"),
    ("この本を読んでいる。", "読んでいる"),
    ("昨日は寒くなかった。", "寒くなかった"),
    # する verbs: analyze/text gives the noun as the lemma (勉強した‹勉強›)
    ("昨日は図書館で勉強した。", "勉強した"),
    ("毎日日本語を勉強しています。", "勉強しています"),
    ("駅に着いたら電話してください。", "電話して"),
    ("母を心配させるな。", "心配させる"),
    ("彼は結婚している。", "結婚している"),
    # Nouns and する (V4 R21): a noun that isn't a する verb, and one-kanji verbs
    ("二人して笑った。", "して"),
    ("皆して騒いだ。", "騒いだ"),
    ("やっと彼氏できた？", "できた"),
    ("友達できた？", "できた"),
    ("日本語が勉強できる。", "勉強できる"),
    ("彼女を愛した。", "愛した"),
    ("先生に話した。", "話した"),
    ("お金を貸して。", "貸して"),
    ("ゴミを出した。", "出した"),
    ("食べないです。", "食べない"),
    ("もう書いたらしい。", "書いたらしい"),
]


def call(key: str, text: str, fast: bool = False) -> dict:
    body = {"text": text, "language": "ja"}
    if fast:
        body["fast"] = True
    req = urllib.request.Request(f"{BASE}/v1/analyze/text", data=json.dumps(body).encode(), method="POST",
                                 headers={"Authorization": f"Bearer {key}", "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.load(r)


def tokens(d: dict) -> list[dict]:
    return [{k: o.get(k) for k in ("word", "lemma", "entryId", "lemmaEntryId", "charStart", "charEnd")}
            for o in d.get("occurrences", [])]


def covering(toks: list[dict], text: str, form: str) -> list[dict]:
    """The occurrences that overlap the form's span in the sentence (charStart/charEnd are UTF-16 = str index
    for these BMP-only sentences)."""
    start = text.index(form)
    end = start + len(form)
    return [t for t in toks if t["charStart"] is not None and t["charStart"] < end and t["charEnd"] > start]


def show(ts: list[dict]) -> str:
    return " · ".join(f"{t['word']}" + (f"<{t['lemma']}>" if t.get("lemma") and t["lemma"] != t["word"] else "")
                      for t in ts)


def probe(n: int, text: str, form: str, key: str, out: str, wait_s: float) -> dict:
    start = time.monotonic()
    answers = [call(key, text)]
    while answers[-1].get("morphoPending") and time.monotonic() - start < wait_s:
        time.sleep(POLL_S)
        answers.append(call(key, text))
    refined = None if answers[-1].get("morphoPending") else answers[-1]
    fast = call(key, text, fast=True) if refined is not None else None
    with open(os.path.join(out, f"{n:02d}.json"), "w", encoding="utf-8") as f:
        json.dump({"text": text, "form": form, "answers": answers, "fast": fast}, f, ensure_ascii=False, indent=1)
    first = answers[0]
    return dict(n=n, text=text, form=form, first_pending=first.get("morphoPending"),
                refined_at=None if refined is None else round(time.monotonic() - start),
                first=show(covering(tokens(first), text, form)),
                refined=None if refined is None else show(covering(tokens(refined), text, form)),
                fast=None if fast is None else show(covering(tokens(fast), text, form)),
                lemma_field=any("lemma" in o for o in first.get("occurrences", [])))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--wait", type=float, default=150, help="seconds to wait for the refined pass")
    ap.add_argument("--only", type=int, help="probe one sentence by index")
    a = ap.parse_args()
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "research", "v4-splits")
    os.makedirs(out, exist_ok=True)
    with open(os.path.expanduser("~/.lexirise_key")) as f:
        key = f.read().strip()
    jobs = [(n, t, form) for n, (t, form) in enumerate(SENTENCES) if a.only is None or n == a.only]
    with cf.ThreadPoolExecutor(8) as ex:
        results = list(ex.map(lambda j: probe(*j, key, out, a.wait), jobs))
    for r in results:
        print(f"[{r['n']:02d}] {r['form']}  in {r['text']}")
        print(f"     first ({'pending' if r['first_pending'] else 'already refined'}): {r['first']}")
        print(f"     refined{'' if r['refined_at'] is None else ' @' + str(r['refined_at']) + 's'}: {r['refined']}")
        print(f"     fast on refined: {r['fast']}   (lemma field present: {r['lemma_field']})")
    with open(os.path.join(out, "summary.json"), "w", encoding="utf-8") as f:
        json.dump(results, f, ensure_ascii=False, indent=1)


if __name__ == "__main__":
    main()

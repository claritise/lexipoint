#!/usr/bin/env python3
"""How does Lexirise's `suspended` behave? (v0.2 V5, C17's "Ignore this word".) WRITES to the dev key's account,
so run it only with claritise's OK; it undoes what it can:

A. A saved word (the dev account's throwaway 蓋然性): PATCH suspended true, then read it back through analyze/text's
   state and GET /v1/vocabulary/{id}; then PATCH suspended false.
B. An unsaved word (寸暇): POST /v1/vocabulary with suspended true, read it back the same ways, then DELETE it and read
   it once more (a dictionary word's DELETE resets it to unknown rather than removing it).

Key from ~/.lexirise_key (never printed). Raw answers go to research/v5/ (gitignored); the summary printed is what the
docs record (lexirise-api-notes.md, "Suspended (Ignore)"). Ids are never printed.

  python3 tools/lexirise/probe_suspend.py
"""

import json
import os
import time
import urllib.error
import urllib.request

from probe_splits import BASE, call

SAVED = "この結論は蓋然性が高い。"   # 蓋然性 is saved in the dev account
UNSAVED_WORD, UNSAVED = "寸暇", "寸暇を惜しんで勉強した。"


def req(key, method, path, body=None):
    data = None if body is None else json.dumps(body).encode()
    r = urllib.request.Request(f"{BASE}{path}", data=data, method=method,
                               headers={"Authorization": f"Bearer {key}", "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=30) as x:
            raw = x.read()
            return x.status, (json.loads(raw) if raw else None)
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")[:300]


def state_of(key, sentence, word):
    d = call(key, sentence)
    for o in d["occurrences"]:
        if o["word"] == word:
            st = (d.get("stateByEntryId") or {}).get(str(o.get("lemmaEntryId") or o.get("entryId")))
            return d, st
    return d, None


def item_of(resp):
    return resp.get("item", resp) if isinstance(resp, dict) else {}


def main():
    key = open(os.path.expanduser("~/.lexirise_key")).read().strip()
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "research", "v5")
    os.makedirs(out, exist_ok=True)
    raw = {}

    print("A. saved word 蓋然性")
    d, st = state_of(key, SAVED, "蓋然性")
    sid = st["saved_expression_id"]
    code, r = req(key, "PATCH", f"/v1/vocabulary/{sid}", {"suspended": True})
    raw["A_patch_true"] = r
    print("  PATCH suspended true ->", code, "| item.suspended =", item_of(r).get("suspended"))
    time.sleep(1)
    d, st = state_of(key, SAVED, "蓋然性")
    raw["A_analyze"] = d
    print("  analyze state keys:", sorted(st.keys()) if st else None, "| suspended in state:", st and "suspended" in st)
    code, r = req(key, "GET", f"/v1/vocabulary/{sid}")
    raw["A_get"] = r
    print("  GET item ->", code, "| suspended =", item_of(r).get("suspended"), "| proficiency =",
          item_of(r).get("proficiency"))
    code, r = req(key, "PATCH", f"/v1/vocabulary/{sid}", {"suspended": False})
    print("  PATCH suspended false ->", code, "| item.suspended =", item_of(r).get("suspended"))

    print(f"B. unsaved word {UNSAVED_WORD}")
    d, st = state_of(key, UNSAVED, UNSAVED_WORD)
    print("  before: saved state =", st)
    code, r = req(key, "POST", "/v1/vocabulary",
                  {"language": "ja", "text": UNSAVED_WORD, "mode": "word", "proficiency": 1, "tags": [],
                   "suspended": True})
    raw["B_post"] = r
    if code == 422:  # 2026-09-27: POST rejects suspended; save, then PATCH it
        print("  POST with suspended true -> 422 (rejected):", str(r)[:160])
        code, r = req(key, "POST", "/v1/vocabulary",
                      {"language": "ja", "text": UNSAVED_WORD, "mode": "word", "proficiency": 1, "tags": []})
        raw["B_post_plain"] = r
    it = item_of(r)
    result = r.get("result", {}) if isinstance(r, dict) else {}
    print("  POST ->", code, "| result.status =", result.get("status"), "| item.suspended =",
          it.get("suspended"), "| proficiency =", it.get("proficiency"))
    bid = it.get("id") or result.get("savedExpressionId")
    if bid and not it.get("suspended"):
        code, r = req(key, "PATCH", f"/v1/vocabulary/{bid}", {"suspended": True})
        raw["B_patch_true"] = r
        print("  then PATCH suspended true ->", code, "| item.suspended =", item_of(r).get("suspended"))
    time.sleep(1)
    d, st = state_of(key, UNSAVED, UNSAVED_WORD)
    raw["B_analyze"] = d
    print("  analyze state:", sorted(st.keys()) if st else None, "| proficiency =", st and st.get("proficiency"))
    if bid:
        code, r = req(key, "DELETE", f"/v1/vocabulary/{bid}")
        raw["B_delete"] = r
        print("  DELETE ->", code, "|", r if not isinstance(r, dict) else {k: r[k] for k in r if k != "item"})
        code, r = req(key, "GET", f"/v1/vocabulary/{bid}")
        raw["B_get_after"] = r
        print("  GET after DELETE ->", code, "| suspended =", item_of(r).get("suspended"), "| proficiency =",
              item_of(r).get("proficiency"))
        if code == 200 and item_of(r).get("suspended"):
            code, r = req(key, "PATCH", f"/v1/vocabulary/{bid}", {"suspended": False})
            print("  cleanup PATCH suspended false ->", code)
    with open(os.path.join(out, "suspend-2026-09-27.json"), "w", encoding="utf-8") as f:
        json.dump(raw, f, ensure_ascii=False, indent=1)


if __name__ == "__main__":
    main()

"""The card's reference states (popup-ui.md §1.1, reference/card-reference.html): one list for the
conformance preview (cardshots.py), the golden display lists (cardgolden.py) and the device smoke test
(lxctl.py card-smoke)."""

from __future__ import annotations


# A state: (name, language, word, view, tab, position, reading, toast, reference actions).
# The reference actions reproduce it in card-reference.html (cardshots.py REF_SCRIPT).
def states() -> list[dict]:
    out = []

    def add(name, lang, word, view="card", tab=0, pos="high", reading="kana", toast="", actions=None, level=None):
        out.append(dict(name=name, lang=lang, word=word, view=view, tab=tab, pos=pos, reading=reading, toast=toast,
                        actions=actions or [], level=level))

    ja_tabs = ["meaning", "examples", "context", "kanji", "form", "actions"]
    zh_tabs = ["meaning", "examples", "context", "chars", "actions"]
    add("ja-card-saved", "ja", 2)
    add("ja-card-new", "ja", 0)
    add("ja-card-low", "ja", 2, pos="low")
    add("ja-card-romaji", "ja", 2, reading="romaji", toast="Readings: romaji", actions=["reading"])
    add("ja-card-save-toast", "ja", 3, toast="Saved as learning  ·  Undo", actions=["level:1"], level=1)
    for i, t in enumerate(ja_tabs):
        add(f"ja-expanded-{t}", "ja", 2, view="expanded", tab=i)
    add("ja-expanded-kanji-romaji", "ja", 1, view="expanded", tab=3, reading="romaji", toast="Readings: romaji",
        actions=["reading"])
    add("ja-expanded-actions-new", "ja", 0, view="expanded", tab=5)
    add("zh-card-saved", "zh", 5)
    add("zh-card-new", "zh", 1)
    add("zh-card-low", "zh", 5, pos="low")
    for i, t in enumerate(zh_tabs):
        add(f"zh-expanded-{t}", "zh", 5, view="expanded", tab=i)
    return out


def render_args(state: dict) -> list[str]:
    """LexiriseCardRender's arguments after the output path (test/lexirise_card/CardRender.cpp)."""
    args = [state["lang"], str(state["word"]), state["view"], str(state["tab"]), state["pos"], state["reading"]]
    if state["level"] is not None:  # the tool reaches the state by tapping, so the toast comes by itself
        args.append(str(state["level"]))
    return args

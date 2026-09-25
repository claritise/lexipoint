"""What users see says Lexipoint (D22, phase M). LexiriseNav.js renames the base's texts on CrossPoint's own web
pages at load: if a base page stops carrying the text the script looks for, the page would quietly say
CrossPoint again, so these tests pin both sides. The network names come from LexiriseConfig.h."""

import json
import os
import re
import shutil
import subprocess
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
FIRMWARE = os.path.join(HERE, "..", "..")
NAV = os.path.join(FIRMWARE, "src", "lexirise", "web", "LexiriseNav.js")
PAGES = os.path.join(FIRMWARE, "src", "network", "html")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def nav_constant(name):
    m = re.search(r"\b" + name + r" = '([^']+)'", read(NAV))
    assert m, f"{name} not found in LexiriseNav.js"
    return m.group(1)


def pages_with_nav():
    """The base pages that load /lexirise/nav.js."""
    out = {}
    for name in sorted(os.listdir(PAGES)):
        if name.endswith(".html"):
            text = read(os.path.join(PAGES, name))
            if '<script src="/lexirise/nav.js"' in text:
                out[name] = text
    return out


class NavRenames(unittest.TestCase):
    def test_every_page_with_a_nav_bar_loads_the_script(self):
        # Pages with CrossPoint's nav bar get the Lexirise link and the rename.
        for name in sorted(os.listdir(PAGES)):
            if name.endswith(".html") and 'class="nav-links"' in read(os.path.join(PAGES, name)):
                self.assertIn(name, pages_with_nav(), name)

    def test_each_page_still_carries_the_title_and_heading_the_script_renames(self):
        title = nav_constant("TITLE")
        pages = pages_with_nav()
        self.assertGreaterEqual(len(pages), 4)  # Files, Fonts, Home, Settings
        for name, text in pages.items():
            self.assertRegex(text, r"<title>[^<]*" + re.escape(title) + r"[^<]*</title>", name)
            self.assertRegex(text, r"<h1>[^<]*" + re.escape(title) + r"[^<]*</h1>", name)

    def test_the_footers_the_script_renames_are_in_a_card_paragraph(self):
        footer = nav_constant("FOOTER")
        found = 0
        for name, text in pages_with_nav().items():
            if footer in text:
                found += 1
                self.assertRegex(text, r'<div class="card">\s*<p[^>]*>\s*' + re.escape(footer), name)
        self.assertGreaterEqual(found, 3)  # Files, Home, Settings

    def test_the_new_names_say_lexipoint(self):
        self.assertEqual(nav_constant("NAME"), "Lexipoint")
        self.assertIn("Lexipoint", nav_constant("FOOTER_NAME"))


# A stub page for LexiriseNav.js: the title (with a MutationObserver that, like a browser's, delivers after the
# write, so the script sees its own edit), text-node children for h1 and the footer, and the nav bar. Each case
# prints the title and texts after the script ran and after `then` retitled the page.
NAV_HARNESS = r"""
const fs = require('fs');
const src = fs.readFileSync(process.argv[1], 'utf8');
const cases = JSON.parse(process.argv[2]);
const out = [];
for (const c of cases) {
  let title = c.title, observers = [], queue = [];
  const flush = () => { while (queue.length) { queue.shift()(); } };
  const text = (value) => ({ nodeType: 3, nodeValue: value, nextSibling: null });
  const el = (value) => ({ firstChild: text(value) });
  const h1 = el(c.h1), footer = el(c.footer);
  const document = {
    get title() { return title.replace(/[ \t\n\f\r]+/g, ' ').trim(); },  // as a browser reads it back
    set title(v) { title = v; observers.forEach((cb) => queue.push(cb)); },
    querySelector: (sel) => (sel === 'title' ? {} : null),
    querySelectorAll: (sel) => (sel === 'h1' ? [h1] : sel === '.card p' ? [footer] : []),
  };
  function MutationObserver(cb) { this.observe = () => observers.push(cb); }
  new Function('document', 'MutationObserver', src)(document, MutationObserver);
  flush();
  if (c.then) { document.title = c.then; flush(); }
  out.push({ title, h1: h1.firstChild.nodeValue, footer: footer.firstChild.nodeValue });
}
console.log(JSON.stringify(out));
"""


# The only behaviour test of the rename: skipped locally without node, never in CI (GitHub's runners have it).
@unittest.skipUnless(shutil.which("node") or os.environ.get("CI"), "node isn't installed")
class NavBehaviour(unittest.TestCase):
    """LexiriseNav.js run against a stub page (node): what a reader of the web pages actually sees."""

    def run_nav(self, cases):
        out = subprocess.run(["node", "-e", NAV_HARNESS, NAV, json.dumps(cases)], capture_output=True, text=True,
                             check=True).stdout
        return json.loads(out)

    def test_the_pages_say_lexipoint(self):
        [home, files] = self.run_nav([
            {"title": "CrossPoint Reader", "h1": "📚 CrossPoint Reader", "footer": "CrossPoint E-Reader • Open Source"},
            {"title": "Files - CrossPoint Reader", "h1": "📚 CrossPoint Reader", "footer": "x",
             "then": "Books - Files - CrossPoint Reader"},  # the file browser retitles as it moves between folders
        ])
        self.assertEqual(home, {"title": "Lexipoint", "h1": "📚 Lexipoint",
                                "footer": "Lexipoint, built on CrossPoint • Open Source"})
        self.assertEqual(files["title"], "Books - Files - Lexipoint")

    def test_a_folder_named_like_the_product_keeps_its_name(self):
        [opened, moved] = self.run_nav([
            # Opened on the folder: the page's inline script titles it before the deferred nav.js runs.
            {"title": "CrossPoint Reader - Files - CrossPoint Reader", "h1": "", "footer": ""},
            {"title": "Files - CrossPoint Reader", "h1": "", "footer": "",
             "then": "CrossPoint Reader - Files - CrossPoint Reader"},  # moved into it later
        ])
        self.assertEqual(opened["title"], "CrossPoint Reader - Files - Lexipoint")
        self.assertEqual(moved["title"], "CrossPoint Reader - Files - Lexipoint")

    def test_a_page_without_the_names_is_left_alone(self):
        [page] = self.run_nav([{"title": "Lexirise - Lexipoint", "h1": "📚 Lexipoint", "footer": "Something else"}])
        self.assertEqual(page, {"title": "Lexirise - Lexipoint", "h1": "📚 Lexipoint", "footer": "Something else"})


class NetworkNames(unittest.TestCase):
    """The hotspot, the mDNS name and the router hostname: LexiriseConfig.h's, in every base file that names them."""

    USES = {
        "src/activities/network/CrossPointWebServerActivity.cpp": ("kHotspotSsid", "kMdnsHostname"),
        "src/activities/network/CalibreConnectActivity.cpp": ("kMdnsHostname",),
        "src/activities/network/WifiSelectionActivity.cpp": ("kDhcpHostnamePrefix",),
    }

    def test_the_base_files_use_the_config_names_in_lexirise_builds(self):
        for path, names in self.USES.items():
            text = read(os.path.join(FIRMWARE, path))
            for name in names:
                self.assertRegex(text, r"#if LEXIRISE[^#]*lexipoint::config::" + name, f"{path}: {name}")

    def test_the_names_say_lexipoint(self):
        config = read(os.path.join(FIRMWARE, "src/lexirise/LexiriseConfig.h"))
        for name, value in (("kHotspotSsid", "Lexipoint"), ("kMdnsHostname", "lexipoint"),
                            ("kDhcpHostnamePrefix", "Lexipoint-")):
            self.assertRegex(config, name + r'(\[\])? = "' + re.escape(value) + '";', name)


if __name__ == "__main__":
    unittest.main()

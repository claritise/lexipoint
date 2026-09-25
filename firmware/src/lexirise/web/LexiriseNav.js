// LEXIPOINT: adds the "Lexirise" link to the nav bar of CrossPoint's own pages, and names the product
// Lexipoint in their title, heading and footer (D22). Served only by Lexirise builds (/lexirise/nav.js);
// elsewhere the script 404s and the pages stay as they were. test_rebrand.py pins the base's texts.
(function () {
  var TITLE = 'CrossPoint Reader', NAME = 'Lexipoint';
  var FOOTER = 'CrossPoint E-Reader', FOOTER_NAME = 'Lexipoint, built on CrossPoint';
  // The product is the title's last part ("<folder> - Files - CrossPoint Reader"): a folder named like it stays.
  // The observer also sees this script's own write, which it leaves alone.
  var written = null;
  var retitle = function () {
    if (document.title === written) return;
    var at = document.title.lastIndexOf(TITLE);
    if (at < 0) return;
    written = document.title.slice(0, at) + NAME + document.title.slice(at + TITLE.length);
    document.title = written;
  };
  retitle();
  var title = document.querySelector('title');
  // The file browser retitles as it moves between folders, after this script's first pass.
  if (title) new MutationObserver(retitle).observe(title, { childList: true });
  var rename = function (selector, from, to) {
    document.querySelectorAll(selector).forEach(function (el) {
      for (var n = el.firstChild; n; n = n.nextSibling) {
        if (n.nodeType === 3 && n.nodeValue.indexOf(from) >= 0) n.nodeValue = n.nodeValue.replace(from, to);
      }
    });
  };
  rename('h1', TITLE, NAME);
  rename('.card p', FOOTER, FOOTER_NAME);
})();
(function () {
  var nav = document.querySelector('.nav-links');
  if (!nav || nav.querySelector('a[href="/lexirise"]')) return;
  var link = document.createElement('a');
  link.href = '/lexirise';
  link.textContent = 'Lexirise';
  nav.appendChild(link);
})();

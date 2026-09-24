// LEXIPOINT: adds the "Lexirise" link to the nav bar of CrossPoint's own pages. Served only by
// Lexirise builds (/lexirise/nav.js); elsewhere the script 404s and the nav stays as it was.
(function () {
  var nav = document.querySelector('.nav-links');
  if (!nav || nav.querySelector('a[href="/lexirise"]')) return;
  var link = document.createElement('a');
  link.href = '/lexirise';
  link.textContent = 'Lexirise';
  nav.appendChild(link);
})();

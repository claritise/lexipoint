#pragma once

// The /lexirise page and its JSON API on CrossPoint's web server (settings.md §1a-2):
//   GET  /lexirise              the page
//   GET  /lexirise/nav.js       adds the "Lexirise" link to CrossPoint's pages' nav bar
//   GET  /api/lexirise          settings (key masked), choices, and the key status
//   POST /api/lexirise          a partial update; a new key is checked straight away
//   POST /api/lexirise/test     re-checks the key
// Handlers run on the main task (CrossPointWebServerActivity's loop).

class WebServer;

namespace lexipoint::web {

void registerRoutes(WebServer& server);

}  // namespace lexipoint::web

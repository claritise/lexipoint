# Offline, errors, fallback

**Status:** proposed 2026-09-24. Decisions D3, D10, D11 in `00-overview.md`.

Related: `lookup-flow.md` §4 (the chain), `lexirise-client.md` (status codes), `popup-ui.md` §4
(strings).

---

## 0. Principle

**A lookup always answers something.** If Lexirise can't answer, StarDict does. If StarDict can't
either, the user gets a short, specific message (never a spinner that doesn't end). A **save**
never fails silently: it either succeeds visibly or leaves a retry on screen.

## 1. Lookup failures

| Condition | Detected | Provider returns | User sees |
|---|---|---|---|
| Lexirise disabled / no key | Config at boot | `Unavailable` | StarDict. `No Lexirise key` once per boot |
| WiFi not configured | `WifiCredentialStore` empty | `Unavailable` | StarDict |
| WiFi connect fails or takes > 6 s | `WifiSession::ensureUp` | `Unavailable` | StarDict, with the `offline` mark on its title |
| TLS pre-flight fails (low internal heap) | `MIN_TLS_FREE_HEAP` check | `Unavailable` | StarDict. Logged with heap numbers |
| Timeout (connect, handshake or read, 6 s each) | `TlsConnection` / `LexiriseClient` | `Unavailable` | StarDict, `offline` |
| Clock not set (no NTP answer within 5 s) | `TlsConnection` | `Unavailable` | StarDict, `offline` |
| Certificate or hostname verification fails | `TlsConnection` | `Unavailable` | StarDict. Logged with the wolfSSL error code |
| **401 / 403** | Status | `Unavailable` **and the provider disables itself until reboot** | `Lexirise key rejected` once, then StarDict |
| **429** | Status | `Unavailable`, with a back-off of `Retry-After` (or 60 s) during which Lexirise isn't tried | `Rate limited`, then StarDict |
| 5xx | Status | `Unavailable` | StarDict, `offline` |
| Response over a limit or malformed | Parser | `Unavailable` | StarDict. Log the first 128 bytes of the body (it holds no key) |
| No word-like occurrence at the tap | Match step | `NotFound` | `Not in dictionary` (1.5 s popup, existing style) |
| `analyze` OK, `dictionary/lookup` fails | Phase B | Card stays at phase A | Translation row shows `offline`. The card is still usable, and Save sends an empty `translation` |
| `translation_status: pending` | Phase B | Card at B′ | `translation pending` |

If StarDict is also unavailable (no dictionary selected), the existing `STR_DICT_*` messages
apply unchanged.

## 2. The offline mark

A small `offline` label in the header of whichever view answered: the Lexirise card at phase A,
or the StarDict definition screen. It means *"this isn't your Lexirise data. Saved state is
unknown."* It's the only place the fallback is visible. It shouldn't be louder than that.

## 3. Save failures

| Condition | User sees | Confirm |
|---|---|---|
| Network / timeout / 5xx | `Save failed · ⏎ retry` | Retries |
| 401/403 | `Key rejected` | Nothing (disabled until reboot) |
| 429 | `Rate limited · try in Ns` | Retries after N |
| *(no duplicate case)* | `POST /v1/vocabulary` is an upsert, so a word that was saved elsewhere since the lookup just returns 2xx | — |
| Any 2xx | `Saved · tracked` | — |

A double press must not save twice: while `Saving…` shows, Confirm is ignored.

## 4. The offline save queue (stretch, not v0.1)

When Save is pressed with no network:

- Append one JSON line (the exact `/v1/vocabulary` body) to `/.lexirise/queue.jsonl`, and show
  `Queued`.
- Flush on the next successful `WifiSession::ensureUp`. POST each line and rewrite the file with
  the ones that failed. Stop at the first 401 or 429.
- The saved state shown for queued words is `Queued`, so the card never claims `Saved` for
  something the server doesn't have yet.

This is the only part of the design that writes user data to SD besides the config, so it gets its
own spec before it's built.

## 5. WiFi lifecycle (D10)

`WifiSession` owns it for the reader (rules in `src/lexirise/net/WifiLease.h`, revised in P1 review):

- `ensureUp()`: a connected station (anyone's) → use it. **Radio off** → join the last-used saved
  network (`WifiCredentialStore`, the store `WifiSelectionActivity` writes) within 6 s. **Radio on but
  not connected** (the web server's hotspot, someone else's join in progress) → `Busy`, and the radio is
  not touched. **Never open the WiFi selection UI from a lookup.** No network, a failed join, or `Busy`
  → `NoWifi` → `Unavailable`.
- `WiFi.setSleep(false)` while up (KOSync does this for the same reason: modem sleep stalls show
  up as HTTP timeouts).
- **Ownership is explicit.** Lexipoint owns WiFi only if it brought it up from radio-off, and keeps it
  across the driver's own reconnects. It gives it back after the idle time (`wifi_idle_min`, default
  5 min) **or as soon as the screen leaves reading** (no reader activity on screen or under it: the
  reader's menus, word select and the card keep it). An ActivityManager hook reports this *before* the
  next activity's `onEnter`, so KOSync, the web server, OTA, ... start with the radio off and bring WiFi
  up themselves: Lexipoint can never turn WiFi off under another feature. If Lexipoint's own link
  drops, it rejoins (it's still its radio) rather than calling it busy. Before deep sleep upstream
  already turns WiFi off. **While the card is open the idle time doesn't run** (`holdWifi`, P5): the card
  makes one call per loop pass, so with `wifi_idle_min=0` ("connect each time") WiFi would otherwise go
  down between its phase A and B, every step and every save. It's once per card; when the card closes
  the idle time counts from then.
- **The TLS session is closed after 30 s idle**, on WiFi teardown, and on leaving reading, so it never
  holds internal heap that upstream TLS users pre-flight for.

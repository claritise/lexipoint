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

`WifiSession` owns it for the reader:

- `ensureUp()`: already connected → return. Otherwise connect using the stored credentials
  (`WifiCredentialStore`, the same store `WifiSelectionActivity` writes) with a 6 s budget.
  **Never open the WiFi selection UI from a lookup.** If no network is configured, return
  `Unavailable`.
- `WiFi.setSleep(false)` while up (KOSync does this for the same reason: modem sleep stalls show
  up as HTTP timeouts).
- **Idle teardown** 5 min after the last Lexirise call → `esp_wifi_stop()`. Also tear down when
  leaving the reader, and before deep sleep.
- **Don't fight other WiFi users.** If KOSync, OTA, or the web server brought WiFi up, `WifiSession`
  doesn't own it and doesn't tear it down.

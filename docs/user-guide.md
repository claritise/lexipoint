# Lexipoint: setup guide

Lexipoint is CrossPoint (the open e-reader firmware) for the **Xteink X4 Pro**, plus **Lexirise** lookups
for **Japanese and Simplified Chinese** books: long-press a word and a card shows its reading, meaning and
frequency, and one tap saves it to your Lexirise account at the level you choose. Without WiFi (or without a
key) the offline StarDict dictionaries you put on the SD card answer instead.

Everything CrossPoint does keeps working the same way. Lexipoint only adds the lookup.

---

## 1. Install

1. Download `crosspoint-<version>-lexi.<n>-x4pro.bin` from the **Releases** page of
   `github.com/claritise/crosspoint-reader`.
2. Flash it the way CrossPoint documents (its README, *Install firmware*): the web installer's
   **Custom .bin** option, or `esptool.py --chip esp32s3 ... write_flash 0x10000 <file>.bin`.
   Read CrossPoint's warning about USB-locked units first. **After an over-the-air or SD-card update** the
   device may boot its second firmware slot, so a plain `write_flash 0x10000` could be ignored: erase the boot record
   too (`esptool.py --chip esp32s3 ... erase_region 0xe000 0x2000`), or use the web installer or
   CrossPoint's SD-card firmware update.
3. Later updates arrive **over the air**: Settings → System → *Check for updates* offers the newest
   Lexipoint release. It never offers upstream CrossPoint's releases, which would remove
   Lexipoint. To go back to stock CrossPoint, flash its release the same way.

## 2. A font that has Japanese and Chinese

CrossPoint's own fonts have no CJK characters, so Japanese and Chinese books (and the card) show boxes
until you add one. What works (tested on an X4 Pro):

1. Get **`NotoSerifCJKjp-Regular.otf`** from `github.com/notofonts/noto-cjk` (`Serif/OTF/Japanese/`). It
   holds every CJK ideograph, with Japanese letterforms that read fine in Chinese books too. (For mainland
   letterforms, convert `NotoSerifCJKsc` the same way and switch fonts per book.)
2. Convert it with the firmware's script (`pip install freetype-py fonttools`, about 40 s):
   ```
   python3 lib/EpdFont/scripts/fontconvert_sdcard.py NotoSerifCJKjp-Regular.otf \
     --intervals latin-ext,punctuation,cjk --sizes 8,10,12,14,16,18 \
     --style regular --name NotoSerifCJK --output-dir ./NotoSerifCJK/
   ```
   Keep sizes 8, 10 and 12: CrossPoint draws CJK book titles in its menus with them, and the card uses
   8, 10 and 18.
3. Copy the `NotoSerifCJK` folder to `/fonts/` on the SD card (USB Drive mode or the file manager).
   **Eject before unplugging**: an unplug mid-copy truncates the files.
4. On the reader: **Settings → Reader → Text Settings → Reader Font Family → NotoSerifCJK**.

Rare characters outside the main CJK block (Extension A, and a few like 𠮟) still show as boxes.
A Japanese font can lack some Simplified characters (这, 们, 说 …): if a Chinese book shows boxes, use
the `sc` font for it.

## 3. WiFi and your Lexirise key

1. **WiFi:** Settings → System → *Wi-Fi Networks*, as in CrossPoint. Lexipoint joins it for a lookup and
   lets it go after *Keep WiFi on after a lookup* (5 minutes by default).
2. **A key:** create an API key in your Lexirise account (API keys need a **Pro** plan). Keys look like
   `lx_…`.
3. **Paste it from a phone or laptop**, not the e-ink keyboard: on the reader, open **File Transfer**
   (CrossPoint's network mode), then open the address it shows in a browser on the same WiFi and choose **Lexirise** in
   the menu (Home · File Manager · Settings · Fonts · Lexirise). Paste the key and save: the page tests it at once
   (`Connected as <you> (<plan>)`, `Key rejected`, or `No internet`).
4. The device shows the same settings under **Settings → System → Lexirise**: the key (masked,
   `lx_••••••••abc`), the account, *Test connection*, each language's switches, and the rest (§6). You can
   type a key there too, but it's long.

## 4. Offline dictionaries (optional, recommended)

When Lexirise can't answer (no WiFi, no key, a switched-off language), CrossPoint's StarDict dictionary
does, and its title says `· offline`. Put StarDict dictionaries in folders under `/dictionaries/` on the
card (CrossPoint's `docs/dictionary.md` lists sources; JMdict for Japanese and CC-CEDICT for Chinese work
well). Then either:

- choose one for everything in CrossPoint's **Settings → Reader → Dictionary**, or
- give each language its own: **Settings → System → Lexirise → Japanese / Chinese → Offline dictionary**.
  A Japanese or Chinese word then uses its language's dictionary, and any other word (an English word in a
  Japanese book, say) still uses CrossPoint's. Traditional Chinese books always use CrossPoint's.

The first lookup in a new dictionary builds its index once (`Indexing dictionary...`).

## 5. Looking words up

- **Long-press a word** on the page. (A long-press anywhere else does nothing; a quick tap still opens the
  menu in the middle and turns the page at the sides.) The card opens at the bottom: the word, its reading,
  meaning and how common it is, filling in as Lexirise answers.
- **Save it:** tap **T L F K** (tracked, learning, fresh, known). The toast offers **Undo** for 2 seconds.
  Tapping another level on a saved word changes it.
- **More:** tap the arrow on the rank row, or swipe up on the card, for the detail view (Meaning,
  Examples, Context, Kanji or Characters, Form, ⋯). Swipe left or right on it to change tabs; swipe down to
  go back.
- **Next and previous word:** the side page buttons step through the sentence while the card is open, and
  on into the page's next sentence at its end.
- **Readings:** tap the reading line (Japanese) to switch kana ⇄ romaji; it's remembered.
- **Another word:** tap it (or long-press it) on the page while the card is open.
- **Close:** ✕, Home, the Back swipe from the left edge, swipe down on the card, or tap the page away from
  the words. You're back on the page you were reading.

Languages: a book's `<dc:language>` decides; a book that doesn't say is read by its text (kana means
Japanese; Han-only text uses **Language when a book doesn't say**, or, with Lexirise lookups on, the only
language you have on).
If a book gets it wrong (a Chinese book looked up as Japanese), open the reader menu and set **Lookup
language** to that book's language; it's remembered for that book (Auto goes back to the rule above).

## 6. Settings (Settings → System → Lexirise)

| Group | Setting | |
|---|---|---|
| Account | Lexirise lookups | Off: offline dictionaries only, no network |
| | API key, Account, Test connection | §3 |
| Japanese | Lookups · Readings (kana / romaji) · Offline dictionary | |
| Chinese (Simplified) | Lookups · Offline dictionary | |
| General | Language when a book doesn't say (unless Lexirise is on with just one language on) · Tags · Keep WiFi on after a lookup | |

A language's rows hide while its Lookups are off (its Offline dictionary stays: it answers that language's
taps then), and come back with their values. With Lexirise lookups off, the screen keeps the Account group,
both Offline dictionaries and Language when a book doesn't say. **Tags** are added to
every word you save (default `xteink`); Lexirise can't delete tags, so choose them with care. The server
address is on the web page only (under Advanced), for a local proxy; changing it asks for the key again.

## 7. Privacy and safety

- **What's sent:** for each lookup, the sentence around the tapped word and its language, to Lexirise
  (`api.lexirise.app`) over TLS, which the device verifies. Saving sends the word, its meaning, your level, the tags you set, and the sentence (as the saved word's
  note, so you see where you met it).
  Nothing else about your books or reading is sent.
- **The key** is stored in `/.lexirise/config.ini` on the SD card **in plain text**, like CrossPoint's
  WiFi passwords. Anyone with the SD card has it: if you lose the card, revoke the key in your Lexirise
  account and make a new one. The web file manager can't read or replace that folder, and the key is never
  shown in full or logged.
- **Use File Transfer on networks you trust.** CrossPoint's web pages have no login: anyone on the same
  WiFi can open them while File Transfer is on. They can replace your key, never read it. Other websites
  open in your browser can't change it.

## 8. When something's off

| You see | Meaning |
|---|---|
| `No Lexirise key` (once per boot) | No key set: the offline dictionary answers |
| `Lexirise key rejected` | The key was revoked or mistyped: paste a new one (lookups stay offline until then or a reboot) |
| `Lexirise: rate limited` | Too many requests for a while: offline until it passes |
| `· offline` in a definition's title | No WiFi or no answer: this is the offline dictionary |
| `Save failed · Retry` on the card | The save didn't reach Lexirise: tap Retry |
| `Lexirise: not saved` after the card | A save couldn't be sent as the card closed: save it again |
| `No dictionary set` | Neither Lexirise nor an offline dictionary could answer |
| Boxes instead of characters | No CJK font: §2 |

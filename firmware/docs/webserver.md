# Web Server Guide

This guide explains how to use CrossPoint Reader's built-in web server for file
transfer, device settings, Wi-Fi management, and SD-card font management.

## Overview

The web server is available while the device is in **File Transfer** mode. It
can:

- Upload, download, rename, move, and delete files on the SD card
- Create folders
- Edit many device settings from a browser
- Manage saved Wi-Fi networks
- Upload and delete `.cpfont` SD-card font families

The server does not require authentication. Use it only on trusted private
networks or in hotspot mode when you control who is connected.

## Starting File Transfer

1. From the Home screen, select **File Transfer**.
2. Choose one of the available modes:

| Mode | Use when |
|------|----------|
| **Join Network** | You want the reader to join an existing Wi-Fi network. |
| **Create Hotspot** | You want the reader to create its own open Wi-Fi network. |
| **USB Drive** | You want the SD card to show up on a computer over USB. |

## Join Network Mode

1. Select **Join Network**.
2. If you have saved Wi-Fi credentials, CrossPoint first tries the last
   connected network, then other visible saved networks in signal-strength
   order. Press **Back** to cancel or **Confirm** to stop auto-connect and show
   the network list.
3. If the network list is shown, pick a 2.4 GHz Wi-Fi network from the scan
   results.
4. Enter the password if prompted.
5. Save credentials if you want the reader to reconnect automatically next time.

After connection, the reader shows:

- The connected SSID
- A QR code for the web URL
- The direct IP URL, for example `http://192.168.1.102/`
- The mDNS fallback URL, usually `http://lexipoint.local/`

Use either URL from a phone, tablet, or computer on the same network.

## Create Hotspot Mode

1. Select **Create Hotspot**.
2. Connect your phone or computer to the open Wi-Fi network:

```text
Lexipoint
```

3. Open the URL shown on the reader. `http://lexipoint.local/` is preferred
   when supported; the fallback IP is typically `http://192.168.4.1/`.

The reader displays one QR code for joining the hotspot and another QR code for
opening the web interface.

## Web Interface

The browser UI has four primary pages, plus Lexipoint's **Lexirise** page.

### Home

The Home page shows firmware status, network mode, IP address, device type,
uptime, and free heap.

### File Manager

The File Manager page can:

- Browse SD-card folders
- Upload files, using WebSocket upload when available and HTTP upload as a fallback
- Create folders
- Download files
- Rename files
- Move files into existing folders
- Delete one or more selected files or empty folders

Existing files with the same name are overwritten by uploads. When EPUB files
are overwritten, moved, renamed, or deleted through the web server, the matching
book cache is cleared so stale metadata is not reused.

### Settings

The Settings page exposes many firmware settings in the browser. It also has a
card for saved Wi-Fi networks. Passwords are accepted when adding or editing entries, but saved passwords are
not returned by the API.

### Fonts

The Fonts page lists installed SD-card font families and lets you upload
`.cpfont` files. Upload files from one font family at a time. The server validates
the font family name, filename, and `.cpfont` magic bytes before accepting the
upload.

Installed fonts appear in **Settings > Reader > Font Family** after the font
registry refreshes.

### Lexirise

The Lexirise page (`/lexirise`) holds the Lexirise key and settings. Its routes (`/lexirise`,
`/lexirise/nav.js`, `GET` and `POST /api/lexirise`, `POST /api/lexirise/test`) are listed in
`webserver-endpoints.md`; the page itself is documented in `../../docs/v0.1/settings.md` §1a and §2.

## Command Line Use

Power users can use `curl` or WebSocket clients while the web
server is running.

Endpoint details are documented in [webserver-endpoints.md](./webserver-endpoints.md).

## Security Notes

- The HTTP server runs on port 80.
- The WebSocket upload server runs on port 81.
- There is no authentication.
- Anyone on the same network can access the web interface while it is running.
- The server stops when you exit File Transfer mode.
- Hotspot mode creates an open network for connectivity fallback; disconnect when done.

## Tips

1. Use **Create Hotspot** when no trusted network is available.
2. Prefer `lexipoint.local` when available, but keep the displayed IP address as a fallback.
3. Move closer to the router if upload progress stalls in Join Network mode.
4. Upload custom fonts through the Fonts page or copy them to `/.fonts/` or `/fonts/` on the SD card.
5. Exit File Transfer mode when finished to conserve battery.

## Related Documentation

- [Lexipoint user guide](../../docs/user-guide.md)
- [Webserver Endpoints](./webserver-endpoints.md)
- [SD Card Fonts](./sd-card-fonts.md)
- [Troubleshooting](./troubleshooting.md)

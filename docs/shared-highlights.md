# Shared highlight archive (custom firmware)

This branch combines upstream touch-clipping PR #3589 (head `093a78b4`)
with a separate local archive uploader. It does not change reading-progress
credentials or sync behaviour.

## Configure

Put `highlight-sync.json` under `/.crosspoint/` on the SD card:

```json
{
  "endpoint": "http://Skyes-MacBook-Pro-2.local:8084/v1/highlights",
  "token": "REPLACE_WITH_A_DEDICATED_RANDOM_TOKEN_AT_LEAST_32_CHARACTERS",
  "device_id": "xteink-skye"
}
```

The token must match the collector. Never commit this real file or publish it.
The existing File Transfer tool can upload it to that hidden directory. Its
file-download capability also exposes SD files during a transfer session;
use only your trusted network and close File Transfer afterwards. The token
is never added to the generic settings API or printed in logs.

This first implementation accepts HTTP only on private IPv4 addresses or
`.local` hosts and refuses redirects. It is intended for a trusted home LAN,
not port forwarding or public hosting. The token is transmitted unencrypted
on that LAN. Use a separate token from all progress-sync/library credentials.

## Use on X4 Pro

1. Open an EPUB, tap the centre and open **More → Save Clipping**.
2. Touch and drag over the words, then release to save the selection.
3. **More → View Clippings** shows saved excerpts.
4. **More → Sync Highlights** connects to Wi-Fi if necessary and uploads all
   saved excerpts from the currently open book. Open another book and repeat
   if you saved excerpts there earlier.

The count advances only after the collector acknowledges each exact excerpt
ID. A failed request stops the operation; retry the same action when your Mac
is awake and reachable. Every source clipping remains on the SD card after
success, failure or cancellation. Repeated sync is safe: IDs depend on title,
author and complete excerpt text, not list index, page layout or filename.
An intentionally repeated identical passage in the same book is one quote.
There is no background Wi-Fi activation and no automatic upload on capture.

Clips are capped at 4 KiB / 256 per book by upstream selection/storage.
The upload uses the full binary stored text; the legacy `My Clippings.txt`
export has a separate 2 KiB limit. Clock values in upstream clipping storage
are uptime, so they are not sent as wall-clock dates. Collector receipt time
can be used for archive ordering. Inline highlight rendering is layout-bound;
the saved excerpt and archive survive layout changes. Archive deletes and
in-book annotation sync are outside this feature.

## Installation and validation

Build `pio run -e x4pro`. Before installing, back up the entire SD card including
hidden files, and preserve a known-good firmware backup. Install the app binary
using CrossPoint's documented SD firmware updater/OTA mechanism or the correct
app partition offset from the generated partition table. Do not flash the app
binary at address zero, erase flash, delete caches, or overwrite the SD card.
No firmware installation is performed by the build process.

Physical verification: save a short passage, reopen the book to verify durable
storage, sync it to the Mac, then repeat sync and verify one archive entry.
Turn off the Mac collector and verify an error leaves the clipping available;
restart the collector and retry. Check touch selection in all four orientations
and monitor free heap (>50 KiB). Existing reading position sync, fonts and
dictionaries must still work. Build/native tests cannot establish these
physical-device results.

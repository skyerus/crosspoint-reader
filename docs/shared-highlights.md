# Shared highlight archive (custom firmware)

This branch combines upstream touch-clipping PR #3589 with a local archive
uploader. Reading-progress credentials and behaviour stay separate.

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
File Transfer can upload it to that hidden directory. Its download capability
also exposes SD files during a transfer session; close File Transfer afterwards.
The token is not exposed through the generic settings API or printed in logs.

This implementation accepts HTTP only on private IPv4 addresses or `.local`
hosts and refuses redirects. Use a trusted home LAN, not port forwarding.
The dedicated token travels unencrypted on that LAN. Do not reuse progress-sync
or library credentials.

## Use on X4 Pro

1. In an EPUB, tap the centre, then **More → Save Clipping**.
2. Touch and drag over the words, then release to save.
3. **More → View Clippings** shows the saved excerpts. Long-press an item and
   confirm **Delete** to remove it locally and from the shared quote archive.

Saving and deleting create durable events covering every book, not just the
book currently open. While the reader is awake on a book or Home screen, it
quietly checks its queue (normally within 30 seconds), joins the last saved
Wi-Fi network when necessary, and sends the events to your Mac. Existing
clippings are backfilled gradually with a persistent checkpoint. No missing
file or incomplete scan is interpreted as a deletion.

There is no Wi-Fi popup. Reading/page turns continue while the task runs. It
tries a saved SSID directly for up to eight seconds, uses bounded mDNS lookup
and short HTTP timeouts, then turns off the radio if it enabled it. Failed
attempts back off from one minute to fifteen minutes; the retry deadline
survives deep sleep. Wake-up retries pending work when due. It does not wake a
sleeping device. Navigation to another activity and sleep cooperatively drain
the task before other Wi-Fi activities or USB drive mode can take ownership.
That handoff may briefly wait for an in-flight network request to finish.

**More → Sync Highlights** explicitly drains the same cross-book queue, including
deletions, and backfills existing clips. It stays available after the last clip
is deleted. It does not bypass deletion suppression in the central archive.

## Durability and deletion semantics

Source clips remain on SD after successful upload. The outbox removes an event
only after the collector acknowledges its exact ID. A lost response causes a
safe retry. The queue is ordered and independent of the currently open book.
Its directory scanner has no 200-file UI-list limit.

A clipping save/delete prepares an outbox intent before atomically replacing
the source file. Before/after file digests distinguish a completed change from
a rollback after reboot. Ambiguous/corrupt data keeps the intent rather than
publishing a speculative deletion. A failed queue write prevents the local
mutation. Do not delete `/.crosspoint/highlight-outbox/` while work is pending.

Identical title/author/text share one archive ID. Deleting one local duplicate
keeps the archive quote until the last local duplicate is deleted. The shared
archive's tombstones are global and permanent: deleting a quote suppresses old
copies arriving from another reader, the Amazon refresh, or a later rehighlight
of exactly the same passage. This version has no restore command. Explicit
user clipping deletion creates tombstones; moving or renaming a book does not.

Clips are capped at 4 KiB / 256 per book by upstream selection/storage. Uploads
use full binary stored text; legacy `My Clippings.txt` export has a separate
2 KiB limit. Uptime timestamps are not sent as wall-clock dates. Inline visible
highlights are layout-bound; saved text and the archive survive layout changes.

## Installation and verification

Build `pio run -e x4pro`. Back up the SD card including hidden files and preserve
known-good firmware. Copy `.pio/build/x4pro/firmware.bin` onto the SD card, then
choose **Settings → System → SD Card Firmware Update**. CrossPoint 1.6.0 already
supports this path and writes the inactive OTA slot after validating the image.
App0 is `0x10000`, app1 is `0x650000`, each `0x640000` bytes; manual USB flashing
must first establish the actual active/next slot. Never flash this application
binary at address zero, erase flash, or delete existing caches/data.

Physical verification: save a passage with Wi-Fi initially off, continue reading,
and check that it appears once in the Mac archive automatically. Delete it and
confirm archive disappearance. Repeat with the Mac offline, reboot/sleep the
reader, then make the Mac available and verify eventual delivery. Save passages
in two books before reconnecting and verify both. During pending work open
File Transfer/USB mode and check there is no concurrent-radio/storage failure.
Check touch selection in all four orientations, free internal heap (>50 KiB),
and existing reading-progress sync, fonts and dictionaries. Native fault tests
and successful firmware compilation do not establish physical-device results.

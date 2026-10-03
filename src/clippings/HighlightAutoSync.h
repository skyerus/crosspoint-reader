#pragma once
namespace HighlightAutoSync {
// Main-loop only. Eligible means the CURRENT activity is reader/home, not a
// network or USB child stacked above it. No renderer or CLIPPINGS access in task.
// Reclamation runs on the main loop, before starting a worker, only for queued
// work whose normal memory budget is not met. It must serialize with rendering.
void tick(bool eligible, void (*reclaimMemory)() = nullptr);
bool cancelAndReady();
void stopAndWait();
bool active();
}  // namespace HighlightAutoSync

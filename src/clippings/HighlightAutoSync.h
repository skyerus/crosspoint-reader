#pragma once
namespace HighlightAutoSync {
// Main-loop only. Eligible means the CURRENT activity is reader/home, not a
// network or USB child stacked above it. No renderer or CLIPPINGS access in task.
void tick(bool eligible);
bool cancelAndReady();
void stopAndWait();
bool active();
}  // namespace HighlightAutoSync

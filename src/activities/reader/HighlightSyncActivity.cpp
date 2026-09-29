#include "HighlightSyncActivity.h"

#include <I18n.h>
#include <Memory.h>
#include <WiFi.h>

#include "ClippingStore.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"

HighlightSyncActivity::HighlightSyncActivity(GfxRenderer& renderer, MappedInputManager& input, std::string title,
                                             std::string author)
    : UiListActivity("HighlightSync", renderer, input), title(std::move(title)), author(std::move(author)) {}

const char* HighlightSyncActivity::headerTitle() const { return tr(STR_SYNC_HIGHLIGHTS); }

void HighlightSyncActivity::onEnter() {
  UiListActivity::onEnter();
  total = CLIPPINGS.clippingCount();
  if (!config.load()) {
    status = StrId::STR_HIGHLIGHT_SYNC_CONFIG;
    requestUpdate();
    return;
  }
  wifiActivated = true;
  if (WiFi.status() == WL_CONNECTED) {
    uploading = true;
    return;
  }
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) {
    status = StrId::STR_HIGHLIGHT_SYNC_FAILED;
    return;
  }
  startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
    uploading = !result.isCancelled;
    status = uploading ? StrId::STR_SYNC_HIGHLIGHTS : StrId::STR_HIGHLIGHT_SYNC_FAILED;
    requestUpdate();
  });
}

void HighlightSyncActivity::loop() {
  UiListActivity::loop();
  if (!uploading) return;
  // One request per loop permits cancellation between excerpts. No background
  // worker can outlive this activity or access the clipping store after exit.
  const bool done = uploaded == total;
  const bool accepted = done || uploadHighlight(config, uploaded, title, author);
  {
    RenderLock lock(*this);
    if (!accepted) {
      uploading = false;
      status = StrId::STR_HIGHLIGHT_SYNC_FAILED;
    } else if (done || ++uploaded == total) {
      uploading = false;
      status = StrId::STR_HIGHLIGHT_SYNC_DONE;
    }
  }
  requestUpdate();
}

void HighlightSyncActivity::onBackButton() {
  uploading = false;
  finish();
}
void HighlightSyncActivity::activateIndex(int) { onBackButton(); }

void HighlightSyncActivity::onExit() {
  UiListActivity::onExit();
  if (wifiActivated) {
    WiFi.disconnect(false);
    silentRestartToReader();
  }
}

void HighlightSyncActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(freeink::ui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight),
                                                        0, static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.centeredText(I18N.get(status), screen.theme().bodyText);
  char count[48];
  snprintf(count, sizeof(count), "%u / %u", static_cast<unsigned>(uploaded), static_cast<unsigned>(total));
  screen.centeredText(count, screen.theme().bodyText);
  freeink::ui::ListItem row;
  row.label = tr(STR_BACK);
  freeink::ui::ListProps props;
  props.count = 1;
  props.action = ACTION_ROW;
  props.items = &row;
  syncListViewport(screen, props);
  screen.list(props);
}

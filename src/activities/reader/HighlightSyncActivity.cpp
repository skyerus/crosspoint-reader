#include "HighlightSyncActivity.h"

#include <I18n.h>
#include <Memory.h>
#include <WiFi.h>

#include "ClippingStore.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"

HighlightSyncActivity::HighlightSyncActivity(GfxRenderer& renderer, MappedInputManager& input)
    : UiListActivity("HighlightSync", renderer, input) {}

const char* HighlightSyncActivity::headerTitle() const { return tr(STR_SYNC_HIGHLIGHTS); }

void HighlightSyncActivity::onEnter() {
  UiListActivity::onEnter();
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
  std::string path;
  HighlightMutation mutation;
  bool accepted = false;
  bool done = false;
  bool failed = false;
  if (HighlightOutbox::next(path, mutation)) {
    accepted = uploadHighlightMutation(config, mutation) && HighlightOutbox::acknowledge(path);
    failed = !accepted;
  } else if (HighlightOutbox::pending()) {
    failed = true;
  } else {
    const int seeded = ClippingStore::seedOneArchiveClipping();
    failed = seeded < 0;
    done = seeded == 0;
  }
  {
    RenderLock lock(*this);
    if (accepted) ++uploaded;
    if (failed || done) {
      uploading = false;
      status = failed ? StrId::STR_HIGHLIGHT_SYNC_FAILED : StrId::STR_HIGHLIGHT_SYNC_DONE;
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
  snprintf(count, sizeof(count), tr(STR_HIGHLIGHTS_UPLOADED_COUNT), static_cast<unsigned>(uploaded));
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

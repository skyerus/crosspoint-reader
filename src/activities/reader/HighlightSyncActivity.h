#pragma once
#include <I18n.h>

#include "activities/UiListActivity.h"
#include "clippings/HighlightSync.h"

class HighlightSyncActivity final : public UiListActivity {
 public:
  HighlightSyncActivity(GfxRenderer& renderer, MappedInputManager& input, std::string title, std::string author);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool handleHomeGesture() override {
    onBackButton();
    return true;
  }
  bool preventAutoSleep() override { return uploading; }

 private:
  int listCount() const override { return 1; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  HighlightSyncConfig config;
  std::string title;
  std::string author;
  size_t uploaded = 0;
  size_t total = 0;
  bool uploading = false;
  bool wifiActivated = false;
  StrId status = StrId::STR_SYNC_HIGHLIGHTS;
};

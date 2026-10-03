#pragma once
struct TestSettings {
  int getReaderFontId() const { return 1; }
};
inline TestSettings clipTestSettings;
#define SETTINGS clipTestSettings

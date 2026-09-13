#pragma once

// Host stand-in for the UI activity base class. SettingsActivity.h (pulled in
// for the SettingInfo type and getSettingsList()) derives from this; the
// suite never instantiates an activity, so the stub only has to satisfy the
// override declarations in the class body.

#include <string>
#include <vector>

class GfxRenderer;
class MappedInputManager;
class UiScreen;
class RenderLock;

namespace freeink::ui {

struct ListItem {
  std::string label;
  std::string value;
};

}  // namespace freeink::ui

class UiTabListActivity {
 public:
  virtual ~UiTabListActivity() = default;
  virtual void onEnter() {}
  virtual void onExit() {}
  virtual void render(RenderLock&&) {}

 protected:
  UiTabListActivity() = default;

  virtual int listCount() const = 0;
  virtual int tabCount() const = 0;
  virtual int activeTab() const = 0;
  virtual const char* tabLabel(int index) const = 0;
  virtual void buildScreen(UiScreen& screen) = 0;
  virtual void activateIndex(int index) = 0;
  virtual void onTabAction(int index) = 0;
  virtual void stepTab(int direction) = 0;
  virtual bool handleButtons() = 0;
  virtual bool handleCustomInput() = 0;
};

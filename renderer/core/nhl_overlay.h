// nhllegacy — in-game enhancements / settings overlay (Vulkan-fsi path).
//
// An ImGui dialog that surfaces the "cheap win" enhancement knobs (internal-res
// supersampling, perf HUD) over the live game. It is ALWAYS registered with the
// SDK ImGui drawer so its per-frame OnDraw can poll the controller Guide/PS
// button and toggle visibility even while the window itself is hidden — that
// per-present tick is the only reliable host-side hook for the pad button.
//
// Backend-agnostic by design: it takes the controller poll and the perf source
// as std::function hooks (the app wires them to the Vulkan fps tap +
// rex::input::InputSystem), so this TU pulls in no backend headers. Toggled by
// the Guide/PS button or the F1 keybind (rebindable, shows in the SDK settings
// overlay); adjusted with the mouse.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <rex/ui/imgui_dialog.h>

#include "renderer/core/nhl_tunable_store.h"

namespace nhl::ui {

// Plain snapshot the overlay displays; the app fills it from the Vulkan
// IssueSwap fps/draws tap (nhl::graphics::ReadVkPerf).
struct PerfSnapshot {
  double fps = 0.0;
  double frame_ms = 0.0;
  double draws_per_frame = 0.0;
  uint64_t frames_total = 0;
  bool valid = false;
};

// Aggregated controller state the overlay needs for the Guide-button toggle and
// ImGui gamepad navigation. The app fills it from rex::input::InputSystem
// (OR-ing buttons across all pads; left stick from the most-deflected pad), so
// this TU stays free of any backend/input header. `buttons` uses the standard
// XInput bit layout (== rex::input::X_INPUT_GAMEPAD_* values).
struct PadState {
  bool connected = false;
  uint16_t buttons = 0;
  float lx = 0.0f;  // left stick X, normalized -1..1 (+ = right)
  float ly = 0.0f;  // left stick Y, normalized -1..1 (+ = up)
};

class NhlEnhancementsDialog : public rex::ui::ImGuiDialog {
 public:
  // poll_pad:       returns the aggregated controller state (Guide button rising
  //                 edge toggles the overlay; buttons + stick drive ImGui nav).
  // perf:           returns the latest perf snapshot for the HUD section.
  // on_exit:        invoked when the user confirms "Exit Game" (clean quit).
  // set_fullscreen: requests borderless-fullscreen vs windowed (must marshal to
  //                 the UI thread — the app wires that). Null on non-VK paths.
  // is_fullscreen:  returns the live fullscreen state for the toggle's display.
  // tunables:       live engine-tunable store for the "Engine Tunables" section
  //                 (null disables the section). Owned by the app.
  // dev_scan_stick_list: fire-and-forget dev hook — scans live guest memory for
  //                 the create-player stick picker's item list, writing a report
  //                 next to the exe. Surfaced under "Developer Tools"; null hides
  //                 the button. Invoke it WHILE the stick picker is on-screen.
  NhlEnhancementsDialog(rex::ui::ImGuiDrawer* drawer,
                        std::function<PadState()> poll_pad,
                        std::function<PerfSnapshot()> perf,
                        std::function<void()> on_exit,
                        std::function<void(bool)> set_fullscreen = {},
                        std::function<bool()> is_fullscreen = {},
                        ITunableStore* tunables = nullptr,
                        std::function<void()> dev_scan_stick_list = {});
  ~NhlEnhancementsDialog();

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  // Feed the current pad state into ImGui's gamepad-nav input queue.
  void FeedGamepadNav(ImGuiIO& io, const PadState& pad);

  // "Engine Tunables" section (live World-B constant editor).
  void DrawTunables();
  // Recompute filtered_ from the current search text + group selection.
  void RefreshTunableFilter();

  std::function<PadState()> poll_pad_;
  std::function<PerfSnapshot()> perf_;
  std::function<void()> on_exit_;
  std::function<void(bool)> set_fullscreen_;
  std::function<bool()> is_fullscreen_;
  ITunableStore* tunables_ = nullptr;
  std::function<void()> dev_scan_stick_list_;

  // "Developer Tools" section (RE diagnostics over the live guest).
  void DrawDevTools();

  bool visible_ = false;
  bool prev_guide_ = false;
  bool confirm_exit_ = false;
  bool dev_stick_scan_fired_ = false;  // latches the one-shot scan trigger

  // Tunable browser state.
  char tun_filter_[96] = {0};   // case-insensitive name substring
  int tun_group_ = 0;           // 0 = All, else group index + 1
  bool tun_hex_ = false;        // edit every row as raw hex
  bool tun_only_overridden_ = false;
  bool tun_dirty_filter_ = true;        // filtered_ needs a rebuild
  std::string tun_last_filter_;         // detects filter-text changes
  std::vector<int> tun_filtered_;       // entry indices passing the filter
};

}  // namespace nhl::ui

#pragma once
#include <cstdint>

namespace dxmt {

class CaptureState {
public:
  enum class NextAction {
    StartCapture,
    StopCapture,
    Nothing,
  };
  NextAction getNextAction(uint64_t frame);
  void scheduleNextFrameCapture(uint64_t next = 0);
  // Frames per capture (DXMT_CAPTURE_FRAME_COUNT, default 1).
  void setFramesPerCapture(uint64_t count) { frames_per_capture_ = count ? count : 1; }
  bool shouldCaptureNextFrame();

  CaptureState();

private:
  enum class State {
    Freezed,
    Idle,
    WillCapture,
    Capturing,
  };
  State state = State::Freezed;
  uint64_t next_capture_frame = 0;
  uint64_t frames_per_capture_ = 1;
  bool capture_key_pressed_ = false;
};

} // namespace dxmt

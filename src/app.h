// Minimal CefApp implementation. Required by CEF to bootstrap the
// browser process and any helper subprocesses.
#pragma once
#include "include/cef_app.h"

class LiteBrowserApp : public CefApp {
 public:
  LiteBrowserApp() = default;

  // V3.1: fixes the known CEF issue where HTML5 video renders as a blank
  // white rectangle (audio still plays) when the browser is embedded in a
  // custom-painted parent window like ours. This happens because CEF's GPU
  // video overlay path doesn't composite correctly on top of our own
  // GDI-drawn chrome. Disabling GPU compositing forces video through the
  // normal software compositor instead of the broken overlay swap chain.
  void OnBeforeCommandLineProcessing(
      const CefString& process_type,
      CefRefPtr<CefCommandLine> command_line) override {
    command_line->AppendSwitch("disable-gpu-compositing");
  }

 private:
  IMPLEMENT_REFCOUNTING(LiteBrowserApp);
};

// Minimal CefApp implementation. Required by CEF to bootstrap the
// browser process and any helper subprocesses.
#pragma once

#include "include/cef_app.h"

class LiteBrowserApp : public CefApp {
 public:
  LiteBrowserApp() = default;

 private:
  IMPLEMENT_REFCOUNTING(LiteBrowserApp);
};

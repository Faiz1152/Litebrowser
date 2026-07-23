// Minimal CefApp implementation. Required by CEF to bootstrap the
// browser process and any helper subprocesses.
#pragma once

#include "include/cef_app.h"
#include "include/cef_render_process_handler.h"
#include "include/cef_v8.h"

class LiteBrowserApp : public CefApp,
                       public CefRenderProcessHandler {
 public:
  LiteBrowserApp() = default;

  // V3.1: fixes the known CEF issue where HTML5 video renders as a blank
  // white rectangle (audio still plays) when the browser is embedded in a
  // custom-painted parent window like ours. Disabling GPU compositing
  // forces video through the normal software compositor instead of the
  // broken overlay swap chain.
  void OnBeforeCommandLineProcessing(
      const CefString& process_type,
      CefRefPtr<CefCommandLine> command_line) override {
    command_line->AppendSwitch("disable-gpu-compositing");
  }

  // V4: this object also acts as the render-process handler.
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
    return this;
  }

  void OnContextCreated(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefV8Context> context) override {
    // REMOVED: All YouTube initialization code
    // 
    // REASON: Running JavaScript during OnContextCreated interferes with
    // YouTube's initialization pipeline BEFORE the DOM is ready. This causes:
    // 1. YouTube's renderer to fail silently
    // 2. Header elements to never be created in the DOM
    // 3. Subsequent attempts to restore visibility to fail (elements don't exist)
    //
    // Instead, we handle all YouTube fixes in OnLoadEnd() after the page
    // has fully rendered and all elements are created.
    //
    // The sanitizer was particularly problematic because it hijacked
    // window.ytInitialPlayerResponse via getter/setter before YouTube's
    // code could read it, breaking the entire initialization sequence.
  }

 private:
  IMPLEMENT_REFCOUNTING(LiteBrowserApp);
};

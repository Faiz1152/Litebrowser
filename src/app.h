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

  // V4: this object also acts as the render-process handler (runs inside
  // the renderer subprocess, which for this build is the same exe
  // re-launched by CEF via CefExecuteProcess in main.cpp).
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
    return this;
  }

  // Fires the instant a page's V8 JS context is created — before any of
  // the page's own scripts have run. This is what makes it possible to
  // sanitize YouTube's ad data before the player ever reads it, instead
  // of trying (and failing, as we saw) to block ad requests at the
  // network layer after the fact.
  void OnContextCreated(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefV8Context> context) override {
    std::string url = frame->GetURL().ToString();

    if (url.find("youtube.com") == std::string::npos)
      return;

    // Strips adPlacements/playerAds/adSlots from YouTube's internal
    // ytInitialPlayerResponse object (both the initial page-load value
    // and any later values set dynamically), and from any /youtubei/v1/player
    // fetch responses (used for autoplay/next-video transitions), so the
    // player itself never learns an ad exists to show.
    static const char* kSanitizeJS = R"JS(
(function(){
  try {
    function clean(o){
      if(!o||typeof o!=='object') return o;
      if(o.adPlacements) o.adPlacements=[];
      if(o.playerAds) o.playerAds=[];
      if(o.adSlots) o.adSlots=[];
      if(o.adsControls) delete o.adsControls;
      if(o.playerResponse) clean(o.playerResponse);
      return o;
    }

    let orig = window.ytInitialPlayerResponse;
    Object.defineProperty(window, 'ytInitialPlayerResponse', {
      get: function(){ return orig; },
      set: function(v){ orig = clean(v); },
      configurable: true
    });

    const originalFetch = window.fetch;
    window.fetch = async function(...args){
      const response = await originalFetch(...args);
      const url = (args[0] && typeof args[0] === 'string') ? args[0] : '';
      if (url.indexOf('/youtubei/v1/player') !== -1) {
        const clone = response.clone();
        try {
          let json = await clone.json();
          json = clean(json);
          return new Response(JSON.stringify(json), response);
        } catch (e) {
          return response;
        }
      }
      return response;
    };
  } catch (e) {}
})();
)JS";

    frame->ExecuteJavaScript(kSanitizeJS, frame->GetURL(), 0);
  }

 private:
  IMPLEMENT_REFCOUNTING(LiteBrowserApp);
};

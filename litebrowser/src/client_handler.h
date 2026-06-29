// ClientHandler wires up the browser window lifecycle: knowing when it's
// ready, when it closes, and keeping a reference to it so main.cpp can
// resize it and send it new addresses to load.
#pragma once

#include "include/cef_client.h"

class ClientHandler : public CefClient,
                       public CefLifeSpanHandler,
                       public CefDisplayHandler {
 public:
  ClientHandler() = default;

  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }

  // CefLifeSpanHandler
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

  // CefDisplayHandler - keep window title plain; we don't show a title bar
  // address, but this is here in case you want it later.
  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                      const CefString& title) override {}

  CefRefPtr<CefBrowser> GetBrowser() { return browser_; }

 private:
  CefRefPtr<CefBrowser> browser_;

  IMPLEMENT_REFCOUNTING(ClientHandler);
};

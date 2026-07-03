#pragma once
#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_display_handler.h"
#include "include/cef_request_handler.h"
#include "include/cef_resource_request_handler.h"
#include <string>

extern bool g_blockAds;
extern bool g_blockTrackers;
extern bool g_blockPopups;
extern bool g_focus;

void OnBrowserCreated(int tabId, CefRefPtr<CefBrowser> browser);
void OnTitleChanged(int tabId, const std::wstring& title);
void OnUrlChanged(int tabId, const std::wstring& url);
void AddHistory(const std::wstring& title, const std::wstring& url);

class ClientHandler : public CefClient,
                      public CefLifeSpanHandler,
                      public CefDisplayHandler,
                      public CefRequestHandler,
                      public CefResourceRequestHandler {
public:
    explicit ClientHandler(int tabId) : tab_id_(tabId) {}

    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefDisplayHandler>  GetDisplayHandler()  override { return this; }
    CefRefPtr<CefRequestHandler>  GetRequestHandler()  override { return this; }

    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

    void OnTitleChange(CefRefPtr<CefBrowser> browser,
                       const CefString& title) override;
    void OnAddressChange(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         const CefString& url) override;

    CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
        CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>,
        bool, bool, const CefString&, bool&) override { return this; }

    ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                                      CefRefPtr<CefRequest>,
                                      CefRefPtr<CefCallback>) override;

    CefRefPtr<CefBrowser> GetBrowser() { return browser_; }

private:
    int tab_id_;
    CefRefPtr<CefBrowser> browser_;
    static bool IsBlocked(const std::string& url);
    IMPLEMENT_REFCOUNTING(ClientHandler);
};

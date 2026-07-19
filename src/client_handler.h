#pragma once
#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_display_handler.h"
#include "include/cef_request_handler.h"
#include "include/cef_resource_request_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_urlrequest.h"
#include <string>
#include <vector>
#include <windows.h>

extern bool g_blockAds;
extern bool g_blockTrackers;
extern bool g_blockPopups;
extern bool g_focus;

void OnBrowserCreated(int tabId, CefRefPtr<CefBrowser> browser);
void OnTitleChanged(int tabId, const std::wstring& title);
void OnUrlChanged(int tabId, const std::wstring& url);
void OnFaviconReady(int tabId, HBITMAP bmp);
void AddHistory(const std::wstring& title, const std::wstring& url);

// V3.1: loads blocklist.txt (baked in at build time) into memory. Call once at startup.
void LoadBlocklist();

class FaviconFetcher : public CefURLRequestClient {
public:
    FaviconFetcher(int tabId) : tab_id_(tabId) {}
    void OnRequestComplete(CefRefPtr<CefURLRequest> request) override;
    void OnUploadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
    void OnDownloadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
    void OnDownloadData(CefRefPtr<CefURLRequest>, const void* data, size_t size) override {
        data_.insert(data_.end(), (const uint8_t*)data, (const uint8_t*)data + size);
    }
    bool GetAuthCredentials(bool, const CefString&, int, const CefString&,
                            const CefString&, CefRefPtr<CefAuthCallback>) override { return false; }
    std::vector<uint8_t> data_;
    int tab_id_;
    IMPLEMENT_REFCOUNTING(FaviconFetcher);
};

class ClientHandler : public CefClient,
                      public CefLifeSpanHandler,
                      public CefDisplayHandler,
                      public CefRequestHandler,
                      public CefResourceRequestHandler,
                      public CefLoadHandler {
public:
    explicit ClientHandler(int tabId) : tab_id_(tabId) {}
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefDisplayHandler>  GetDisplayHandler()  override { return this; }
    CefRefPtr<CefRequestHandler>  GetRequestHandler()  override { return this; }
    CefRefPtr<CefLoadHandler>     GetLoadHandler()      override { return this; }

    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
    void OnTitleChange(CefRefPtr<CefBrowser> browser,
                       const CefString& title) override;
    void OnAddressChange(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         const CefString& url) override;
    void OnFaviconURLChange(CefRefPtr<CefBrowser> browser,
                            const std::vector<CefString>& icon_urls) override;

    // V3.1: popup blocking
    bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        int popup_id,
                        const CefString& target_url,
                        const CefString& target_frame_name,
                        CefLifeSpanHandler::WindowOpenDisposition target_disposition,
                        bool user_gesture,
                        const CefPopupFeatures& popupFeatures,
                        CefWindowInfo& windowInfo,
                        CefRefPtr<CefClient>& client,
                        CefBrowserSettings& settings,
                        CefRefPtr<CefDictionaryValue>& extra_info,
                        bool* no_javascript_access) override;

    // V3.1: CSS/JS ad-hiding injection once page finishes loading
    void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   int httpStatusCode) override;

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
    static bool IsYouTubeAd(const std::string& url);
    IMPLEMENT_REFCOUNTING(ClientHandler);
};

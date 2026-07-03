#include "client_handler.h"

static const char* kBlockList[] = {
    // Ads
    "doubleclick.net","googlesyndication.com","googleadservices.com",
    "adnxs.com","advertising.com","adroll.com","criteo.com","criteo.net",
    "outbrain.com","taboola.com","pubmatic.com","rubiconproject.com",
    "casalemedia.com","openx.net","moatads.com","amazon-adsystem.com",
    "adsafeprotected.com","media.net","sharethrough.com","bidswitch.net",
    // Trackers
    "google-analytics.com","googletagmanager.com","hotjar.com",
    "mixpanel.com","segment.com","segment.io","amplitude.com",
    "fullstory.com","heap.io","chartbeat.com","newrelic.com",
    "connect.facebook.net","bat.bing.com","analytics.twitter.com",
    nullptr
};

static const char* kFocusBlock[] = {
    "facebook.com","twitter.com","x.com","instagram.com","reddit.com",
    "youtube.com","tiktok.com","snapchat.com","twitch.tv","discord.com",
    nullptr
};

bool ClientHandler::IsBlocked(const std::string& url) {
    for (int i = 0; kBlockList[i]; i++)
        if (url.find(kBlockList[i]) != std::string::npos) return true;
    return false;
}

void ClientHandler::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    browser_ = browser;
    OnBrowserCreated(tab_id_, browser);
}
void ClientHandler::OnBeforeClose(CefRefPtr<CefBrowser>) {
    browser_ = nullptr;
}
void ClientHandler::OnTitleChange(CefRefPtr<CefBrowser> browser,
                                   const CefString& title) {
    std::wstring t = title.ToWString();
    std::wstring url = browser->GetMainFrame()->GetURL().ToWString();
    OnTitleChanged(tab_id_, t);
    if (!url.empty() && url != L"about:blank")
        AddHistory(t, url);
}
void ClientHandler::OnAddressChange(CefRefPtr<CefBrowser>,
                                     CefRefPtr<CefFrame> frame,
                                     const CefString& url) {
    if (!frame->IsMain()) return;
    OnUrlChanged(tab_id_, url.ToWString());
}

CefResourceRequestHandler::ReturnValue ClientHandler::OnBeforeResourceLoad(
    CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
    CefRefPtr<CefRequest> request, CefRefPtr<CefCallback>) {
    std::string url = request->GetURL().ToString();
    if (g_focus)
        for (int i = 0; kFocusBlock[i]; i++)
            if (url.find(kFocusBlock[i]) != std::string::npos) return RV_CANCEL;
    if ((g_blockAds || g_blockTrackers) && IsBlocked(url)) return RV_CANCEL;
    return RV_CONTINUE;
}

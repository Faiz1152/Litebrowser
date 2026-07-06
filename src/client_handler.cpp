#include "client_handler.h"

static const char* kBlockList[] = {
    // Display & programmatic ads
    "doubleclick.net","googlesyndication.com","googleadservices.com",
    "adnxs.com","advertising.com","adroll.com","criteo.com","criteo.net",
    "outbrain.com","taboola.com","pubmatic.com","rubiconproject.com",
    "casalemedia.com","openx.net","moatads.com","amazon-adsystem.com",
    "adsafeprotected.com","media.net","sharethrough.com","bidswitch.net",
    "adsrvr.org","adform.net","appnexus.com","smartadserver.com",
    "lijit.com","sovrn.com","33across.com","triplelift.com",
    "indexexchange.com","yieldmo.com","rhythmone.com","spotxchange.com",
    "spotx.tv","undertone.com","conversantmedia.com","turn.com",
    // Trackers
    "google-analytics.com","googletagmanager.com","hotjar.com",
    "mixpanel.com","segment.com","segment.io","amplitude.com",
    "fullstory.com","heap.io","chartbeat.com","newrelic.com",
    "connect.facebook.net","bat.bing.com","analytics.twitter.com",
    "scorecardresearch.com","quantserve.com","comscore.com",
    "krxd.net","bluekai.com","demdex.net","rlcdn.com",
    "adtechus.com","adblade.com","mathtag.com","exelator.com",
    // YouTube ads
    "googleads.g.doubleclick.net","static.doubleclick.net",
    "pagead2.googlesyndication.com","ad.youtube.com",
    "ads.youtube.com","imasdk.googleapis.com",
    "youtube.com/api/stats/ads","youtube.com/pagead",
    "youtube.com/ptracking","youtubei.googleapis.com/youtubei/v1/log",
    nullptr
};

static const char* kFocusBlock[] = {
    "facebook.com","twitter.com","x.com","instagram.com","reddit.com",
    "youtube.com","tiktok.com","snapchat.com","twitch.tv","discord.com",
    "netflix.com","primevideo.com","disneyplus.com","hulu.com",
    "9gag.com","pinterest.com","tumblr.com","linkedin.com",
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

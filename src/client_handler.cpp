#include <windows.h>
#include <objidl.h>

#undef GetFirstChild
#undef GetNextSibling

#include <gdiplus.h>
#include <sstream>

#include "client_handler.h"
#include "include/cef_urlrequest.h"

#pragma comment(lib, "gdiplus.lib")

static const char* kBlockList[] = {
    "doubleclick.net","googlesyndication.com","googleadservices.com",
    "adnxs.com","advertising.com","adroll.com","criteo.com","criteo.net",
    "outbrain.com","taboola.com","pubmatic.com","rubiconproject.com",
    "casalemedia.com","openx.net","moatads.com","amazon-adsystem.com",
    "adsafeprotected.com","media.net","sharethrough.com","bidswitch.net",
    "adsrvr.org","adform.net","appnexus.com","smartadserver.com",
    "lijit.com","sovrn.com","33across.com","triplelift.com",
    "indexexchange.com","yieldmo.com","rhythmone.com","spotxchange.com",
    "spotx.tv","undertone.com","conversantmedia.com","turn.com",
    "google-analytics.com","googletagmanager.com","hotjar.com",
    "mixpanel.com","segment.com","segment.io","amplitude.com",
    "fullstory.com","heap.io","chartbeat.com","newrelic.com",
    "connect.facebook.net","bat.bing.com","analytics.twitter.com",
    "scorecardresearch.com","quantserve.com","comscore.com",
    "krxd.net","bluekai.com","demdex.net","rlcdn.com",
    "adtechus.com","adblade.com","mathtag.com","exelator.com",
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

static HBITMAP DecodeFaviconBytes(const std::vector<uint8_t>& data) {
    if (data.empty()) return nullptr;
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, data.size());
    if (!hg) return nullptr;
    void* ptr = GlobalLock(hg);
    if (!ptr) { GlobalFree(hg); return nullptr; }
    memcpy(ptr, data.data(), data.size());
    GlobalUnlock(hg);
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(hg, TRUE, &stream) != S_OK) { GlobalFree(hg); return nullptr; }
    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromStream(stream);
    stream->Release();
    if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) { delete bmp; return nullptr; }
    const int SZ = 16;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = SZ;
    bi.bmiHeader.biHeight      = -SZ;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC hdc = GetDC(nullptr);
    HBITMAP hbmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, hdc);
    if (!hbmp) { delete bmp; return nullptr; }
    HDC memDC = CreateCompatibleDC(nullptr);
    HBITMAP old = (HBITMAP)SelectObject(memDC, hbmp);
    Gdiplus::Graphics g(memDC);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.DrawImage(bmp, 0, 0, SZ, SZ);
    SelectObject(memDC, old);
    DeleteDC(memDC);
    delete bmp;
    return hbmp;
}

#define WM_FAVICON_READY (WM_APP + 10)
extern HWND g_hwnd;
struct FaviconMsg { int tabId; HBITMAP bmp; };

void FaviconFetcher::OnRequestComplete(CefRefPtr<CefURLRequest> request) {
    HBITMAP bmp = nullptr;
    if (request->GetRequestStatus() == UR_SUCCESS && !data_.empty())
        bmp = DecodeFaviconBytes(data_);
    FaviconMsg* msg = new FaviconMsg{tab_id_, bmp};
    PostMessage(g_hwnd, WM_FAVICON_READY, 0, (LPARAM)msg);
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
    std::wstring t   = title.ToWString();
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
void ClientHandler::OnFaviconURLChange(CefRefPtr<CefBrowser> browser,
                                        const std::vector<CefString>& icon_urls) {
    if (icon_urls.empty()) return;

    CefRefPtr<CefRequest> req = CefRequest::Create();
    req->SetURL(icon_urls[0]);
    req->SetMethod("GET");

    CefRefPtr<FaviconFetcher> fetcher = new FaviconFetcher(tab_id_);

    CefURLRequest::Create(
        req,
        fetcher,
        browser->GetHost()->GetRequestContext()
    );
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

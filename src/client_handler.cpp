#include <windows.h>
#include <objidl.h>
#include <combaseapi.h>

#undef GetFirstChild
#undef GetNextSibling

#include <gdiplus.h>
#include <wincodec.h>
#include <vector>
#include <string>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <algorithm>
#include <iterator>
#include <chrono>
#include <ctime>

#include "client_handler.h"
#include "include/cef_urlrequest.h"
#include "include/cef_image.h"
#include "include/wrapper/cef_closure_task.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "windowscodecs.lib")

static const char* kBlockList[] = {
    "googlesyndication.com",
    "googleadservices.com","adnxs.com",
    "advertising.com","adroll.com",
    "criteo.com","criteo.net",
    "outbrain.com","taboola.com",
    "pubmatic.com","rubiconproject.com",
    "casalemedia.com","openx.net",
    "moatads.com","amazon-adsystem.com",
    "adsafeprotected.com","media.net",
    "sharethrough.com","bidswitch.net",
    "adsrvr.org","adform.net",
    "appnexus.com","smartadserver.com",
    "lijit.com","sovrn.com",
    "33across.com","triplelift.com",
    "indexexchange.com","yieldmo.com",
    "rhythmone.com","spotxchange.com",
    "spotx.tv","undertone.com",
    "conversantmedia.com","turn.com",
    "google-analytics.com",
    "googletagmanager.com",
    "hotjar.com","mixpanel.com",
    "segment.com","segment.io",
    "amplitude.com","fullstory.com",
    "heap.io","chartbeat.com",
    "newrelic.com",
    nullptr
};

static const char* kFocusBlock[] = {
    "facebook.com",
    "twitter.com",
    "x.com",
    "instagram.com",
    "reddit.com",
    "youtube.com",
    "tiktok.com",
    "snapchat.com",
    "twitch.tv",
    "discord.com",
    "netflix.com",
    "primevideo.com",
    nullptr
};

static const char* kYouTubeAdPatterns[] = {
    "youtube.com/api/stats/ads",
    "youtube.com/get_midroll",
    nullptr
};

static std::unordered_set<std::string> g_blockDomains;
static std::vector<std::string> g_blockPatterns;
static bool g_blocklistLoaded = false;

static std::wstring GetExeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p(path);
    size_t slash = p.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? L"." : p.substr(0, slash);
}

// DISABLED: Debug logging to reduce file I/O
// Uncomment to re-enable: set DEBUG_LOGGING to 1
#define DEBUG_LOGGING 0

static void DebugLog(const std::string& msg) {
#if DEBUG_LOGGING
    static std::wstring logPath = GetExeDir() + L"\\debug.log";
    std::ofstream f(logPath, std::ios::app);
    if (!f.is_open()) return;
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    struct tm tmv;
    localtime_s(&tmv, &t);
    strftime(buf, sizeof(buf), "%H:%M:%S", &tmv);
    f << "[" << buf << "] " << msg << "\n";
#endif
}

static bool WildcardMatch(const std::string& url, const std::string& pattern) {
    size_t pi = 0, ui = 0, star = std::string::npos, match = 0;
    while (ui < url.size()) {
        if (pi < pattern.size() && (pattern[pi] == url[ui])) {
            pi++; ui++;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            star = pi++; match = ui;
        } else if (star != std::string::npos) {
            pi = star + 1; ui = ++match;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == '*') pi++;
    return pi == pattern.size();
}

static const char* kNeverBlockDomains[] = {
    "googlevideo.com",
    "ytimg.com",
    "youtube.com",
    "youtu.be",
    "ggpht.com",
    "gstatic.com",
    "youtubei.googleapis.com",
    "youtube.googleapis.com",
    "accounts.google.com",
    "accounts.youtube.com",
    "googleusercontent.com",
    "l.google.com",
    "apis.google.com",
    "widevine.com",
    nullptr
};
static bool IsProtectedDomain(const std::string& s) {
    for (int i = 0; kNeverBlockDomains[i]; i++) {
        std::string p = kNeverBlockDomains[i];
        if (s.find(p) != std::string::npos) return true;
        if (s.size() >= 5 && p.find(s) != std::string::npos) return true;
    }
    return false;
}

void LoadBlocklist() {
    if (g_blocklistLoaded) return;
    g_blocklistLoaded = true;

    std::wstring path = GetExeDir() + L"\\blocklist.txt";
    std::ifstream file(path);
    if (!file.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        if (line[0] == '!' || line[0] == '[') continue;
        if (line.rfind("@@", 0) == 0) continue;
        if (line.find("##") != std::string::npos) continue;
        if (line.find("#@#") != std::string::npos) continue;

        std::string rule = line;

        if (rule.rfind("||", 0) == 0) {
            std::string body = rule.substr(2);
            size_t end = body.find_first_of("^/");
            std::string domain = (end == std::string::npos) ? body : body.substr(0, end);
            size_t dollar = domain.find('$');
            if (dollar != std::string::npos) domain = domain.substr(0, dollar);
            if (!domain.empty() && domain.size() >= 3 && domain.find('*') == std::string::npos
                && !IsProtectedDomain(domain)) {
                g_blockDomains.insert(domain);
                continue;
            }
        }

        if (rule.find('*') != std::string::npos || rule.find('/') == 0) {
            size_t dollar = rule.find('$');
            if (dollar != std::string::npos) rule = rule.substr(0, dollar);
            size_t literalChars = 0;
            for (char c : rule) if (c != '*') literalChars++;
            if (!rule.empty() && literalChars >= 3 && !IsProtectedDomain(rule)) g_blockPatterns.push_back(rule);
            continue;
        }

        bool looksLikeDomain = rule.find('.') != std::string::npos &&
                                rule.find(' ') == std::string::npos &&
                                rule.find('/') == std::string::npos;
        if (looksLikeDomain && rule.size() >= 3 && !IsProtectedDomain(rule)) {
            size_t dollar = rule.find('$');
            if (dollar != std::string::npos) rule = rule.substr(0, dollar);
            if (!rule.empty() && rule.size() >= 3) g_blockDomains.insert(rule);
        }
    }

    static const char* kSafeUrls[] = {
        "https://www.google.com/",
        "https://www.wikipedia.org/",
        "https://github.com/",
        "file:///start.html",
        "https://www.youtube.com/",
        "https://youtubei.googleapis.com/youtubei/v1/search",
        "https://www.gstatic.com/youtube/img/",
        "https://i.ytimg.com/vi/",
        nullptr
    };
    for (auto it = g_blockDomains.begin(); it != g_blockDomains.end(); ) {
        bool bad = false;
        for (int i = 0; kSafeUrls[i]; i++) {
            std::string u = kSafeUrls[i];
            if (!it->empty() && u.find(*it) != std::string::npos) { bad = true; break; }
        }
        it = bad ? g_blockDomains.erase(it) : std::next(it);
    }
    g_blockPatterns.erase(
        std::remove_if(g_blockPatterns.begin(), g_blockPatterns.end(),
            [&](const std::string& p) {
                for (int i = 0; kSafeUrls[i]; i++) {
                    if (WildcardMatch(kSafeUrls[i], p)) return true;
                }
                return false;
            }),
        g_blockPatterns.end()
    );

    DebugLog("Blocklist loaded: " + std::to_string(g_blockDomains.size()) +
             " domains, " + std::to_string(g_blockPatterns.size()) + " patterns.");
}

static std::string HostOfUrl(const std::string& url) {
    size_t start = url.find("://");
    start = (start == std::string::npos) ? 0 : start + 3;
    size_t end = url.find('/', start);
    return (end == std::string::npos) ? url.substr(start) : url.substr(start, end - start);
}

static bool DomainBlocked(const std::string& url) {
    std::string host = HostOfUrl(url);

    for (const auto& d : g_blockDomains) {
        if (host == d)
            return true;

        if (host.size() > d.size() &&
            host.compare(host.size() - d.size(), d.size(), d) == 0 &&
            host[host.size() - d.size() - 1] == '.') {
            return true;
        }
    }

    return false;
}
static bool PatternBlocked(const std::string& url) {
    for (const auto& p : g_blockPatterns) {
        if (p.size() < 4) continue;
        if (WildcardMatch(url, p)) return true;
    }
    return false;
}

bool ClientHandler::IsBlocked(const std::string& url)
{
    if (IsProtectedDomain(HostOfUrl(url)))
        return false;

    for (int i = 0; kBlockList[i]; i++)
    {
        if (url.find(kBlockList[i]) != std::string::npos)
        {
            DebugLog("Matched hardcoded rule: " + std::string(kBlockList[i]));
            return true;
        }
    }

    if (DomainBlocked(url)) return true;
    if (PatternBlocked(url)) return true;

    return false;
}

bool ClientHandler::IsYouTubeAd(const std::string& url)
{
    for (int i = 0; kYouTubeAdPatterns[i]; i++) {
        if (url.find(kYouTubeAdPatterns[i]) != std::string::npos)
            return true;
    }
    return false;
}

static HBITMAP CreateBitmap16FromHICON(HICON icon)
{
    ICONINFO ii{};
    if (!GetIconInfo(icon, &ii))
        return nullptr;

    HBITMAP bmp = ii.hbmColor;
    DeleteObject(ii.hbmMask);
    return bmp;
}

static HBITMAP DecodeFaviconBytes(const std::vector<uint8_t>& data)
{
    if (data.empty())
        return nullptr;

    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)
    );
    if (FAILED(hr))
        return nullptr;

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, data.size());
    if (!memory) {
        factory->Release();
        return nullptr;
    }

    void* buffer = GlobalLock(memory);
    memcpy(buffer, data.data(), data.size());
    GlobalUnlock(memory);

    IStream* stream = nullptr;
    CreateStreamOnHGlobal(memory, TRUE, &stream);

    IWICStream* wicStream = nullptr;
    factory->CreateStream(&wicStream);
    wicStream->InitializeFromIStream(stream);

    IWICBitmapDecoder* decoder = nullptr;
    hr = factory->CreateDecoderFromStream(
        wicStream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder
    );
    if (FAILED(hr)) {
        wicStream->Release();
        stream->Release();
        factory->Release();
        return nullptr;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    decoder->GetFrame(0, &frame);

    IWICFormatConverter* converter = nullptr;
    factory->CreateFormatConverter(&converter);
    converter->Initialize(
        frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
        nullptr, 0, WICBitmapPaletteTypeCustom
    );

    UINT width = 16, height = 16;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -((LONG)height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HDC hdc = GetDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(hdc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    ReleaseDC(nullptr, hdc);

    if (bitmap) {
        converter->CopyPixels(nullptr, width * 4, width * height * 4, (BYTE*)pixels);
    }

    converter->Release();
    frame->Release();
    decoder->Release();
    wicStream->Release();
    stream->Release();
    factory->Release();

    return bitmap;
}

#define WM_FAVICON_READY (WM_APP + 10)

extern HWND g_hwnd;

struct FaviconMsg
{
    int tabId;
    HBITMAP bmp;
};

void FaviconFetcher::OnRequestComplete(
    CefRefPtr<CefURLRequest> request)
{
    HBITMAP bmp = nullptr;

    if (request->GetRequestStatus() == UR_SUCCESS)
    {
        bmp = DecodeFaviconBytes(data_);
    }

    FaviconMsg* msg =
        new FaviconMsg{
            tab_id_,
            bmp
        };

    PostMessage(
        g_hwnd,
        WM_FAVICON_READY,
        0,
        (LPARAM)msg
    );
}

void ClientHandler::OnAfterCreated(
    CefRefPtr<CefBrowser> browser)
{
    browser_ = browser;
    OnBrowserCreated(tab_id_, browser);
}

void ClientHandler::OnBeforeClose(
    CefRefPtr<CefBrowser>)
{
    browser_ = nullptr;
}

void ClientHandler::OnTitleChange(
    CefRefPtr<CefBrowser> browser,
    const CefString& title)
{
    std::wstring t =
        title.ToWString();

    std::wstring url =
        browser->GetMainFrame()
        ->GetURL()
        .ToWString();

    OnTitleChanged(
        tab_id_,
        t
    );

    if (!url.empty() &&
        url != L"about:blank")
    {
        AddHistory(
            t,
            url
        );
    }
}

void ClientHandler::OnAddressChange(
    CefRefPtr<CefBrowser>,
    CefRefPtr<CefFrame> frame,
    const CefString& url)
{
    if (!frame->IsMain())
        return;

    OnUrlChanged(
        tab_id_,
        url.ToWString()
    );
}

void ClientHandler::OnFaviconURLChange(
    CefRefPtr<CefBrowser> browser,
    const std::vector<CefString>& icon_urls)
{
    if (icon_urls.empty())
        return;

    CefRefPtr<CefRequest> request =
        CefRequest::Create();

    request->SetURL(
        icon_urls[0]
    );

    request->SetMethod(
        "GET"
    );

    CefRefPtr<FaviconFetcher> fetcher =
        new FaviconFetcher(
            tab_id_
        );

    CefURLRequest::Create(
        request,
        fetcher,
        browser->GetHost()
        ->GetRequestContext()
    );
}

bool ClientHandler::OnBeforePopup(
    CefRefPtr<CefBrowser>,
    CefRefPtr<CefFrame>,
    int,
    const CefString& target_url,
    const CefString&,
    CefLifeSpanHandler::WindowOpenDisposition,
    bool,
    const CefPopupFeatures&,
    CefWindowInfo&,
    CefRefPtr<CefClient>&,
    CefBrowserSettings&,
    CefRefPtr<CefDictionaryValue>&,
    bool*)
{
    if (!g_blockPopups)
        return false;

    std::string url = target_url.ToString();
    return true;
}

void ClientHandler::OnLoadEnd(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    int httpStatusCode)
{
    if (!frame->IsMain())
        return;

    std::string url = frame->GetURL().ToString();

    // ============================================================
    // YOUTUBE-SPECIFIC HANDLING
    // Separate ad blocking from header restoration to avoid conflicts
    // ============================================================
    if (url.find("youtube.com") != std::string::npos) {
        // FIRST: Restore header visibility (innocuous, just clears inline styles)
        static const char* kYouTubeHeaderFix = R"JS(
(function(){
  function ensureHeaderVisible() {
    try {
      const masthead = document.querySelector('ytd-masthead');
      if (masthead) {
        masthead.style.display = '';
        masthead.style.visibility = 'visible';
        masthead.style.opacity = '1';
      }
    } catch(e) {}
  }
  
  // Run immediately
  ensureHeaderVisible();
  
  // Run again after a short delay in case YouTube re-renders
  setTimeout(ensureHeaderVisible, 500);
  setTimeout(ensureHeaderVisible, 1000);
})();
)JS";
        frame->ExecuteJavaScript(kYouTubeHeaderFix, frame->GetURL(), 0);
        
        // SECOND: Hide ads ONLY if blocking is enabled
        if (g_blockAds) {
            static const char* kYouTubeAdHideCSS = R"JS(
(function(){
  try {
    var style = document.createElement('style');
    style.textContent = [
      // Only hide SPECIFIC ad elements, not general containers
      'ins.adsbygoogle,',
      'iframe[src*="doubleclick"],',
      'iframe[src*="googlesyndication"],',
      '.ytp-ad-module,',
      '.video-ads,',
      '.ytp-ad-overlay-container,',
      '.ytp-ad-player-overlay,',
      'ytd-promoted-sparkles-web-renderer,',
      'ytd-display-ad-renderer,',
      'ytd-in-feed-ad-layout-renderer,'
    ].join('') + '{display:none !important;}';
    document.head.appendChild(style);
  } catch(e) {}
})();
)JS";
            frame->ExecuteJavaScript(kYouTubeAdHideCSS, frame->GetURL(), 0);
        }
        return;
    }

    // ============================================================
    // NON-YOUTUBE SITES: Standard ad blocking
    // ============================================================
    if (!g_blockAds)
        return;

    static const char* kGenericAdHideCSS = R"JS(
(function(){
  try {
    var style = document.createElement('style');
    style.textContent = [
      '[class*="ad-container"],[id*="ad-container"],',
      '[class*="advertisement"],[id*="advertisement"],',
      'ins.adsbygoogle,iframe[src*="doubleclick"],',
      'iframe[src*="googlesyndication"]'
    ].join('') + '{display:none!important;}';
    document.head.appendChild(style);
  } catch (e) {}
})();
)JS";

    frame->ExecuteJavaScript(kGenericAdHideCSS, frame->GetURL(), 0);
}

CefResourceRequestHandler::ReturnValue
ClientHandler::OnBeforeResourceLoad(
    CefRefPtr<CefBrowser>,
    CefRefPtr<CefFrame>,
    CefRefPtr<CefRequest> request,
    CefRefPtr<CefCallback>)
{
    std::string url = request->GetURL().ToString();
    std::string resourceType = std::to_string((int)request->GetResourceType());

    DebugLog("REQUEST [" + resourceType + "]: " + url);

    // ============================================================
    // CRITICAL: YouTube needs most of its requests to succeed.
    // Only block actual ad serving patterns, NOT initialization APIs.
    // ============================================================
    if (url.find("youtube.com") != std::string::npos ||
        url.find("youtubei.googleapis.com") != std::string::npos ||
        url.find("googlevideo.com") != std::string::npos)
    {
        // Only block known YouTube ad endpoints
        if (g_blockAds && IsYouTubeAd(url)) {
            DebugLog("BLOCKED (youtube ad pattern): " + url);
            return RV_CANCEL;
        }
        // ALLOW everything else on YouTube to ensure initialization works
        return RV_CONTINUE;
    }

    // Focus mode: block distracting sites entirely
    if (g_focus)
    {
        for (int i = 0; kFocusBlock[i]; i++)
        {
            if (url.find(kFocusBlock[i]) != std::string::npos)
            {
                DebugLog("BLOCKED (focus mode, matched '" + std::string(kFocusBlock[i]) + "'): " + url);
                return RV_CANCEL;
            }
        }
    }

    // Block other ads and trackers
    if ((g_blockAds || g_blockTrackers) && IsBlocked(url))
    {
        DebugLog("BLOCKED (generic/blocklist): " + url);
        return RV_CANCEL;
    }

    return RV_CONTINUE;
}

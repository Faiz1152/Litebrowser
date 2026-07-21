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

#include "client_handler.h"
#include "include/cef_urlrequest.h"
#include "include/cef_image.h"
#include "include/wrapper/cef_closure_task.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "windowscodecs.lib")

// ---------------------------------------------------------------------
// Original hardcoded lists (kept as-is; still checked alongside the big
// blocklist.txt so nothing that worked before stops working).
// ---------------------------------------------------------------------
static const char* kBlockList[] = {
    "doubleclick.net","googlesyndication.com",
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

// YouTube-specific ad server / path patterns. Kept separate from the
// general blocklist so it's easy to tune without touching blocklist.txt.
// These target ad DELIVERY, not video delivery, so videos keep playing.
static const char* kYouTubeAdPatterns[] = {
    "youtube.com/api/stats/ads",
    "youtube.com/pagead/",
    "youtube.com/ptracking",
    "youtube.com/api/stats/qoe",
    "googlevideo.com/videoplayback?",  // narrowed further below by param check
    "googleads.g.doubleclick.net",
    "static.doubleclick.net",
    "youtube.com/get_midroll",
    nullptr
};

// ---------------------------------------------------------------------
// V3.1: big blocklist.txt (EasyList + EasyPrivacy + uBlock filters +
// Peter Lowe's list), fetched and merged at BUILD time by GitHub Actions,
// shipped next to the exe, loaded once here at startup.
//
// Parsing note: this is a lightweight loader, not a full Adblock Plus
// filter engine. It extracts what it can reliably act on:
//   - "||domain.tld^" style rules  -> exact/subdomain domain block
//   - plain hostname lines          -> exact/subdomain domain block
//   - lines with '*' wildcards      -> substring/wildcard pattern block
// Cosmetic rules (##, #@#), exception rules (@@), and option-heavy rules
// ($script,domain=...) are skipped for matching purposes because they need
// a real filter engine to apply correctly — but nothing is deleted from
// blocklist.txt itself, so the full list still ships untrimmed.
// ---------------------------------------------------------------------
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

static bool WildcardMatch(const std::string& url, const std::string& pattern) {
    // Simple '*' wildcard matcher (Adblock Plus style, minus '^' separators).
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

// Domains that must never be blocked wholesale, even if a fetched filter
// list contains a rule targeting them with qualifiers (like $domain=) that
// our simplified parser can't honor. These serve real content (video, etc),
// not just ads — YouTube's ad blocking is handled separately and precisely
// by IsYouTubeAd() in OnBeforeResourceLoad, so the generic list doesn't
// need to (and must not) touch these.
static const char* kNeverBlockDomains[] = {
    "googlevideo.com",
    "ytimg.com",
    "youtube.com",
    "youtu.be",
    "ggpht.com",
    nullptr
};
static bool IsProtectedDomain(const std::string& s) {
    for (int i = 0; kNeverBlockDomains[i]; i++)
        if (s.find(kNeverBlockDomains[i]) != std::string::npos)
            return true;
    return false;
}

void LoadBlocklist() {
    if (g_blocklistLoaded) return;
    g_blocklistLoaded = true;

    std::wstring path = GetExeDir() + L"\\blocklist.txt";
    std::ifstream file(path);
    if (!file.is_open()) {
        // Not fatal — browser still works with the hardcoded lists above.
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        if (line[0] == '!' || line[0] == '[') continue;           // comment / metadata
        if (line.rfind("@@", 0) == 0) continue;                    // exception rule, skip
        if (line.find("##") != std::string::npos) continue;        // cosmetic rule, skip
        if (line.find("#@#") != std::string::npos) continue;       // cosmetic exception, skip

        std::string rule = line;

        if (rule.rfind("||", 0) == 0) {
            // ||domain.tld^ or ||domain.tld/path
            std::string body = rule.substr(2);
            size_t end = body.find_first_of("^/");
            std::string domain = (end == std::string::npos) ? body : body.substr(0, end);
            // Strip any trailing options after '$' if they leaked through
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

        // Plain hostname line (no special chars) -> treat as domain rule
        bool looksLikeDomain = rule.find('.') != std::string::npos &&
                                rule.find(' ') == std::string::npos &&
                                rule.find('/') == std::string::npos;
        if (looksLikeDomain && rule.size() >= 3 && !IsProtectedDomain(rule)) {
            size_t dollar = rule.find('$');
            if (dollar != std::string::npos) rule = rule.substr(0, dollar);
            if (!rule.empty() && rule.size() >= 3) g_blockDomains.insert(rule);
        }
    }

    // Self-test: make sure nothing in the loaded list blocks known-safe
    // URLs (including the local start page). Strips any rule that does,
    // so a bad rule from an upstream list can never blank out the browser.
    static const char* kSafeUrls[] = {
        "https://www.google.com/",
        "https://www.wikipedia.org/",
        "https://github.com/",
        "file:///start.html",
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
}

static bool DomainBlocked(const std::string& url) {
    for (const auto& d : g_blockDomains) {
        if (d.empty()) continue; // defensive: never let an empty rule match everything
        if (url.find(d) != std::string::npos) return true;
    }
    return false;
}
static bool PatternBlocked(const std::string& url) {
    for (const auto& p : g_blockPatterns) {
        if (p.size() < 4) continue; // defensive: too short to be a real, safe pattern
        if (WildcardMatch(url, p)) return true;
    }
    return false;
}

bool ClientHandler::IsBlocked(const std::string& url)
{
    for (int i = 0; kBlockList[i]; i++)
        if (url.find(kBlockList[i]) != std::string::npos)
            return true;

    if (DomainBlocked(url)) return true;
    if (PatternBlocked(url)) return true;

    return false;
}

bool ClientHandler::IsYouTubeAd(const std::string& url)
{
    // Only special-case within youtube/googlevideo domains, never block
    // general googlevideo.com playback requests wholesale (that would
    // break video, not just ads).
    for (int i = 0; kYouTubeAdPatterns[i]; i++) {
        if (url.find(kYouTubeAdPatterns[i]) != std::string::npos) {
            if (std::string(kYouTubeAdPatterns[i]).rfind("googlevideo.com", 0) == 0 ||
                url.find("googlevideo.com/videoplayback?") != std::string::npos) {
                // Only treat as an ad request if it explicitly carries an
                // ad-marker param; otherwise let it through (real video).
                if (url.find("&ctier=") != std::string::npos ||
                    url.find("&oad=") != std::string::npos ||
                    url.find("adformat") != std::string::npos) {
                    return true;
                }
                continue;
            }
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------
// Favicon handling (fixed — previously this file had two DecodeFaviconBytes
// definitions spliced into one broken function with mismatched braces).
// This is the single, complete WIC-based implementation.
// ---------------------------------------------------------------------
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

// ---------------------------------------------------------------------
// V3.1: popup blocking
// ---------------------------------------------------------------------
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
        return false; // false = allow

    std::string url = target_url.ToString();
    // Always block ad-network popups outright, and block everything else
    // too while g_blockPopups is on (matches the "block or block ad-only"
    // plan — set to ad-only by removing the "return true" fallthrough below
    // if you want popups from non-ad sites through).
    return true; // true = block
}

// ---------------------------------------------------------------------
// V3.1: CSS + JS injection to hide/remove ads that slip past network
// blocking (in-page ad containers, YouTube overlay ads, etc.)
// ---------------------------------------------------------------------
void ClientHandler::OnLoadEnd(
    CefRefPtr<CefBrowser>,
    CefRefPtr<CefFrame> frame,
    int)
{
    if (!frame->IsMain())
        return;

    if (!g_blockAds)
        return;

    static const char* kInjectJS = R"JS(
(function(){
  try {
    var style = document.createElement('style');
    style.textContent = [
      '[class*="ad-container"],[id*="ad-container"],',
      '[class*="advertisement"],[id*="advertisement"],',
      'ins.adsbygoogle,iframe[src*="doubleclick"],',
      'iframe[src*="googlesyndication"],',
      '.ytp-ad-module,.video-ads,.ytp-ad-overlay-container,',
      '.ytp-ad-player-overlay,ytd-promoted-sparkles-web-renderer,',
      'ytd-display-ad-renderer,ytd-in-feed-ad-layout-renderer,',
      '#player-ads,#masthead-ad'
    ].join('') + '{display:none!important;visibility:hidden!important;}';
    document.documentElement.appendChild(style);

    function skipYouTubeAds(){
      var skip = document.querySelector('.ytp-ad-skip-button, .ytp-ad-skip-button-modern');
      if (skip) skip.click();
      var video = document.querySelector('video');
      var adShowing = document.querySelector('.ad-showing');
      if (adShowing && video && video.duration) {
        video.currentTime = video.duration;
      }
    }

    var mo = new MutationObserver(function(){ skipYouTubeAds(); });
    mo.observe(document.documentElement, {childList:true, subtree:true});
    setInterval(skipYouTubeAds, 500);
  } catch (e) {}
})();
)JS";

    frame->ExecuteJavaScript(kInjectJS, frame->GetURL(), 0);
}

CefResourceRequestHandler::ReturnValue
ClientHandler::OnBeforeResourceLoad(
    CefRefPtr<CefBrowser>,
    CefRefPtr<CefFrame>,
    CefRefPtr<CefRequest> request,
    CefRefPtr<CefCallback>)
{
    std::string url =
        request->GetURL()
        .ToString();

    if (g_focus)
    {
        for (int i = 0;
             kFocusBlock[i];
             i++)
        {
            if (url.find(kFocusBlock[i])
                != std::string::npos)
            {
                return RV_CANCEL;
            }
        }
    }

    if (url.find("youtube.com") != std::string::npos ||
        url.find("googlevideo.com") != std::string::npos)
    {
        if (g_blockAds && IsYouTubeAd(url))
            return RV_CANCEL;
    }

    if ((g_blockAds ||
         g_blockTrackers) &&
        IsBlocked(url))
    {
        return RV_CANCEL;
    }

    return RV_CONTINUE;
}

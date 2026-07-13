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

#include "client_handler.h"
#include "include/cef_urlrequest.h"
#include "include/cef_image.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "windowscodecs.lib")

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

bool ClientHandler::IsBlocked(const std::string& url)
{
    for (int i = 0; kBlockList[i]; i++)
        if (url.find(kBlockList[i]) != std::string::npos)
            return true;

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

    CefRefPtr<CefImage> image = CefImage::CreateImage();

    if (!image)
        return nullptr;

    if (!image->AddBitmap(
        16,
        16,
        CEF_COLOR_TYPE_BGRA_8888,
        CEF_ALPHA_TYPE_PREMULTIPLIED,
        data.data(),
        data.size()))
    {
        return nullptr;
    }

    void* pixels = nullptr;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 16;
    info.bmiHeader.biHeight = -16;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC(nullptr);

    HBITMAP bitmap = CreateDIBSection(
        hdc,
        &info,
        DIB_RGB_COLORS,
        &pixels,
        nullptr,
        0
    );

    ReleaseDC(nullptr, hdc);

    if (!bitmap)
        return nullptr;

    CefRefPtr<CefBinaryValue> value =
        image->GetAsBitmap(
            16,
            16,
            CEF_COLOR_TYPE_BGRA_8888,
            CEF_ALPHA_TYPE_PREMULTIPLIED
        );

    if (!value)
    {
        DeleteObject(bitmap);
        return nullptr;
    }

    memcpy(
        pixels,
        value->GetRawData(),
        16 * 16 * 4
    );

    return bitmap;
}
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

    HGLOBAL memory = GlobalAlloc(
        GMEM_MOVEABLE,
        data.size()
    );

    if (!memory)
    {
        factory->Release();
        return nullptr;
    }

    void* buffer = GlobalLock(memory);

    memcpy(
        buffer,
        data.data(),
        data.size()
    );

    GlobalUnlock(memory);

    IStream* stream = nullptr;

    CreateStreamOnHGlobal(
        memory,
        TRUE,
        &stream
    );

    IWICStream* wicStream = nullptr;

    factory->CreateStream(
        &wicStream
    );

    wicStream->InitializeFromIStream(
        stream
    );

    IWICBitmapDecoder* decoder = nullptr;

    hr = factory->CreateDecoderFromStream(
        wicStream,
        nullptr,
        WICDecodeMetadataCacheOnLoad,
        IID_PPV_ARGS(&decoder)
    );

    if (FAILED(hr))
    {
        wicStream->Release();
        stream->Release();
        factory->Release();
        return nullptr;
    }

    IWICBitmapFrameDecode* frame = nullptr;

    decoder->GetFrame(
        0,
        &frame
    );

    IWICFormatConverter* converter = nullptr;

    factory->CreateFormatConverter(
        &converter
    );

    converter->Initialize(
        frame,
        GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0,
        WICBitmapPaletteTypeCustom
    );

    UINT width = 16;
    UINT height = 16;

    BITMAPINFO info{};
    info.bmiHeader.biSize =
        sizeof(BITMAPINFOHEADER);

    info.bmiHeader.biWidth =
        width;

    info.bmiHeader.biHeight =
        -((LONG)height);

    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression =
        BI_RGB;

    void* pixels = nullptr;

    HDC hdc = GetDC(nullptr);

    HBITMAP bitmap =
        CreateDIBSection(
            hdc,
            &info,
            DIB_RGB_COLORS,
            &pixels,
            nullptr,
            0
        );

    ReleaseDC(nullptr, hdc);

    if (bitmap)
    {
        converter->CopyPixels(
            nullptr,
            width * 4,
            width * height * 4,
            (BYTE*)pixels
        );
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


    if ((g_blockAds ||
         g_blockTrackers) &&
        IsBlocked(url))
    {
        return RV_CANCEL;
    }


    return RV_CONTINUE;
}

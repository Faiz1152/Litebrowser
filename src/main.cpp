#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cctype>

#ifdef GetNextSibling
#undef GetNextSibling
#endif
#ifdef GetFirstChild
#undef GetFirstChild
#endif
#ifdef GetParent
#undef GetParent
#endif

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/base/cef_bind.h"
#include "app.h"
#include "client_handler.h"
#include "resource.h"

#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "shell32.lib")

#define WM_FAVICON_READY (WM_APP + 10)
struct FaviconMsg { int tabId; HBITMAP bmp; };

static const int kTabH      = 38;
static const int kToolH     = 40;
static const int kSideW     = 220;
static const int kTabGap    = 4;
static const int kTabRadius = 10;
static const int kBtnRadius = 8;
static const wchar_t kWndClass[] = L"LiteBrowserMain";
static const int ID_FOCUS_TIMER = 200;
static const int ID_SMART_TIMER = 201;
static const int ID_ANIM_TIMER  = 202;

struct Theme {
    COLORREF bg,chrome,text,textDim,border,tabOn,tabOff,tabHover,sidebar,accent,btnHover,shadow;
};
static const Theme kLight{
    RGB(255,255,255),RGB(242,242,244),RGB(20,20,20),RGB(120,120,120),
    RGB(224,224,226),RGB(255,255,255),RGB(228,228,230),RGB(238,238,240),
    RGB(248,248,248),RGB(0,102,204),RGB(220,220,224),RGB(0,0,0)
};
static const Theme kDark{
    RGB(24,24,26),RGB(34,34,37),RGB(225,225,228),RGB(140,140,145),
    RGB(52,52,56),RGB(48,48,52),RGB(30,30,32),RGB(40,40,44),
    RGB(28,28,30),RGB(56,150,255),RGB(52,52,58),RGB(0,0,0)
};
static bool  g_dark = false;
static Theme g_T    = kLight;

bool g_focus         = false;
bool g_blockAds      = true;
bool g_blockTrackers = true;
bool g_blockPopups   = true;

struct Tab {
    int id;
    std::wstring title, url;
    CefRefPtr<CefBrowser>    browser;
    CefRefPtr<ClientHandler> handler;
    DWORD lastUsed  = 0;
    int   useCount  = 0;
    float curW      = 0.f;
    HBITMAP favicon = nullptr;
};
static std::vector<Tab> g_tabs;
static int g_activeIdx = -1;
static int g_tabCtr    = 0;

static bool g_sideOpen = false;
static bool g_showBm   = true;
struct Entry { std::wstring title, url; };
static std::vector<Entry> g_bookmarks, g_history;

static int g_focusSecs = 0;

HWND  g_hwnd            = nullptr;
static HWND  g_addr     = nullptr;
static HWND  g_sbList   = nullptr;
static HFONT g_font     = nullptr;
static HFONT g_fontSm   = nullptr;
static HFONT g_fontIcon = nullptr;

static int  g_hoverTab      = -1;
static bool g_hoverTabClose = false;
static bool g_hoverPlus     = false;
static int  g_hoverToolBtn  = -1;
static int  g_hoverRightBtn = -1;
static bool g_tracking      = false;

static bool  g_animating = false;
static DWORD g_animStart = 0;
static const DWORD kAnimMs = 140;

#define REDRAW_CHROME() do { RECT _ir={0,0,20000,kTabH+kToolH}; InvalidateRect(g_hwnd,&_ir,FALSE); } while(0)

static std::wstring GetProfileDir() {
    wchar_t* p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) {
        std::wstring dir = std::wstring(p) + L"\\LiteBrowser";
        CoTaskMemFree(p);
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir;
    }
    return L".";
}

static void SyncThemeToStartPage() {
    if (g_activeIdx < 0 || g_activeIdx >= (int)g_tabs.size()) return;
    auto& t = g_tabs[g_activeIdx];
    if (!t.browser) return;
    if (t.url.find(L"start.html") == std::wstring::npos) return;
    std::string js = std::string("window.__lbTheme='")
        + (g_dark ? "dark" : "light")
        + "'; if(window.applyLBTheme) window.applyLBTheme(window.__lbTheme);";
    t.browser->GetMainFrame()->ExecuteJavaScript(js, "", 0);
}

static std::string W2A(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8,0,w.c_str(),(int)w.size(),nullptr,0,nullptr,nullptr);
    std::string r(n,0);
    WideCharToMultiByte(CP_UTF8,0,w.c_str(),(int)w.size(),&r[0],n,nullptr,nullptr);
    return r;
}
static std::wstring NormalizeUrl(const std::wstring& raw) {
    std::wstring s = raw;
    while (!s.empty()&&iswspace(s.front())) s.erase(s.begin());
    while (!s.empty()&&iswspace(s.back()))  s.pop_back();
    if (s.empty()) return L"https://www.google.com";
    if (s.find(L"://")!=std::wstring::npos) return s;
    if (s.find(L' ')==std::wstring::npos&&s.find(L'.')!=std::wstring::npos)
        return L"https://"+s;
    std::wstring q;
    for (wchar_t c:s) {
        if (iswalnum(c)) q+=c;
        else if (c==L' ') q+=L'+';
        else { wchar_t b[8]; swprintf(b,8,L"%%%02X",(unsigned)c); q+=b; }
    }
    return L"https://www.google.com/search?q="+q;
}
static bool IsHttps(const std::wstring& url) { return url.rfind(L"https://",0)==0; }
static std::wstring HostOf(const std::wstring& url) {
    size_t p=url.find(L"://");
    std::wstring rest=(p==std::wstring::npos)?url:url.substr(p+3);
    size_t slash=rest.find(L'/');
    std::wstring host=(slash==std::wstring::npos)?rest:rest.substr(0,slash);
    if (host.rfind(L"www.",0)==0) host=host.substr(4);
    return host;
}
static wchar_t FaviconLetter(const std::wstring& url) {
    std::wstring h=HostOf(url);
    for (wchar_t c:h) if(iswalnum(c)) return (wchar_t)towupper(c);
    return L'?';
}
static COLORREF FaviconColor(const std::wstring& url) {
    std::wstring h=HostOf(url); unsigned hash=0;
    for (wchar_t c:h) hash=hash*131+(unsigned)c;
    static const COLORREF p[]={
        RGB(66,133,244),RGB(219,68,55),RGB(244,180,0),RGB(15,157,88),
        RGB(171,71,188),RGB(0,172,193),RGB(255,112,67),RGB(92,107,192)};
    return p[hash%8];
}
static bool IsStartPage(const std::wstring& url) {
    return url.find(L"start.html")!=std::wstring::npos;
}

static RECT CR()   { RECT r; GetClientRect(g_hwnd,&r); return r; }
static int  SW()   { return g_sideOpen?kSideW:0; }
static RECT TabBarRc()  { RECT c=CR(); return {SW(),0,c.right,kTabH}; }
static RECT ToolbarRc() { RECT c=CR(); return {SW(),kTabH,c.right,kTabH+kToolH}; }
static RECT BrowserRc() { RECT c=CR(); return {SW(),kTabH+kToolH,c.right,c.bottom}; }

static float TargetTabWidth() {
    RECT tb=TabBarRc(); int n=(int)g_tabs.size(); if(!n) return 0;
    float tw=(float)(tb.right-tb.left-40)/n-kTabGap;
    return std::min(210.f,std::max(90.f,tw));
}
static RECT TabRc(int i) {
    RECT tb=TabBarRc(); float w=g_tabs[i].curW,x=(float)tb.left+6;
    for (int k=0;k<i;k++) x+=g_tabs[k].curW+kTabGap;
    return {(int)x,3,(int)(x+w),kTabH-2};
}
static int TabHit(int x,int y) {
    if (y<0||y>=kTabH) return -1;
    for (int i=0;i<(int)g_tabs.size();i++) {
        RECT r=TabRc(i); if(x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom) return i;
    }
    return -1;
}
static bool CloseHit(int i,int x,int y) {
    RECT r=TabRc(i); int cx=r.right-16,cy=(r.top+r.bottom)/2;
    return abs(x-cx)<9&&abs(y-cy)<9;
}
static RECT PlusRc() {
    RECT tb=TabBarRc(); float x=(float)tb.left+6;
    for (auto&t:g_tabs) x+=t.curW+kTabGap;
    return {(int)x+2,7,(int)x+2+24,kTabH-7};
}

static void StartTabAnim() {
    g_animating=true; g_animStart=GetTickCount();
    SetTimer(g_hwnd,ID_ANIM_TIMER,15,nullptr);
}
static void StepTabAnim() {
    float target=TargetTabWidth(); bool moving=false;
    for (auto&tb:g_tabs) {
        if(tb.curW<=0.f) tb.curW=target;
        float d=target-tb.curW;
        if(fabsf(d)>0.5f){tb.curW+=d*0.35f;moving=true;}else tb.curW=target;
    }
    if(!moving){for(auto&tb:g_tabs)tb.curW=target;g_animating=false;KillTimer(g_hwnd,ID_ANIM_TIMER);}
    REDRAW_CHROME();
}

static void FillRc(HDC h,RECT r,COLORREF c){HBRUSH b=CreateSolidBrush(c);FillRect(h,&r,b);DeleteObject(b);}
static void RoundFillRc(HDC h,RECT r,COLORREF c,int rad,COLORREF border=CLR_INVALID){
    HBRUSH b=CreateSolidBrush(c);
    HPEN p=border==CLR_INVALID?(HPEN)GetStockObject(NULL_PEN):CreatePen(PS_SOLID,1,border);
    HBRUSH ob=(HBRUSH)SelectObject(h,b); HPEN op=(HPEN)SelectObject(h,p);
    RoundRect(h,r.left,r.top,r.right,r.bottom,rad,rad);
    SelectObject(h,ob);SelectObject(h,op);DeleteObject(b);
    if(border!=CLR_INVALID)DeleteObject(p);
}
static void Txt(HDC h,const std::wstring&s,RECT r,COLORREF c,HFONT f,UINT fl=DT_SINGLELINE|DT_VCENTER|DT_CENTER|DT_END_ELLIPSIS){
    SetTextColor(h,c);SetBkMode(h,TRANSPARENT);
    HFONT o=(HFONT)SelectObject(h,f);DrawTextW(h,s.c_str(),-1,&r,fl);SelectObject(h,o);
}
static void HLine(HDC h,int x1,int x2,int y,COLORREF c){HPEN p=CreatePen(PS_SOLID,1,c),o=(HPEN)SelectObject(h,p);MoveToEx(h,x1,y,nullptr);LineTo(h,x2,y);SelectObject(h,o);DeleteObject(p);}
static void VLine(HDC h,int x,int y1,int y2,COLORREF c){HPEN p=CreatePen(PS_SOLID,1,c),o=(HPEN)SelectObject(h,p);MoveToEx(h,x,y1,nullptr);LineTo(h,x,y2);SelectObject(h,o);DeleteObject(p);}

static void DrawFavicon(HDC hdc,HBITMAP bmp,int x,int y){
    if(!bmp) return;
    HDC mdc=CreateCompatibleDC(hdc);
    HBITMAP old=(HBITMAP)SelectObject(mdc,bmp);
    BitBlt(hdc,x,y,16,16,mdc,0,0,SRCCOPY);
    SelectObject(mdc,old);DeleteDC(mdc);
}

static void PaintAll(HDC hdc){
    RECT cr=CR();
    FillRc(hdc,cr,g_T.bg);
    if(g_sideOpen){
        RECT sr={0,0,kSideW,cr.bottom};
        FillRc(hdc,sr,g_T.sidebar);
        VLine(hdc,kSideW-1,0,cr.bottom,g_T.border);
        int bw=(kSideW-4)/2;
        RECT bmr={2,kTabH+4,2+bw,kTabH+kToolH-4};
        RECT hir={2+bw,kTabH+4,kSideW-2,kTabH+kToolH-4};
        RoundFillRc(hdc,bmr,g_showBm?g_T.tabOn:g_T.tabOff,8);
        RoundFillRc(hdc,hir,!g_showBm?g_T.tabOn:g_T.tabOff,8);
        Txt(hdc,L"Bookmarks",bmr,g_T.text,g_fontSm);
        Txt(hdc,L"History",hir,g_T.text,g_fontSm);
        if(g_showBm){
            RECT ar={2,cr.bottom-30,kSideW-2,cr.bottom-4};
            RoundFillRc(hdc,ar,g_T.accent,8);
            Txt(hdc,L"+ Bookmark this page",ar,RGB(255,255,255),g_fontSm);
        }
    }
    RECT tbr=TabBarRc();
    FillRc(hdc,tbr,g_T.chrome);
    for(int i=0;i<(int)g_tabs.size();i++){
        RECT tr=TabRc(i); bool act=(i==g_activeIdx),hov=(i==g_hoverTab);
        COLORREF fill=act?g_T.tabOn:(hov?g_T.tabHover:g_T.chrome);
        RoundFillRc(hdc,tr,fill,kTabRadius);
        if(act) HLine(hdc,tr.left+6,tr.right-6,tr.bottom-1,g_T.accent);
        int fcy=(tr.top+tr.bottom)/2;
        if(g_tabs[i].favicon){
            DrawFavicon(hdc,g_tabs[i].favicon,tr.left+8,fcy-8);
        }else{
            int fcx=tr.left+16,fr=8;
            HBRUSH fb=CreateSolidBrush(FaviconColor(g_tabs[i].url));
            HBRUSH ofb=(HBRUSH)SelectObject(hdc,fb);
            HPEN onp=(HPEN)SelectObject(hdc,GetStockObject(NULL_PEN));
            Ellipse(hdc,fcx-fr,fcy-fr,fcx+fr,fcy+fr);
            SelectObject(hdc,ofb);SelectObject(hdc,onp);DeleteObject(fb);
            wchar_t fl[2]={FaviconLetter(g_tabs[i].url),0};
            RECT flr={fcx-fr,fcy-fr,fcx+fr,fcy+fr};
            Txt(hdc,fl,flr,RGB(255,255,255),g_fontSm);
        }
        RECT txr={tr.left+30,tr.top,tr.right-24,tr.bottom};
        Txt(hdc,g_tabs[i].title.empty()?L"New Tab":g_tabs[i].title,
            txr,act?g_T.text:g_T.textDim,g_fontSm,
            DT_SINGLELINE|DT_VCENTER|DT_LEFT|DT_END_ELLIPSIS);
        bool hovClose=hov&&g_hoverTabClose;
        RECT xr={tr.right-25,fcy-9,tr.right-7,fcy+9};
        if(hovClose) RoundFillRc(hdc,xr,g_T.tabHover,6);
        Txt(hdc,L"\u2715",xr,g_T.textDim,g_fontSm);
    }
    RECT plus=PlusRc();
    RoundFillRc(hdc,plus,g_hoverPlus?g_T.tabHover:g_T.chrome,12,g_T.border);
    Txt(hdc,L"+",plus,g_T.text,g_fontSm);

    RECT tr=ToolbarRc();
    FillRc(hdc,tr,g_T.chrome);
    HLine(hdc,tr.left,tr.right,tr.bottom-1,g_T.border);
    int bx=SW()+10,by=kTabH+6,bw=30,bh=kToolH-12;
    auto Btn=[&](int idx,int x,const wchar_t*lbl,COLORREF activeBg){
        RECT r={x,by,x+bw,by+bh};
        COLORREF fill=(g_hoverToolBtn==idx)?g_T.btnHover:activeBg;
        RoundFillRc(hdc,r,fill,kBtnRadius);
        Txt(hdc,lbl,r,g_T.text,g_fontIcon);
    };
    Btn(0,bx,    L"\u2190",g_T.chrome);
    Btn(1,bx+34, L"\u2192",g_T.chrome);
    Btn(2,bx+68, L"\u21BA",g_T.chrome);
    int addrX=bx+68+34+10;
    int rx=cr.right-3*34-8;
    RECT ar={addrX,kTabH+5,rx-8,kTabH+kToolH-5};
    RoundFillRc(hdc,ar,g_dark?RGB(44,44,48):RGB(255,255,255),18,g_T.border);
    bool https=g_activeIdx>=0&&IsHttps(g_tabs[g_activeIdx].url);
    RECT lockr={ar.left+6,ar.top,ar.left+26,ar.bottom};
    Txt(hdc,https?L"\U0001F512":L"\u26A0",lockr,https?RGB(60,160,90):RGB(200,140,20),g_fontSm);
    auto RBtn=[&](int idx,int x,const wchar_t*lbl,COLORREF activeBg){
        RECT r={x,by,x+bw,by+bh};
        COLORREF fill=(g_hoverRightBtn==idx)?g_T.btnHover:activeBg;
        RoundFillRc(hdc,r,fill,kBtnRadius);
        Txt(hdc,lbl,r,g_T.text,g_fontIcon);
    };
    RBtn(0,rx,    g_focus?L"\u25C9":L"\u25CB",g_focus?RGB(210,70,70):g_T.chrome);
    RBtn(1,rx+34, g_dark?L"\u2600":L"\u263E", g_T.chrome);
    RBtn(2,rx+68, g_sideOpen?L"\u25C0":L"\u25B6",g_T.chrome);
    if(g_focus&&g_focusSecs>0){
        wchar_t buf[32]; swprintf(buf,32,L"%d:%02d",g_focusSecs/60,g_focusSecs%60);
        RECT fr={rx-58,by,rx-8,by+bh};
        Txt(hdc,buf,fr,RGB(220,80,80),g_fontSm,DT_SINGLELINE|DT_VCENTER|DT_RIGHT);
    }
}

static void PositionAddressBar(){
    RECT cr=CR(); int bx=SW()+10;
    int addrX=bx+68+34+10+28,rx=cr.right-3*34-8,addrW=(rx-8)-addrX-8;
    if(g_addr){
        MoveWindow(g_addr,addrX,kTabH+7,std::max(addrW,10),kToolH-14,TRUE);
        HRGN rgn=CreateRoundRectRgn(0,0,std::max(addrW,10)+1,kToolH-14+1,10,10);
        SetWindowRgn(g_addr,rgn,TRUE);
    }
}
static void Layout(){
    if(!g_hwnd) return;
    RECT cr=CR();
    PositionAddressBar();
    if(g_sbList){
        if(g_sideOpen){MoveWindow(g_sbList,2,kTabH+kToolH,kSideW-4,cr.bottom-kTabH-kToolH-36,TRUE);ShowWindow(g_sbList,SW_SHOW);}
        else ShowWindow(g_sbList,SW_HIDE);
    }
    if(g_activeIdx>=0&&g_activeIdx<(int)g_tabs.size()){
        auto&t=g_tabs[g_activeIdx];
        if(t.browser){
            HWND bh=t.browser->GetHost()->GetWindowHandle();
            if(bh&&IsWindow(bh)){
                RECT br=BrowserRc();
                SetWindowPos(bh,HWND_TOP,br.left,br.top,br.right-br.left,br.bottom-br.top,SWP_NOACTIVATE);
            }
        }
    }
    StartTabAnim();
    REDRAW_CHROME();
}
static void RefreshList(){
    if(!g_sbList) return;
    SendMessage(g_sbList,LB_RESETCONTENT,0,0);
    auto&lst=g_showBm?g_bookmarks:g_history;
    for(auto&e:lst) SendMessage(g_sbList,LB_ADDSTRING,0,(LPARAM)(e.title.empty()?e.url:e.title).c_str());
}

void OnFaviconReady(int tabId,HBITMAP bmp){
    for(auto&t:g_tabs) if(t.id==tabId){
        if(t.favicon){DeleteObject(t.favicon);t.favicon=nullptr;}
        t.favicon=bmp;
        REDRAW_CHROME();
        break;
    }
}
void OnBrowserCreated(int tabId,CefRefPtr<CefBrowser> browser){
    for(auto&t:g_tabs) if(t.id==tabId){
        t.browser=browser;
        if(g_activeIdx>=0&&g_tabs[g_activeIdx].id==tabId){
            RECT br=BrowserRc();HWND bh=browser->GetHost()->GetWindowHandle();
            if(bh){MoveWindow(bh,br.left,br.top,br.right-br.left,br.bottom-br.top,TRUE);ShowWindow(bh,SW_SHOW);}
        }
        break;
    }
}
void OnTitleChanged(int tabId,const std::wstring&title){
    for(auto&t:g_tabs) if(t.id==tabId){t.title=title;break;}
    REDRAW_CHROME();
}
void OnUrlChanged(int tabId,const std::wstring&url){
    for(auto&t:g_tabs) if(t.id==tabId){
        t.url=url;
        if(t.favicon){DeleteObject(t.favicon);t.favicon=nullptr;}
        if(g_activeIdx>=0&&g_tabs[g_activeIdx].id==tabId){
            if(!IsStartPage(url)) SetWindowTextW(g_addr,url.c_str());
            else SetWindowTextW(g_addr,L"");
        }
        break;
    }
    REDRAW_CHROME();
}
void AddHistory(const std::wstring&title,const std::wstring&url){
    g_history.erase(std::remove_if(g_history.begin(),g_history.end(),[&](const Entry&e){return e.url==url;}),g_history.end());
    g_history.insert(g_history.begin(),{title,url});
    if(g_history.size()>200) g_history.resize(200);
    if(g_sideOpen&&!g_showBm) RefreshList();
}

static std::wstring StartPageUrl(){
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr,path,MAX_PATH);
    std::wstring p(path);
    size_t slash=p.find_last_of(L"\\/");
    std::wstring dir=(slash==std::wstring::npos)?L".":p.substr(0,slash);
    return L"file:///"+dir+L"/start.html";
}

static void CreateTab(const std::wstring&url=L""){
    std::wstring realUrl=url.empty()?StartPageUrl():url;
    if(g_activeIdx>=0&&g_activeIdx<(int)g_tabs.size()){
        auto&old=g_tabs[g_activeIdx];
        if(old.browser){HWND bh=old.browser->GetHost()->GetWindowHandle();if(bh)ShowWindow(bh,SW_HIDE);}
    }
    int id=++g_tabCtr;
    Tab t;t.id=id;t.title=L"New Tab";t.url=realUrl;t.lastUsed=GetTickCount();
    t.curW=g_tabs.empty()?TargetTabWidth():0.f;
    t.handler=new ClientHandler(id);
    g_tabs.push_back(t);
    g_activeIdx=(int)g_tabs.size()-1;
    RECT br=BrowserRc();
    CefWindowInfo wi;wi.SetAsChild(g_hwnd,CefRect(br.left,br.top,br.right-br.left,br.bottom-br.top));
    CefBrowserSettings bs;
    CefBrowserHost::CreateBrowser(wi,g_tabs[g_activeIdx].handler,W2A(realUrl),bs,nullptr,nullptr);
    SetWindowTextW(g_addr,L"");
    Layout();
}
static void CloseTab(int idx){
    if(idx<0||idx>=(int)g_tabs.size()) return;
    auto&t=g_tabs[idx];
    if(t.favicon){DeleteObject(t.favicon);t.favicon=nullptr;}
    if(t.browser) t.browser->GetHost()->CloseBrowser(true);
    g_tabs.erase(g_tabs.begin()+idx);
    if(g_tabs.empty()){CreateTab();return;}
    g_activeIdx=std::min(g_activeIdx,(int)g_tabs.size()-1);
    auto&a=g_tabs[g_activeIdx];
    if(a.browser){HWND bh=a.browser->GetHost()->GetWindowHandle();if(bh){RECT br=BrowserRc();MoveWindow(bh,br.left,br.top,br.right-br.left,br.bottom-br.top,TRUE);ShowWindow(bh,SW_SHOW);}}
    SetWindowTextW(g_addr,IsStartPage(a.url)?L"":a.url.c_str());
    Layout();
}
static void SwitchTab(int idx){
    if(idx<0||idx>=(int)g_tabs.size()||idx==g_activeIdx) return;
    if(g_activeIdx>=0&&g_activeIdx<(int)g_tabs.size()){
        auto&old=g_tabs[g_activeIdx];old.useCount++;
        if(old.browser){HWND bh=old.browser->GetHost()->GetWindowHandle();if(bh)ShowWindow(bh,SW_HIDE);}
    }
    g_activeIdx=idx;auto&t=g_tabs[idx];t.lastUsed=GetTickCount();t.useCount++;
    if(t.browser){HWND bh=t.browser->GetHost()->GetWindowHandle();if(bh){RECT br=BrowserRc();MoveWindow(bh,br.left,br.top,br.right-br.left,br.bottom-br.top,TRUE);ShowWindow(bh,SW_SHOW);}}
    SetWindowTextW(g_addr,IsStartPage(t.url)?L"":t.url.c_str());
    Layout();
}
static void SmartReorder(){
    if(g_tabs.size()<=1) return;
    int aid=g_activeIdx>=0?g_tabs[g_activeIdx].id:-1;
    std::stable_sort(g_tabs.begin(),g_tabs.end(),[](const Tab&a,const Tab&b){
        return a.useCount*1000.0+a.lastUsed>b.useCount*1000.0+b.lastUsed;});
    if(aid>=0) for(int i=0;i<(int)g_tabs.size();i++) if(g_tabs[i].id==aid){g_activeIdx=i;break;}
    REDRAW_CHROME();
}
static void Navigate(const std::wstring&input){
    if(g_activeIdx<0||g_activeIdx>=(int)g_tabs.size()) return;
    auto&t=g_tabs[g_activeIdx];
    std::wstring url=NormalizeUrl(input);t.url=url;
    SetWindowTextW(g_addr,url.c_str());
    if(t.browser){
        class NavTask:public CefTask{
        public:
            NavTask(CefRefPtr<CefBrowser>b,std::string u):browser(b),url(u){}
            void Execute()override{browser->GetMainFrame()->LoadURL(url);}
            CefRefPtr<CefBrowser> browser;std::string url;
            IMPLEMENT_REFCOUNTING(NavTask);
        };
        CefPostTask(TID_UI,new NavTask(t.browser,W2A(url)));
    }
    REDRAW_CHROME();
}
static void ToolbarClick(int x,int y){
    RECT cr=CR();int bx=SW()+10,by=kTabH+6,bw=30,bh=kToolH-12;
    auto hit=[&](int ox)->bool{return x>=bx+ox&&x<bx+ox+bw&&y>=by&&y<by+bh;};
    if(hit(0)) {if(g_activeIdx>=0&&g_tabs[g_activeIdx].browser)g_tabs[g_activeIdx].browser->GoBack();return;}
    if(hit(34)){if(g_activeIdx>=0&&g_tabs[g_activeIdx].browser)g_tabs[g_activeIdx].browser->GoForward();return;}
    if(hit(68)){if(g_activeIdx>=0&&g_tabs[g_activeIdx].browser)g_tabs[g_activeIdx].browser->Reload();return;}
    int rx=cr.right-3*34-8;
    if(x>=rx&&x<rx+bw&&y>=by&&y<by+bh){
        g_focus=!g_focus;
        if(g_focus){g_focusSecs=25*60;SetTimer(g_hwnd,ID_FOCUS_TIMER,1000,nullptr);}
        else{KillTimer(g_hwnd,ID_FOCUS_TIMER);g_focusSecs=0;}
        REDRAW_CHROME();return;
    }
    if(x>=rx+34&&x<rx+64&&y>=by&&y<by+bh){
        g_dark=!g_dark;g_T=g_dark?kDark:kLight;
        SyncThemeToStartPage();
        REDRAW_CHROME();return;
    }
    if(x>=rx+68&&x<rx+98&&y>=by&&y<by+bh){g_sideOpen=!g_sideOpen;Layout();return;}
}
static void SidebarClick(int x,int y){
    if(!g_sideOpen||x>=kSideW) return;
    RECT cr=CR();int bw=(kSideW-4)/2;
    if(y>=kTabH+4&&y<kTabH+kToolH-4){
        if(x>=2&&x<2+bw){g_showBm=true;RefreshList();REDRAW_CHROME();}
        else{g_showBm=false;RefreshList();REDRAW_CHROME();}
        return;
    }
    if(g_showBm&&y>=cr.bottom-30&&y<cr.bottom-4&&g_activeIdx>=0){
        auto&t=g_tabs[g_activeIdx];
        g_bookmarks.push_back({t.title,t.url});
        RefreshList();REDRAW_CHROME();
    }
}
static void UpdateHover(int x,int y){
    int nt=-1;bool nc=false,np=false;int ntt=-1,nr=-1;
    if(y>=0&&y<kTabH){
        nt=TabHit(x,y);if(nt>=0)nc=CloseHit(nt,x,y);
        RECT pr=PlusRc();if(x>=pr.left&&x<pr.right&&y>=pr.top&&y<pr.bottom)np=true;
    }else if(y>=kTabH&&y<kTabH+kToolH){
        RECT cr=CR();int bx=SW()+10,by=kTabH+6,bw=30,bh=kToolH-12;
        auto hit=[&](int ox)->bool{return x>=bx+ox&&x<bx+ox+bw&&y>=by&&y<by+bh;};
        if(hit(0))ntt=0;else if(hit(34))ntt=1;else if(hit(68))ntt=2;
        int rx=cr.right-3*34-8;
        if(x>=rx&&x<rx+bw&&y>=by&&y<by+bh)nr=0;
        else if(x>=rx+34&&x<rx+64&&y>=by&&y<by+bh)nr=1;
        else if(x>=rx+68&&x<rx+98&&y>=by&&y<by+bh)nr=2;
    }
    if(nt!=g_hoverTab||nc!=g_hoverTabClose||np!=g_hoverPlus||ntt!=g_hoverToolBtn||nr!=g_hoverRightBtn){
        g_hoverTab=nt;g_hoverTabClose=nc;g_hoverPlus=np;g_hoverToolBtn=ntt;g_hoverRightBtn=nr;
        REDRAW_CHROME();
    }
    if(!g_tracking){TRACKMOUSEEVENT tme={sizeof(tme)};tme.dwFlags=TME_LEAVE;tme.hwndTrack=g_hwnd;TrackMouseEvent(&tme);g_tracking=true;}
}
static void ClearHover(){
    g_tracking=false;
    if(g_hoverTab!=-1||g_hoverPlus||g_hoverToolBtn!=-1||g_hoverRightBtn!=-1){
        g_hoverTab=-1;g_hoverTabClose=false;g_hoverPlus=false;g_hoverToolBtn=-1;g_hoverRightBtn=-1;
        REDRAW_CHROME();
    }
}
static LRESULT CALLBACK AddrProc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
    if(m==WM_KEYDOWN&&w==VK_RETURN){wchar_t buf[2048];GetWindowTextW(h,buf,2048);Navigate(buf);return 0;}
    if(m==WM_SETFOCUS){LRESULT r=DefSubclassProc(h,m,w,l);PostMessage(h,EM_SETSEL,0,-1);return r;}
    return DefSubclassProc(h,m,w,l);
}

static LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:
        g_hwnd=hwnd;
        g_font=CreateFontW(13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        g_fontSm=CreateFontW(13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        g_fontIcon=CreateFontW(15,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI Symbol");
        g_addr=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,100,24,hwnd,nullptr,GetModuleHandle(nullptr),nullptr);
        SendMessage(g_addr,WM_SETFONT,(WPARAM)g_font,TRUE);
        SetWindowSubclass(g_addr,AddrProc,1,0);
        g_sbList=CreateWindowExW(0,L"LISTBOX",L"",WS_CHILD|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,0,0,100,100,hwnd,(HMENU)300,GetModuleHandle(nullptr),nullptr);
        SendMessage(g_sbList,WM_SETFONT,(WPARAM)g_font,TRUE);
        ShowWindow(g_sbList,SW_HIDE);
        SetTimer(hwnd,ID_SMART_TIMER,30000,nullptr);
        return 0;
    case WM_SIZE:
        if(wp!=SIZE_MINIMIZED) Layout();
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE:{int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);UpdateHover(x,y);return 0;}
    case WM_MOUSELEAVE: ClearHover(); return 0;
    case WM_PAINT:{
        RECT cr;GetClientRect(hwnd,&cr);
        PAINTSTRUCT ps;HDC hdc=BeginPaint(hwnd,&ps);
        HDC m=CreateCompatibleDC(hdc);
        HBITMAP b=CreateCompatibleBitmap(hdc,cr.right,kTabH+kToolH);
        HBITMAP ob=(HBITMAP)SelectObject(m,b);
        PaintAll(m);
        BitBlt(hdc,0,0,cr.right,kTabH+kToolH,m,0,0,SRCCOPY);
        SelectObject(m,ob);DeleteObject(b);DeleteDC(m);
        EndPaint(hwnd,&ps);return 0;
    }
    case WM_LBUTTONDOWN:{
        int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
        if(g_sideOpen&&x<kSideW){SidebarClick(x,y);return 0;}
        if(y>=0&&y<kTabH){
            RECT pr=PlusRc();
            if(x>=pr.left&&x<pr.right&&y>=pr.top&&y<pr.bottom){CreateTab();return 0;}
            int ti=TabHit(x,y);
            if(ti>=0){CloseHit(ti,x,y)?CloseTab(ti):SwitchTab(ti);return 0;}
            return 0;
        }
        if(y>=kTabH&&y<kTabH+kToolH){ToolbarClick(x,y);return 0;}
        return DefWindowProc(hwnd,msg,wp,lp);
    }
    case WM_COMMAND:
        if(HIWORD(wp)==LBN_DBLCLK&&LOWORD(wp)==300){
            int sel=(int)SendMessage(g_sbList,LB_GETCURSEL,0,0);
            if(sel>=0){auto&lst=g_showBm?g_bookmarks:g_history;if(sel<(int)lst.size())Navigate(lst[sel].url);}
        }
        return 0;
    case WM_TIMER:
        if(wp==ID_FOCUS_TIMER){
            if(g_focusSecs>0){g_focusSecs--;REDRAW_CHROME();}
            if(g_focusSecs==0){KillTimer(hwnd,ID_FOCUS_TIMER);g_focus=false;MessageBoxW(hwnd,L"Focus session complete!",L"Browser",MB_OK|MB_ICONINFORMATION);REDRAW_CHROME();}
        }else if(wp==ID_SMART_TIMER){SmartReorder();}
        else if(wp==ID_ANIM_TIMER){StepTabAnim();}
        return 0;
    case WM_FAVICON_READY:{
        FaviconMsg* msg2=(FaviconMsg*)lp;
        if(msg2){OnFaviconReady(msg2->tabId,msg2->bmp);delete msg2;}
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd,msg,wp,lp);
}

int APIENTRY WinMain(HINSTANCE hInstance,HINSTANCE,LPSTR,int){
    CefMainArgs args(hInstance);
    CefRefPtr<LiteBrowserApp> app(new LiteBrowserApp);
    int ex=CefExecuteProcess(args,app.get(),nullptr);if(ex>=0)return ex;
    CefSettings s;s.no_sandbox=true;
    std::wstring cachePath = GetProfileDir() + L"\\Cache";
    CefString(&s.root_cache_path).FromWString(cachePath);
    CefInitialize(args,s,app.get(),nullptr);
    LoadBlocklist(); // V3.1: load blocklist.txt (baked in at build time) before any tabs open
    WNDCLASSW wc={};wc.lpfnWndProc=WndProc;wc.hInstance=hInstance;
    wc.lpszClassName=kWndClass;wc.hbrBackground=(HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    wc.hIcon=(HICON)LoadImageW(hInstance,MAKEINTRESOURCEW(IDI_ICON1),IMAGE_ICON,0,0,LR_DEFAULTSIZE|LR_SHARED);
    RegisterClassW(&wc);
    g_hwnd=CreateWindowExW(0,kWndClass,L"LiteBrowser",WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,1280,800,nullptr,nullptr,hInstance,nullptr);
    ShowWindow(g_hwnd,SW_SHOWNORMAL);UpdateWindow(g_hwnd);
    CreateTab();
    CefRunMessageLoop();CefShutdown();return 0;
}

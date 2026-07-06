
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cctype>
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "app.h"
#include "client_handler.h"

// ── Constants ─────────────────────────────────────────────
static const int kTabH  = 34;
static const int kToolH = 36;
static const int kSideW = 220;
static const wchar_t kWndClass[] = L"LiteBrowserMain";
static const int ID_FOCUS_TIMER = 200;
static const int ID_SMART_TIMER = 201;

// ── Theme ─────────────────────────────────────────────────
struct Theme { COLORREF bg,chrome,text,border,tabOn,tabOff,sidebar,accent; };
static const Theme kLight{RGB(255,255,255),RGB(235,235,235),RGB(20,20,20),
    RGB(200,200,200),RGB(255,255,255),RGB(220,220,220),RGB(248,248,248),RGB(0,102,204)};
static const Theme kDark{RGB(24,24,24),RGB(36,36,36),RGB(220,220,220),
    RGB(60,60,60),RGB(50,50,50),RGB(36,36,36),RGB(30,30,30),RGB(30,144,255)};
static bool  g_dark = false;
static Theme g_T    = kLight;

// ── Blocker globals (extern in client_handler.h) ──────────
bool g_blockAds      = true;
bool g_blockTrackers = true;
bool g_blockPopups   = true;
bool g_focus         = false;

// ── Tab ───────────────────────────────────────────────────
struct Tab {
    int id;
    std::wstring title, url;
    CefRefPtr<CefBrowser>    browser;
    CefRefPtr<ClientHandler> handler;
    DWORD lastUsed = 0;
    int   useCount = 0;
};
static std::vector<Tab> g_tabs;
static int g_activeIdx = -1;
static int g_tabCtr    = 0;

// ── Sidebar ───────────────────────────────────────────────
static bool g_sideOpen = false;
static bool g_showBm   = true;
struct Entry { std::wstring title, url; };
static std::vector<Entry> g_bookmarks, g_history;

// ── Focus ─────────────────────────────────────────────────
static int g_focusSecs = 0;

// ── Handles ───────────────────────────────────────────────
static HWND  g_hwnd   = nullptr;
static HWND  g_addr   = nullptr;
static HWND  g_sbList = nullptr;
static HFONT g_font   = nullptr;

// ── String helpers ────────────────────────────────────────
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

// ── Layout helpers ────────────────────────────────────────
static RECT CR()  { RECT r; GetClientRect(g_hwnd,&r); return r; }
static int  SW()  { return g_sideOpen ? kSideW : 0; }
static RECT TabBarRc() { RECT c=CR(); return {SW(),0,c.right,kTabH}; }
static RECT ToolbarRc(){ RECT c=CR(); return {SW(),kTabH,c.right,kTabH+kToolH}; }
static RECT BrowserRc(){ RECT c=CR(); return {SW(),kTabH+kToolH,c.right,c.bottom}; }

static RECT TabRc(int i) {
    RECT tb=TabBarRc();
    int n=(int)g_tabs.size(); if(!n) return {tb.left,0,tb.left,kTabH};
    int tw=(tb.right-tb.left-32)/n;
    tw=std::min(200,std::max(80,tw));
    return {tb.left+i*tw,0,tb.left+(i+1)*tw,kTabH};
}
static int TabHit(int x,int y) {
    if (y<0||y>=kTabH) return -1;
    for (int i=0;i<(int)g_tabs.size();i++) {
        RECT r=TabRc(i); if(x>=r.left&&x<r.right) return i;
    }
    return -1;
}
static bool CloseHit(int i,int x,int y) {
    RECT r=TabRc(i); int cx=r.right-14,cy=kTabH/2;
    return abs(x-cx)<9&&abs(y-cy)<9;
}

// ── GDI helpers ───────────────────────────────────────────
static void FillRc(HDC h,RECT r,COLORREF c){HBRUSH b=CreateSolidBrush(c);FillRect(h,&r,b);DeleteObject(b);}
static void Txt(HDC h,const std::wstring& s,RECT r,COLORREF c,UINT f=DT_SINGLELINE|DT_VCENTER|DT_CENTER|DT_END_ELLIPSIS){
    SetTextColor(h,c);SetBkMode(h,TRANSPARENT);
    HFONT o=(HFONT)SelectObject(h,g_font);DrawTextW(h,s.c_str(),-1,&r,f);SelectObject(h,o);
}
static void HLine(HDC h,int x1,int x2,int y,COLORREF c){HPEN p=CreatePen(PS_SOLID,1,c),o=(HPEN)SelectObject(h,p);MoveToEx(h,x1,y,nullptr);LineTo(h,x2,y);SelectObject(h,o);DeleteObject(p);}
static void VLine(HDC h,int x,int y1,int y2,COLORREF c){HPEN p=CreatePen(PS_SOLID,1,c),o=(HPEN)SelectObject(h,p);MoveToEx(h,x,y1,nullptr);LineTo(h,x,y2);SelectObject(h,o);DeleteObject(p);}

// ── Paint ─────────────────────────────────────────────────
static void PaintAll(HDC hdc) {
    RECT cr=CR();
    FillRc(hdc,cr,g_T.bg);

    // Sidebar
    if (g_sideOpen) {
        RECT sr={0,0,kSideW,cr.bottom};
        FillRc(hdc,sr,g_T.sidebar);
        VLine(hdc,kSideW-1,0,cr.bottom,g_T.border);
        int bw=(kSideW-4)/2;
        RECT bmr={2,kTabH+4,2+bw,kTabH+kToolH-4};
        RECT hir={2+bw,kTabH+4,kSideW-2,kTabH+kToolH-4};
        FillRc(hdc,bmr,g_showBm?g_T.tabOn:g_T.tabOff);
        FillRc(hdc,hir,!g_showBm?g_T.tabOn:g_T.tabOff);
        Txt(hdc,L"Bookmarks",bmr,g_T.text);
        Txt(hdc,L"History",hir,g_T.text);
        if (g_showBm) {
            RECT ar={2,cr.bottom-30,kSideW-2,cr.bottom-4};
            FillRc(hdc,ar,g_T.accent);
            Txt(hdc,L"+ Bookmark this page",ar,RGB(255,255,255));
        }
    }

    // Tab bar
    RECT tbr=TabBarRc();
    FillRc(hdc,tbr,g_T.chrome);
    HLine(hdc,tbr.left,tbr.right,kTabH-1,g_T.border);
    for (int i=0;i<(int)g_tabs.size();i++) {
        RECT tr=TabRc(i); bool act=(i==g_activeIdx);
        FillRc(hdc,tr,act?g_T.tabOn:g_T.tabOff);
        if (i>0) VLine(hdc,tr.left,2,kTabH-2,g_T.border);
        RECT txr={tr.left+8,0,tr.right-22,kTabH};
        Txt(hdc,g_tabs[i].title.empty()?L"New Tab":g_tabs[i].title,txr,g_T.text,
            DT_SINGLELINE|DT_VCENTER|DT_LEFT|DT_END_ELLIPSIS);
        RECT xr={tr.right-20,kTabH/2-8,tr.right-4,kTabH/2+8};
        Txt(hdc,L"×",xr,g_T.text);
    }
    RECT plus={tbr.right-28,4,tbr.right-4,kTabH-4};
    FillRc(hdc,plus,g_T.tabOff); Txt(hdc,L"+",plus,g_T.text);

    // Toolbar
    RECT tr=ToolbarRc();
    FillRc(hdc,tr,g_T.chrome);
    HLine(hdc,tr.left,tr.right,tr.bottom-1,g_T.border);

    int bx=SW()+4,by=kTabH+4,bw=28,bh=kToolH-8;
    auto Btn=[&](int x,const wchar_t* lbl,COLORREF bg){
        RECT r={x,by,x+bw,by+bh}; FillRc(hdc,r,bg);
        HPEN p=CreatePen(PS_SOLID,1,g_T.border),o=(HPEN)SelectObject(hdc,p);
        Rectangle(hdc,x,by,x+bw,by+bh); SelectObject(hdc,o); DeleteObject(p);
        Txt(hdc,lbl,r,g_T.text);
    };
    Btn(bx,       L"←",g_T.tabOff);
    Btn(bx+30,    L"→",g_T.tabOff);
    Btn(bx+60,    L"↺",g_T.tabOff);

    int rx=cr.right-3*30-4;
    Btn(rx,     g_focus?L"⊙":L"○", g_focus?RGB(200,50,50):g_T.tabOff);
    Btn(rx+30,  g_dark?L"☀":L"☾",  g_T.tabOff);
    Btn(rx+60,  g_sideOpen?L"◀":L"▶", g_T.tabOff);

    if (g_focus&&g_focusSecs>0) {
        wchar_t buf[32]; swprintf(buf,32,L"%d:%02d",g_focusSecs/60,g_focusSecs%60);
        RECT fr={rx-70,by,rx-4,by+bh};
        Txt(hdc,buf,fr,RGB(255,80,80),DT_SINGLELINE|DT_VCENTER|DT_RIGHT);
    }
}

// ── Layout ────────────────────────────────────────────────
static void Layout() {
    if (!g_hwnd) return;
    RECT cr=CR();
    int addrX=SW()+4+3*30+8;
    int rx=cr.right-3*30-4;
    int addrW=rx-addrX-8;
    if (g_addr) MoveWindow(g_addr,addrX,kTabH+5,std::max(addrW,10),kToolH-10,TRUE);
    if (g_sbList) {
        if (g_sideOpen) {
            MoveWindow(g_sbList,2,kTabH+kToolH,kSideW-4,cr.bottom-kTabH-kToolH-36,TRUE);
            ShowWindow(g_sbList,SW_SHOW);
        } else { ShowWindow(g_sbList,SW_HIDE); }
    }
    if (g_activeIdx>=0&&g_activeIdx<(int)g_tabs.size()) {
        auto& t=g_tabs[g_activeIdx];
        if (t.browser) {
            RECT br=BrowserRc(); HWND bh=t.browser->GetHost()->GetWindowHandle();
            if (bh) MoveWindow(bh,br.left,br.top,br.right-br.left,br.bottom-br.top,TRUE);
        }
    }
    InvalidateRect(g_hwnd,nullptr,FALSE);
}

// ── Sidebar list ──────────────────────────────────────────
static void RefreshList() {
    if (!g_sbList) return;
    SendMessage(g_sbList,LB_RESETCONTENT,0,0);
    auto& lst=g_showBm?g_bookmarks:g_history;
    for (auto& e:lst) SendMessage(g_sbList,LB_ADDSTRING,0,(LPARAM)(e.title.empty()?e.url:e.title).c_str());
}

// ── Callbacks from ClientHandler ──────────────────────────
void OnBrowserCreated(int tabId, CefRefPtr<CefBrowser> browser) {
    for (auto& t:g_tabs) if (t.id==tabId) {
        t.browser=browser;
        if (g_activeIdx>=0&&g_tabs[g_activeIdx].id==tabId) {
            RECT br=BrowserRc(); HWND bh=browser->GetHost()->GetWindowHandle();
            if (bh) { MoveWindow(bh,br.left,br.top,br.right-br.left,br.bottom-br.top,TRUE); ShowWindow(bh,SW_SHOW); }
        }
        break;
    }
}
void OnTitleChanged(int tabId, const std::wstring& title) {
    for (auto& t:g_tabs) if (t.id==tabId) { t.title=title; break; }
    InvalidateRect(g_hwnd,nullptr,FALSE);
}
void OnUrlChanged(int tabId, const std::wstring& url) {
    for (auto& t:g_tabs) if (t.id==tabId) {
        t.url=url;
        if (g_activeIdx>=0&&g_tabs[g_activeIdx].id==tabId) SetWindowTextW(g_addr,url.c_str());
        break;
    }
}
void AddHistory(const std::wstring& title, const std::wstring& url) {
    g_history.erase(std::remove_if(g_history.begin(),g_history.end(),
        [&](const Entry& e){return e.url==url;}),g_history.end());
    g_history.insert(g_history.begin(),{title,url});
    if (g_history.size()>200) g_history.resize(200);
    if (g_sideOpen&&!g_showBm) RefreshList();
}

// ── Tab management ────────────────────────────────────────
static void CreateTab(const std::wstring& url=L"https://www.google.com") {
    if (g_activeIdx>=0&&g_activeIdx<(int)g_tabs.size()) {
        auto& old=g_tabs[g_activeIdx];
        if (old.browser) { HWND bh=old.browser->GetHost()->GetWindowHandle(); if(bh) ShowWindow(bh,SW_HIDE); }
    }
    int id=++g_tabCtr;
    Tab t; t.id=id; t.title=L"New Tab"; t.url=url; t.lastUsed=GetTickCount();
    t.handler=new ClientHandler(id);
    g_tabs.push_back(t);
    g_activeIdx=(int)g_tabs.size()-1;
    RECT br=BrowserRc();
    CefWindowInfo wi; wi.SetAsChild(g_hwnd,CefRect(br.left,br.top,br.right-br.left,br.bottom-br.top));
    CefBrowserSettings bs;
    CefBrowserHost::CreateBrowser(wi,g_tabs[g_activeIdx].handler,W2A(url),bs,nullptr,nullptr);
    SetWindowTextW(g_addr,url.c_str());
    Layout();
}
static void CloseTab(int idx) {
    if (idx<0||idx>=(int)g_tabs.size()) return;
    auto& t=g_tabs[idx]; if(t.browser) t.browser->GetHost()->CloseBrowser(true);
    g_tabs.erase(g_tabs.begin()+idx);
    if (g_tabs.empty()) { CreateTab(); return; }
    g_activeIdx=std::min(g_activeIdx,(int)g_tabs.size()-1);
    auto& a=g_tabs[g_activeIdx];
    if (a.browser) {
        HWND bh=a.browser->GetHost()->GetWindowHandle();
        if (bh) { RECT br=BrowserRc(); MoveWindow(bh,br.left,br.top,br.right-br.left,br.bottom-br.top,TRUE); ShowWindow(bh,SW_SHOW); }
    }
    SetWindowTextW(g_addr,a.url.c_str()); Layout();
}
static void SwitchTab(int idx) {
    if (idx<0||idx>=(int)g_tabs.size()||idx==g_activeIdx) return;
    if (g_activeIdx>=0&&g_activeIdx<(int)g_tabs.size()) {
        auto& old=g_tabs[g_activeIdx]; old.useCount++;
        if (old.browser) { HWND bh=old.browser->GetHost()->GetWindowHandle(); if(bh) ShowWindow(bh,SW_HIDE); }
    }
    g_activeIdx=idx; auto& t=g_tabs[idx]; t.lastUsed=GetTickCount(); t.useCount++;
    if (t.browser) {
        HWND bh=t.browser->GetHost()->GetWindowHandle();
        if (bh) { RECT br=BrowserRc(); MoveWindow(bh,br.left,br.top,br.right-br.left,br.bottom-br.top,TRUE); ShowWindow(bh,SW_SHOW); }
    }
    SetWindowTextW(g_addr,t.url.c_str()); Layout();
}
static void SmartReorder() {
    if (g_tabs.size()<=1) return;
    int aid=g_activeIdx>=0?g_tabs[g_activeIdx].id:-1;
    std::stable_sort(g_tabs.begin(),g_tabs.end(),[](const Tab& a,const Tab& b){
        double sa=a.useCount*1000.0+(double)a.lastUsed;
        double sb=b.useCount*1000.0+(double)b.lastUsed;
        return sa>sb;
    });
    if (aid>=0) for (int i=0;i<(int)g_tabs.size();i++) if(g_tabs[i].id==aid){g_activeIdx=i;break;}
    InvalidateRect(g_hwnd,nullptr,FALSE);
}

// ── Navigate ──────────────────────────────────────────────
static void Navigate(const std::wstring& input) {
    if (g_activeIdx<0||g_activeIdx>=(int)g_tabs.size()) return;
    auto& t=g_tabs[g_activeIdx];
    std::wstring url=NormalizeUrl(input); t.url=url;
    if (t.browser) t.browser->GetMainFrame()->LoadURL(W2A(url));
    SetWindowTextW(g_addr,url.c_str());
}

// ── Click handlers ────────────────────────────────────────
static void ToolbarClick(int x,int y) {
    RECT cr=CR();
    int bx=SW()+4,by=kTabH+4,bw=28,bh=kToolH-8;
    auto hit=[&](int ox)->bool{ return x>=bx+ox&&x<bx+ox+bw&&y>=by&&y<by+bh; };
    if (hit(0))  { if(g_activeIdx>=0&&g_tabs[g_activeIdx].browser) g_tabs[g_activeIdx].browser->GoBack();    return; }
    if (hit(30)) { if(g_activeIdx>=0&&g_tabs[g_activeIdx].browser) g_tabs[g_activeIdx].browser->GoForward(); return; }
    if (hit(60)) { if(g_activeIdx>=0&&g_tabs[g_activeIdx].browser) g_tabs[g_activeIdx].browser->Reload();    return; }
    int rx=cr.right-3*30-4;
    if (x>=rx&&x<rx+bw&&y>=by&&y<by+bh) {
        g_focus=!g_focus;
        if (g_focus) { g_focusSecs=25*60; SetTimer(g_hwnd,ID_FOCUS_TIMER,1000,nullptr); }
        else { KillTimer(g_hwnd,ID_FOCUS_TIMER); g_focusSecs=0; SetWindowTextW(g_hwnd,L"Browser"); }
        InvalidateRect(g_hwnd,nullptr,FALSE); return;
    }
    if (x>=rx+30&&x<rx+58&&y>=by&&y<by+bh) {
        g_dark=!g_dark; g_T=g_dark?kDark:kLight; InvalidateRect(g_hwnd,nullptr,FALSE); return;
    }
    if (x>=rx+60&&x<rx+88&&y>=by&&y<by+bh) {
        g_sideOpen=!g_sideOpen; Layout(); return;
    }
}
static void SidebarClick(int x,int y) {
    if (!g_sideOpen||x>=kSideW) return;
    RECT cr=CR(); int bw=(kSideW-4)/2;
    if (y>=kTabH+4&&y<kTabH+kToolH-4) {
        if (x>=2&&x<2+bw) { g_showBm=true;  RefreshList(); InvalidateRect(g_hwnd,nullptr,FALSE); }
        else               { g_showBm=false; RefreshList(); InvalidateRect(g_hwnd,nullptr,FALSE); }
        return;
    }
    if (g_showBm&&y>=cr.bottom-30&&y<cr.bottom-4&&g_activeIdx>=0) {
        auto& t=g_tabs[g_activeIdx];
        g_bookmarks.push_back({t.title,t.url});
        RefreshList(); InvalidateRect(g_hwnd,nullptr,FALSE);
    }
}

// ── Address bar subclass ──────────────────────────────────
static LRESULT CALLBACK AddrProc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR) {
    if (m==WM_KEYDOWN&&w==VK_RETURN) {
        wchar_t buf[2048]; GetWindowTextW(h,buf,2048); Navigate(buf); return 0;
    }
    return DefSubclassProc(h,m,w,l);
}

// ── WndProc ───────────────────────────────────────────────
static LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CREATE:
        g_hwnd=hwnd;
        g_font=CreateFontW(13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
            DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        g_addr=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",
            WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,
            0,0,100,24,hwnd,nullptr,GetModuleHandle(nullptr),nullptr);
        SendMessage(g_addr,WM_SETFONT,(WPARAM)g_font,TRUE);
        SetWindowSubclass(g_addr,AddrProc,1,0);
        g_sbList=CreateWindowExW(0,L"LISTBOX",L"",
            WS_CHILD|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,
            0,0,100,100,hwnd,(HMENU)300,GetModuleHandle(nullptr),nullptr);
        SendMessage(g_sbList,WM_SETFONT,(WPARAM)g_font,TRUE);
        ShowWindow(g_sbList,SW_HIDE);
        SetTimer(hwnd,ID_SMART_TIMER,30000,nullptr);
        return 0;

    case WM_SIZE:    Layout(); return 0;
    case WM_ERASEBKGND: return 1;

    case WM_PAINT: {
        RECT cr; GetClientRect(hwnd,&cr);
        PAINTSTRUCT ps; HDC hdc=BeginPaint(hwnd,&ps);
        HDC m=CreateCompatibleDC(hdc);
        HBITMAP b=CreateCompatibleBitmap(hdc,cr.right,cr.bottom);
        HBITMAP ob=(HBITMAP)SelectObject(m,b);
        PaintAll(m);
        BitBlt(hdc,0,0,cr.right,cr.bottom,m,0,0,SRCCOPY);
        SelectObject(m,ob); DeleteObject(b); DeleteDC(m);
        EndPaint(hwnd,&ps); return 0;
    }
    case WM_LBUTTONDOWN: {
        int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
        if (g_sideOpen&&x<kSideW) { SidebarClick(x,y); return 0; }
        if (y<kTabH) {
            RECT tbr=TabBarRc();
            if (x>=tbr.right-28&&x<tbr.right-4&&y>=4&&y<kTabH-4) { CreateTab(); return 0; }
            int ti=TabHit(x,y);
            if (ti>=0) CloseHit(ti,x,y)?CloseTab(ti):SwitchTab(ti);
            return 0;
        }
        if (y>=kTabH&&y<kTabH+kToolH) { ToolbarClick(x,y); return 0; }
        return 0;
    }
    case WM_COMMAND:
        if (HIWORD(wp)==LBN_DBLCLK&&LOWORD(wp)==300) {
            int sel=(int)SendMessage(g_sbList,LB_GETCURSEL,0,0);
            if (sel>=0) {
                auto& lst=g_showBm?g_bookmarks:g_history;
                if (sel<(int)lst.size()) Navigate(lst[sel].url);
            }
        }
        return 0;

    case WM_TIMER:
        if (wp==ID_FOCUS_TIMER) {
            if (g_focusSecs>0) { g_focusSecs--; InvalidateRect(hwnd,nullptr,FALSE); }
            if (g_focusSecs==0) {
                KillTimer(hwnd,ID_FOCUS_TIMER); g_focus=false;
                SetWindowTextW(hwnd,L"Browser");
                MessageBoxW(hwnd,L"Focus session complete! 🎉",L"Browser",MB_OK|MB_ICONINFORMATION);
                InvalidateRect(hwnd,nullptr,FALSE);
            }
        } else if (wp==ID_SMART_TIMER) { SmartReorder(); }
        return 0;

    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd,msg,wp,lp);
}

// ── WinMain ───────────────────────────────────────────────
int APIENTRY WinMain(HINSTANCE hInstance,HINSTANCE,LPSTR,int) {
    CefMainArgs args(hInstance);
    CefRefPtr<LiteBrowserApp> app(new LiteBrowserApp);
    int ex=CefExecuteProcess(args,app.get(),nullptr); if(ex>=0) return ex;
    CefSettings s; s.no_sandbox=true;
    CefInitialize(args,s,app.get(),nullptr);
    WNDCLASSW wc={}; wc.lpfnWndProc=WndProc; wc.hInstance=hInstance;
    wc.lpszClassName=kWndClass; wc.hbrBackground=(HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW); RegisterClassW(&wc);
    g_hwnd=CreateWindowExW(0,kWndClass,L"Browser",WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,1280,800,nullptr,nullptr,hInstance,nullptr);
    ShowWindow(g_hwnd,SW_SHOWNORMAL); UpdateWindow(g_hwnd);
    CreateTab(L"https://www.google.com");
    CefRunMessageLoop(); CefShutdown(); return 0;
}

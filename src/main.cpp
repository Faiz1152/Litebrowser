// LiteBrowser - minimal independent browser shell
// Window with a single search/address bar on top, and the web page filling the rest.
// No bookmarks, no profile, no built-in AI, no branding.

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <cstdio>
#include <cctype>

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_command_line.h"
#include "app.h"
#include "client_handler.h"

namespace {

HWND g_main_hwnd = nullptr;
HWND g_address_bar = nullptr;
CefRefPtr<ClientHandler> g_client_handler;

const int kToolbarHeight = 36;
const wchar_t kWindowClassName[] = L"LiteBrowserMainWindow";

std::string Narrow(const std::wstring& wide) {
  if (wide.empty()) return std::string();
  int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(),
                                  nullptr, 0, nullptr, nullptr);
  std::string out(size, 0);
  WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), &out[0],
                       size, nullptr, nullptr);
  return out;
}

std::wstring Widen(const std::string& narrow) {
  if (narrow.empty()) return std::wstring();
  int size = MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), (int)narrow.size(),
                                  nullptr, 0);
  std::wstring out(size, 0);
  MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), (int)narrow.size(), &out[0],
                       size);
  return out;
}

// Turn whatever the user typed into either a real URL or a search query.
std::string NormalizeInput(const std::string& raw) {
  std::string input = raw;
  // Trim whitespace
  size_t start = input.find_first_not_of(" \t");
  size_t end = input.find_last_not_of(" \t");
  if (start == std::string::npos) return "https://www.google.com";
  input = input.substr(start, end - start + 1);

  bool has_scheme = input.find("://") != std::string::npos;
  bool looks_like_domain =
      input.find(' ') == std::string::npos &&
      input.find('.') != std::string::npos;

  if (has_scheme) return input;
  if (looks_like_domain) return "https://" + input;

  // Treat as a search query.
  std::string escaped;
  for (char c : input) {
    if (isalnum((unsigned char)c)) {
      escaped += c;
    } else if (c == ' ') {
      escaped += '+';
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      escaped += buf;
    }
  }
  return "https://www.google.com/search?q=" + escaped;
}

void NavigateFromAddressBar() {
  if (!g_client_handler || !g_client_handler->GetBrowser()) return;

  wchar_t buf[2048];
  GetWindowTextW(g_address_bar, buf, 2048);
  std::string url = NormalizeInput(Narrow(buf));

  g_client_handler->GetBrowser()->GetMainFrame()->LoadURL(url);
}

void ResizeChildren(int width, int height) {
  if (g_address_bar) {
    MoveWindow(g_address_bar, 8, 6, width - 16, kToolbarHeight - 12, TRUE);
  }
  if (g_client_handler && g_client_handler->GetBrowser()) {
    HWND browser_hwnd =
        g_client_handler->GetBrowser()->GetHost()->GetWindowHandle();
    if (browser_hwnd) {
      MoveWindow(browser_hwnd, 0, kToolbarHeight, width,
                 height - kToolbarHeight, TRUE);
    }
  }
}

LRESULT CALLBACK AddressBarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                 UINT_PTR id, DWORD_PTR ref) {
  if (msg == WM_KEYDOWN && wp == VK_RETURN) {
    NavigateFromAddressBar();
    return 0;
  }
  return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE: {
      g_address_bar = CreateWindowExW(
          0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
          8, 6, 400, kToolbarHeight - 12, hwnd, nullptr,
          GetModuleHandle(nullptr), nullptr);
      SetWindowSubclass(g_address_bar, AddressBarProc, 1, 0);

      HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
      SendMessage(g_address_bar, WM_SETFONT, (WPARAM)font, TRUE);
      return 0;
    }
    case WM_SIZE: {
      ResizeChildren(LOWORD(lp), HIWORD(lp));
      return 0;
    }
    case WM_ERASEBKGND: {
      RECT rc;
      GetClientRect(hwnd, &rc);
      rc.bottom = kToolbarHeight;
      HBRUSH white = (HBRUSH)GetStockObject(WHITE_BRUSH);
      FillRect((HDC)wp, &rc, white);
      return 1;
    }
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProc(hwnd, msg, wp, lp);
}

void CreateMainWindow(HINSTANCE hinstance) {
  WNDCLASSW wc = {};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hinstance;
  wc.lpszClassName = kWindowClassName;
  wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  RegisterClassW(&wc);

  g_main_hwnd = CreateWindowExW(
      0, kWindowClassName, L"Browser", WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, 1200, 800, nullptr, nullptr, hinstance,
      nullptr);

  ShowWindow(g_main_hwnd, SW_SHOWNORMAL);
  UpdateWindow(g_main_hwnd);
}

}  // namespace

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  CefMainArgs main_args(hInstance);

  CefRefPtr<LiteBrowserApp> app(new LiteBrowserApp);

  // CEF re-launches this same exe for its helper processes (renderer, gpu,
  // etc). This call detects that case and runs the helper logic instead of
  // showing a window. Must happen before any window is created.
  int exit_code = CefExecuteProcess(main_args, app.get(), nullptr);
  if (exit_code >= 0) return exit_code;

  CefSettings settings;
  settings.no_sandbox = true;  // simplifies things; see README for tradeoffs
  settings.windowless_rendering_enabled = false;

  CefInitialize(main_args, settings, app.get(), nullptr);

  CreateMainWindow(hInstance);

  CefWindowInfo window_info;
  window_info.SetAsChild(g_main_hwnd, CefRect(0, kToolbarHeight, 1200, 800 - kToolbarHeight));

  CefBrowserSettings browser_settings;
  g_client_handler = new ClientHandler();

  CefBrowserHost::CreateBrowser(window_info, g_client_handler, "https://www.google.com",
                                 browser_settings, nullptr, nullptr);

  CefRunMessageLoop();

  CefShutdown();
  return 0;
}

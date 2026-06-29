// LiteBrowser - minimal independent browser shell
// Window with a single search/address bar on top, and the web page filling the rest.

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

// (kept minimal for template)

std::string NormalizeInput(const std::string& raw) {
  std::string input = raw;
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

  return "https://www.google.com/search?q=" + input;
}

} // namespace

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {

  CefMainArgs main_args(hInstance);
  CefRefPtr<CefApp> app;

  int exit_code = CefExecuteProcess(main_args, app, nullptr);
  if (exit_code >= 0) return exit_code;

  CefSettings settings;
  settings.no_sandbox = true;

  CefInitialize(main_args, settings, app, nullptr);

  CefWindowInfo window_info;
  CefBrowserSettings browser_settings;

  std::string homepage = "file:///assets/start.html";

  CefBrowserHost::CreateBrowser(
      window_info,
      nullptr,
      homepage,
      browser_settings,
      nullptr,
      nullptr);

  CefRunMessageLoop();
  CefShutdown();
  return 0;
}

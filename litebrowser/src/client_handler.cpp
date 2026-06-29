#include "client_handler.h"

void ClientHandler::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  browser_ = browser;
}

void ClientHandler::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  browser_ = nullptr;
}

#include "BrowserClient.h"

#include "WallpaperEngine/Render/Wallpapers/CWeb.h"

using namespace WallpaperEngine::WebBrowser::CEF;

BrowserClient::BrowserClient (
    CefRefPtr<CefRenderHandler> ptr,
    WallpaperEngine::Render::Wallpapers::CWeb* owner
) : m_renderHandler (std::move (ptr)), m_owner (owner) { }

CefRefPtr<CefRenderHandler> BrowserClient::GetRenderHandler () { return m_renderHandler; }

CefRefPtr<CefLifeSpanHandler> BrowserClient::GetLifeSpanHandler () { return this; }

bool BrowserClient::OnBeforePopup (
    CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString&, const CefString&, WindowOpenDisposition, bool,
    const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&, CefBrowserSettings&,
    CefRefPtr<CefDictionaryValue>&, bool*
) {
    // Web wallpapers render into one fixed off-screen surface. A popup sharing
    // this client would outlive that surface and make browser shutdown ambiguous.
    return true;
}

void BrowserClient::OnAfterCreated (CefRefPtr<CefBrowser> browser) {
    this->m_createdBrowser = true;
    this->m_browserIdentifiers.insert (browser->GetIdentifier ());
}

void BrowserClient::OnBeforeClose (CefRefPtr<CefBrowser> browser) {
    const int identifier = browser->GetIdentifier ();
    if (this->m_owner) this->m_owner->browserClosed (identifier);
    this->m_browserIdentifiers.erase (identifier);
}

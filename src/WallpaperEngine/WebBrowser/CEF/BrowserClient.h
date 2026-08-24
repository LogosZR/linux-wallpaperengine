#pragma once

#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"

#include <set>

namespace WallpaperEngine::Render::Wallpapers {
class CWeb;
}

namespace WallpaperEngine::WebBrowser::CEF {
// *************************************************************************
//! \brief Provide access to browser-instance-specific callbacks. A single
//! CefClient instance can be shared among any number of browsers.
// *************************************************************************
class BrowserClient : public CefClient, public CefLifeSpanHandler {
public:
    BrowserClient (
	CefRefPtr<CefRenderHandler> ptr,
	WallpaperEngine::Render::Wallpapers::CWeb* owner
    );

    [[nodiscard]] CefRefPtr<CefRenderHandler> GetRenderHandler () override;
    [[nodiscard]] CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler () override;
    bool OnBeforePopup (
	CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int popupId,
	const CefString& targetUrl, const CefString& targetFrameName,
	WindowOpenDisposition targetDisposition, bool userGesture,
	const CefPopupFeatures& popupFeatures, CefWindowInfo& windowInfo,
	CefRefPtr<CefClient>& client, CefBrowserSettings& settings,
	CefRefPtr<CefDictionaryValue>& extraInfo, bool* noJavascriptAccess
    ) override;
    void OnAfterCreated (CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose (CefRefPtr<CefBrowser> browser) override;
    [[nodiscard]] bool allClosed () const { return m_createdBrowser && m_browserIdentifiers.empty (); }

    CefRefPtr<CefRenderHandler> m_renderHandler = nullptr;
    WallpaperEngine::Render::Wallpapers::CWeb* m_owner = nullptr;
    std::set<int> m_browserIdentifiers;
    bool m_createdBrowser = false;

    IMPLEMENT_REFCOUNTING (BrowserClient);
};
} // namespace WallpaperEngine::WebBrowser::CEF
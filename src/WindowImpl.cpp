#include "berkelium/Platform.hpp"
#include "ContextImpl.hpp"
#include "RenderWidget.hpp"
#include "WindowImpl.hpp"
#include "MemoryRenderViewHost.hpp"
#include "Root.hpp"
#include "berkelium/WindowDelegate.hpp"

#include "base/file_util.h"
#include "net/base/net_util.h"
#include "chrome/browser/renderer_host/render_view_host.h"
#include "chrome/common/render_messages.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/browser/dom_ui/dom_ui.h"
#include "chrome/browser/dom_ui/dom_ui_factory.h"
#include "chrome/browser/renderer_host/render_widget_host_view.h"
#include "chrome/browser/browser_url_handler.h"
namespace Berkelium {
//WindowImpl temp;
void WindowImpl::init(SiteInstance*site) {
    mRenderManager.reset(new RenderViewHostManager(this,this));

    mRenderManager->Init(profile(),
                          site,
                          MSG_ROUTING_NONE,
                          NULL);
}

WindowImpl::WindowImpl(const Context*otherContext):Window(otherContext) {
    init(mContext->getImpl()->getSiteInstance());
    mLastNavEntry = NULL;
    mNavEntry = NULL;
}
WindowImpl::~WindowImpl() {
    mRenderManager.reset();
    delete mLastNavEntry;
    delete mNavEntry;
}

RenderProcessHost *WindowImpl::process() const {
    return host()->process();
}
RenderWidgetHostView *WindowImpl::view() const {
    if (!host()) {
        return NULL;
    }
    return host()->view();
}
RenderViewHost *WindowImpl::host() const {
    return mRenderManager->current_host();
}


void MakeNavigateParams(const NavigationEntry& entry, bool reload,
                        ViewMsg_Navigate_Params* params) {
  params->page_id = entry.page_id();
  params->url = entry.url();
  params->referrer = entry.referrer();
  params->transition = entry.transition_type();
  params->state = entry.content_state();
  params->reload = reload;
  params->request_time = base::Time::Now();
}

void WindowImpl::resize(int width, int height) {
    SetContainerBounds(gfx::Rect(0, 0, width, height));
}

void WindowImpl::SetContainerBounds (const gfx::Rect &rc) {
    mRect = rc;
    RenderWidgetHostView* myview = view();
    if (myview) {
        myview->SetSize(this->GetContainerSize());
    }
    RenderViewHost* myhost = host();
    if (myhost) {
        myhost->WasResized();
    }
}

bool WindowImpl::navigateTo(const std::string &url) {
    return doNavigateTo(GURL(url), GURL(), false);
}
bool WindowImpl::doNavigateTo(
        const GURL &newURL,
        const GURL &referrerURL,
        bool reload)
{
    if (mLastNavEntry) {
        delete mLastNavEntry;
    }
    mLastNavEntry = mNavEntry;
    mNavEntry = CreateNavigationEntry(
        newURL,
        referrerURL,
        PageTransition::TYPED);

    if (!mNavEntry) {
        mNavEntry = mLastNavEntry;
        return false;
    }

    RenderViewHost* dest_render_view_host = mRenderManager->Navigate(*mNavEntry);
    if (!dest_render_view_host) {
        return false;  // Unable to create the desired render view host.
    }

    // Navigate in the desired RenderViewHost.
    ViewMsg_Navigate_Params navigate_params;
    MakeNavigateParams(*mNavEntry, reload, &navigate_params);
    dest_render_view_host->Navigate(navigate_params);
    return true;
}

NavigationEntry* WindowImpl::CreateNavigationEntry(
    const GURL& url, const GURL& referrer, PageTransition::Type transition) {
  // Allow the browser URL handler to rewrite the URL. This will, for example,
  // remove "view-source:" from the beginning of the URL to get the URL that
  // will actually be loaded. This real URL won't be shown to the user, just
  // used internally.
  GURL loaded_url(url);
  BrowserURLHandler::RewriteURLIfNecessary(&loaded_url, profile());

  NavigationEntry* entry = new NavigationEntry(NULL, -1, loaded_url, referrer,
                                               string16(), transition);
  entry->set_virtual_url(url);
  entry->set_user_typed_url(url);
  if (url.SchemeIsFile()) {
    entry->set_title(WideToUTF16Hack(
        file_util::GetFilenameFromPath(net::FormatUrl(url, L"en_US"))));
  }
  return entry;
}



Profile* WindowImpl::profile() const{
    //return Root::getSingleton().getProfile();
    return getContextImpl()->profile();
}
WindowImpl* WindowImpl::getImpl() {
    return this;
}

ContextImpl *WindowImpl::getContextImpl() const {
    return static_cast<ContextImpl*>(getContext());
}


/******* RenderViewHostManager::Delegate *******/

bool WindowImpl::CreateRenderViewForRenderManager(
    RenderViewHost* render_view_host) {

  RenderWidget* rwh_view = 
      static_cast<RenderWidget*>(this->CreateViewForWidget(render_view_host));

  if (!render_view_host->CreateRenderView())
    return false;

  // Now that the RenderView has been created, we need to tell it its size.
  rwh_view->SetSize(this->GetContainerSize());
  render_view_host->set_view(rwh_view);

  UpdateMaxPageIDIfNecessary(render_view_host->site_instance(),
                             render_view_host);
  return true;

}

void WindowImpl::UpdateMaxPageIDIfNecessary(SiteInstance* site_instance,
                                             RenderViewHost* rvh) {
  // If we are creating a RVH for a restored controller, then we might
  // have more page IDs than the SiteInstance's current max page ID.  We must
  // make sure that the max page ID is larger than any restored page ID.
  // Note that it is ok for conflicting page IDs to exist in another tab
  // (i.e., NavigationController), but if any page ID is larger than the max,
  // the back/forward list will get confused.

#if 0
    int max_restored_page_id = controller_.max_restored_page_id();
  if (max_restored_page_id > 0) {

    int curr_max_page_id = site_instance->max_page_id();
    if (max_restored_page_id > curr_max_page_id) {
      // Need to update the site instance immediately.
      site_instance->UpdateMaxPageID(max_restored_page_id);

      // Also tell the renderer to update its internal representation.  We
      // need to reserve enough IDs to make all restored page IDs less than
      // the max.
      if (curr_max_page_id < 0)
        curr_max_page_id = 0;
      rvh->ReservePageIDRange(max_restored_page_id - curr_max_page_id);
    }
  }
#endif
}


// See 'Browser::BeforeUnloadFired' in chrome/browser/browser.cc
// for an example of how to properly handle beforeUnload and unload
// in the case of the user closing a whole window.

void WindowImpl::BeforeUnloadFiredFromRenderManager(
    bool proceed,
    bool* proceed_to_fire_unload)
{
    *proceed_to_fire_unload = proceed;
    if (mDelegate) {
        if (proceed) {
            mDelegate->onBeforeUnload(this, proceed_to_fire_unload);
        } else {
            mDelegate->onCancelUnload(this);
        }
    }
}
void WindowImpl::DidStartLoadingFromRenderManager(
    RenderViewHost* render_view_host) {

    DidStartLoading(render_view_host);
}

void WindowImpl::DidStartLoading(
    RenderViewHost* render_view_host) {

    mRenderManager->SetIsLoading(true);

    if (mDelegate) {
//        mDelegate->onStartLoading(this);
    }
}
void WindowImpl::DidStopLoading(
    RenderViewHost* render_view_host) {

    mRenderManager->SetIsLoading(false);

/*
    if (mDelegate) {
        mDelegate->onStopLoading(this);
    }
*/
}
void WindowImpl::RenderViewGoneFromRenderManager(
    RenderViewHost* render_view_host) {
    if (render_view_host != static_cast<RenderViewHost*>(host())) {
        return; // The pending destination page crashed: don't care.
    }
    if (mDelegate) {
        mDelegate->onCrashed(this);
    }
}
void WindowImpl::UpdateRenderViewSizeForRenderManager() {
    SetContainerBounds(mRect);
}
void WindowImpl::NotifyRenderViewHostSwitchedFromRenderManager(RenderViewHostSwitchedDetails*details) {
    // Called in two cases:
    //   * We called Navigate() and the old_host is null.
    //   * We just switched to a new renderer (e.g. via a cross domain link)

    // Two members: details->old_host, details->new_host

    // Note: RenderWidget::Hide() and ::Show() will also likely be called
    // in these cases.
}
void WindowImpl::NotifySwappedFromRenderManager() {
    // Called after NotifyRenderViewHostSwitchedFromRenderManager, but now
    // the old RenderViewHost has been deleted.
    // (only called if there was a previous host, not in a fresh window)

    // Used in Chrome just to update the task manager process groupings.
}
Profile* WindowImpl::GetProfileForRenderManager() const{
    return profile();
}
NavigationEntry* WindowImpl::GetEntryAtOffsetForRenderManager(int offset) {
    return mNavEntry;
}
DOMUI* WindowImpl::CreateDOMUIForRenderManager(const GURL& url) {
    return NULL;
}
NavigationEntry* WindowImpl::GetLastCommittedNavigationEntryForRenderManager() {
    return mNavEntry;
}

int WindowImpl::GetBrowserWindowID() const {
    // FIXME!!!!
    return 0; //controller_.window_id().id();
}
ViewType::Type WindowImpl::GetRenderViewType()const {
    return ViewType::TAB_CONTENTS;
}

/******* RenderViewHostDelegate *******/

RenderViewHostDelegate::View* WindowImpl::GetViewDelegate() {
    return this;
}
RenderViewHostDelegate::Resource* WindowImpl::GetResourceDelegate() {
    return this;
}

/******* RenderViewHostDelegate::Resource *******/

RenderWidgetHostView* WindowImpl::CreateViewForWidget(RenderWidgetHost*render_widget_host) {
    return new RenderWidget(this, render_widget_host);
}

void WindowImpl::DidStartProvisionalLoadForFrame(
        RenderViewHost* render_view_host,
        bool is_main_frame,
        const GURL& url) {
    if (!is_main_frame) {
        return;
    }
    if (render_view_host != static_cast<RenderViewHost*>(host())) {
        return;
    }
    mDelegate->onStartLoading(this, url.spec());
    if (mDelegate) {
        mDelegate->onAddressBarChanged(this, url.spec());
    }
}

void WindowImpl::DidStartReceivingResourceResponse(
        ResourceRequestDetails* details) {
    // See "chrome/browser/renderer_host/resource_request_details.h"
    // for list of accessor functions.
}

void WindowImpl::DidRedirectProvisionalLoad(
    int32 page_id,
    const GURL& source_url,
    const GURL& target_url)
{
    // should use page_id to lookup in history
    NavigationEntry *entry = mNavEntry;
    if (entry->url() == source_url) {
        entry->set_url(target_url);
    }
}

void WindowImpl::DidRedirectResource(ResourceRequestDetails* details) {
    // Only accessor function:
    // details->new_url();
}

void WindowImpl::DidLoadResourceFromMemoryCache(
        const GURL& url,
        const std::string& frame_origin,
        const std::string& main_frame_origin,
        const std::string& security_info) {
}

void WindowImpl::DidFailProvisionalLoadWithError(
        RenderViewHost* render_view_host,
        bool is_main_frame,
        int error_code,
        const GURL& url,
        bool showing_repost_interstitial) {
    mRenderManager->RendererAbortedProvisionalLoad(render_view_host);
}

void WindowImpl::DocumentLoadedInFrame() {
    if (mDelegate) {
        mDelegate->onLoad(this);
    }
}

/******* RenderViewHostDelegate::View *******/
void WindowImpl::CreateNewWindow(int route_id,
                                 base::WaitableEvent* modal_dialog_event) {
    // TODO: Support multiple windows.
}
void WindowImpl::CreateNewWidget(int route_id, bool activatable) {
    // TODO: Support multiple windows.
}
void WindowImpl::ShowCreatedWindow(int route_id,
                                   WindowOpenDisposition disposition,
                                   const gfx::Rect& initial_pos,
                                   bool user_gesture,
                                   const GURL& creator_url) {
    WindowImpl *win = new WindowImpl(getContext());
    win->resize(initial_pos.width(), initial_pos.height());
    if (mDelegate) {
        mDelegate->onCreatedWindow(this, win);
    }
}
void WindowImpl::ShowCreatedWidget(int route_id,
                                   const gfx::Rect& initial_pos) {
}
void WindowImpl::ShowContextMenu(const ContextMenuParams& params) {
    // TODO: Add context menu event
}
void WindowImpl::StartDragging(const WebDropData& drop_data,
                               WebKit::WebDragOperationsMask allowed_ops) {
    // TODO: Add dragging event
}
void WindowImpl::UpdateDragCursor(WebKit::WebDragOperation operation) {
    // TODO: Add dragging event
}
void WindowImpl::GotFocus() {
    // Useless: just calls this when we hand it an input event.
}
void WindowImpl::TakeFocus(bool reverse) {
}
void WindowImpl::HandleKeyboardEvent(const NativeWebKeyboardEvent& event) {
    // Useless: just calls this when we hand it an input event.
}
void WindowImpl::HandleMouseEvent() {
    // Useless: just calls this when we hand it an input event.
}
void WindowImpl::HandleMouseLeave() {
    // Useless: just calls this when we hand it an input event.
}
void WindowImpl::UpdatePreferredWidth(int pref_width) {
}


}

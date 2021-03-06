#include "config.h"
#include "ylib.h"
#include "yxtray.h"
#include "yrect.h"
#include "yxapp.h"
#include "prefs.h"
#include "yprefs.h"
#include "wmtaskbar.h"
#include "sysdep.h"

class YXTrayProxy: public YWindow {
public:
    YXTrayProxy(const char *atom, YXTray *tray, YWindow *aParent = 0);
    ~YXTrayProxy();

    virtual void handleClientMessage(const XClientMessageEvent &message);
private:
    Atom _NET_SYSTEM_TRAY_OPCODE;
    Atom _NET_SYSTEM_TRAY_MESSAGE_DATA;
    Atom _NET_SYSTEM_TRAY_S0; /// FIXME
    YXTray *fTray;
};

YXTrayProxy::YXTrayProxy(const char *atom, YXTray *tray, YWindow *aParent):
    YWindow(aParent)
{
    Atom manager = XInternAtom(xapp->display(), "MANAGER", False);
    fTray = tray;

    _NET_SYSTEM_TRAY_OPCODE = XInternAtom(xapp->display(),
                                          "_NET_SYSTEM_TRAY_OPCODE",
                                          False);
    _NET_SYSTEM_TRAY_MESSAGE_DATA =
        XInternAtom(xapp->display(),
                    "_NET_SYSTEM_TRAY_MESSAGE_DATA",
                    False);
    _NET_SYSTEM_TRAY_S0 = XInternAtom(xapp->display(),
                                      atom,
                                      False);

    XSetSelectionOwner(xapp->display(),
                       _NET_SYSTEM_TRAY_S0,
                       handle(),
                       CurrentTime);

    XClientMessageEvent xev;
    memset(&xev, 0, sizeof(xev));

    xev.type = ClientMessage;
    xev.window = desktop->handle();
    xev.message_type = manager;
    xev.format = 32;
    xev.data.l[0] = CurrentTime;
    xev.data.l[1] = _NET_SYSTEM_TRAY_S0;
    xev.data.l[2] = handle();

    XSendEvent(xapp->display(), desktop->handle(), False, StructureNotifyMask, (XEvent *) &xev);
}

YXTrayProxy::~YXTrayProxy() {
    XSetSelectionOwner(xapp->display(),
                       _NET_SYSTEM_TRAY_S0,
                       None,
                       CurrentTime);
}

void YXTrayProxy::handleClientMessage(const XClientMessageEvent &message) {
/// TODO #warning "implement systray notifications"
    if (message.message_type == _NET_SYSTEM_TRAY_OPCODE) {
        //printf("message data %ld %ld %ld %ld %ld\n", message.data.l[0], message.data.l[1], message.data.l[2], message.data.l[3], message.data.l[4]);
        if (message.data.l[1] == SYSTEM_TRAY_REQUEST_DOCK) {
            MSG(("systemTrayRequestDock"));
            fTray->trayRequestDock(message.data.l[2]);
        } else if (message.data.l[1] == SYSTEM_TRAY_BEGIN_MESSAGE) {
            MSG(("systemTrayBeginMessage"));
            //fTray->trayBeginMessage();
        } else if (message.data.l[1] == SYSTEM_TRAY_CANCEL_MESSAGE) {
            MSG(("systemTrayCancelMessage"));
            //fTray->trayCancelMessage();
        } else {
            MSG(("systemTray???Message"));
        }
    } else if (message.message_type == _NET_SYSTEM_TRAY_MESSAGE_DATA) {
        MSG(("systemTrayMessageData"));
    } else {
        MSG(("handleClientMessage: do nothing"));
    }
}

YXTrayEmbedder::YXTrayEmbedder(YXTray *tray, Window win): YXEmbed(tray) {
    fTray = tray;
    setStyle(wsManager);
    fDocked = new YXEmbedClient(this, this, win);

    XSetWindowBorderWidth(xapp->display(),
                          client_handle(),
                          0);

    XAddToSaveSet(xapp->display(), client_handle());

    client()->reparent(this, 0, 0);
    fVisible = true;
    fDocked->show();
}

YXTrayEmbedder::~YXTrayEmbedder() {
    fDocked->hide();
    fDocked->reparent(desktop, 0, 0);
    delete fDocked;
    fDocked = 0;
}

void YXTrayEmbedder::detach() {
    XAddToSaveSet(xapp->display(), fDocked->handle());

    fDocked->reparent(desktop, 0, 0);
    fDocked->hide();
    XRemoveFromSaveSet(xapp->display(), fDocked->handle());
}

void YXTrayEmbedder::destroyedClient(Window win) {
    fTray->destroyedClient(win);
}

void YXTrayEmbedder::handleClientUnmap(Window win) {
    fTray->showClient(win, false);
}

void YXTrayEmbedder::paint(Graphics &g, const YRect &/*r*/) {
#ifdef CONFIG_TASKBAR
    g.setColor(getTaskBarBg());
#endif
    g.fillRect(0, 0, width(), height());
}

void YXTrayEmbedder::configure(const YRect &r) {
    YXEmbed::configure(r);
    fDocked->setGeometry(YRect(0, 0, r.width(), r.height()));
}

void YXTrayEmbedder::handleConfigureRequest(const XConfigureRequestEvent &configureRequest)
{
    fTray->handleConfigureRequest(configureRequest);
}

void YXTrayEmbedder::handleMapRequest(const XMapRequestEvent &mapRequest) {
    fDocked->show();
    fTray->showClient(mapRequest.window, true);
}

YXTray::YXTray(YXTrayNotifier *notifier,
               bool internal,
               const char *atom,
               YWindow *aParent):
    YWindow(aParent)
{
    fNotifier = notifier;
    fInternal = internal;
    fTrayProxy = new YXTrayProxy(atom, this);
    show();
#ifndef LITE
#ifdef CONFIG_TASKBAR
    XSetWindowBackground(xapp->display(), handle(), getTaskBarBg()->pixel());
#endif
    XClearArea(xapp->display(), handle(), 0, 0, 0, 0, True);
#endif
}

YXTray::~YXTray() {
    delete fTrayProxy; fTrayProxy = 0;
}

void YXTray::getScaleSize(int *ww, int *hh)
{
    // check if height / max_height < width / max_width. */
    if (*hh * trayIconMaxWidth < *ww * trayIconMaxHeight) {
        // the given icon is too wide.
        *hh = trayIconMaxWidth * *hh / *ww;
        *ww = trayIconMaxWidth;
    } else {
        // the given icon is too tall.
        *hh = trayIconMaxHeight;
        *ww = trayIconMaxHeight * *ww / *hh;
    }
}

void YXTray::trayRequestDock(Window win) {
    MSG(("trayRequestDock win %lX", win));

    destroyedClient(win);
    YXTrayEmbedder *embed = new YXTrayEmbedder(this, win);

    MSG(("size %d %d", embed->client()->width(), embed->client()->height()));

    int ww = embed->client()->width();
    int hh = embed->client()->height();

    /* Workaround for GTK-Apps */
    if (!(ww == 0 || hh == 0)){
        if (!fInternal) {
            // scale too big icons
            getScaleSize(&ww, &hh);
        }
        embed->setSize(ww, hh);
        embed->fVisible = true;
        fDocked.append(embed);
        relayout();
    }
}

void YXTray::destroyedClient(Window win) {
    MSG(("undock N=%d, win=%lX", fDocked.getCount(), win));
    for (IterType ec = fDocked.reverseIterator(); ++ec; ) {
        MSG(("win %lX, %d handle %lX", win, ec.where(), ec->handle()));
        if (ec->client_handle() == win) {
            MSG(("removing i=%d, win=%lX", ec.where(), win));
            ec.remove();
            relayout();
            break;
        }
    }
}

void YXTray::handleConfigureRequest(const XConfigureRequestEvent &configureRequest)
{
    MSG(("tray configureRequest w=%d h=%d internal=%s\n", configureRequest.width, configureRequest.height, fInternal ? "true" : "false"));
    bool changed = false;
    for (IterType ec = fDocked.iterator(); ++ec; ) {
        if (ec->client_handle() == configureRequest.window) {
            int ww = configureRequest.width;
            int hh = configureRequest.height;
            if (!fInternal) {
                /* scale down too big icons */
                getScaleSize(&ww, &hh);
            }
            if (ww != ec->width() || hh != ec->height())
                changed = true;
            ec->setSize(configureRequest.width, configureRequest.height);
        }
    }
    if (changed)
        relayout();
}

void YXTray::showClient(Window win, bool showClient) {
    for (IterType ec = fDocked.iterator(); ++ec; ) {
        if (ec->client_handle() == win) {
            ec->fVisible = showClient;
            if (showClient)
                ec->show();
            else
                ec->hide();
        }
    }
    relayout();
}

void YXTray::detachTray() {
    for (IterType ec = fDocked.iterator(); ++ec; ) {
        ec->detach();
    }
    fDocked.clear();
}


void YXTray::paint(Graphics &g, const YRect &/*r*/) {
    if (fInternal)
        return;
#ifdef CONFIG_TASKBAR
    g.setColor(getTaskBarBg());
#endif
    g.fillRect(0, 0, width(), height());
    if (trayDrawBevel && fDocked.getCount())
        g.draw3DRect(0, 0, width() - 1, height() - 1, false);
}

void YXTray::configure(const YRect &r) {
    YWindow::configure(r);
    relayout();
}

void YXTray::backgroundChanged() {
    if (fInternal)
        return;
#ifdef CONFIG_TASKBAR
    unsigned long bg = getTaskBarBg()->pixel();
    XSetWindowBackground(xapp->display(), handle(), bg);
#endif
    for (IterType ec = fDocked.iterator(); ++ec; ) {
#ifdef CONFIG_TASKBAR
        XSetWindowBackground(xapp->display(), ec->handle(), bg);
        XSetWindowBackground(xapp->display(), ec->client_handle(), bg);
	/* something is not clearing which background changes */
	XClearArea(xapp->display(), ec->client_handle(), 0, 0, 0, 0, True);
#endif
	ec->repaint();
    }
    relayout();
    repaint();
}

void YXTray::relayout() {
    int aw = 0;
    int h  = trayIconMaxHeight;
    if (!fInternal && trayDrawBevel)
        aw+=1;
    int cnt = 0;

    /*
       sanity check - remove already destroyed xwindows
    */
    for (IterType ec = fDocked.reverseIterator(); ++ec; ) {
        XWindowAttributes attributes;
        int status = XGetWindowAttributes(xapp->display(),
                                          ec->client()->handle(), &attributes);
        if (status == 0) {
            MSG(("relayout sanity check: removing %lX", ec->client()->handle()));
            ec.remove();
        }
    }

    for (IterType ec = fDocked.iterator(); ++ec; ) {
        if (!ec->fVisible)
            continue;
        cnt++;
        int eh(h), ew=ec->width(), ay(0);
        if (!fInternal) {
            ew = min(trayIconMaxWidth, ec->width());
            if (trayDrawBevel) {
                eh-=2; ay=1;
            }
        }
        ec->setGeometry(YRect(aw,ay,ew,eh));
        aw += ew;
    }
    if (!fInternal && trayDrawBevel)
        aw+=1;

    int w = aw;
    if (!fInternal) {
        if (w < 1)
            w = 1;
    } else {
        if (w < 2)
            w = 0;
    }
    if (cnt == 0) {
        hide();
        w = 0;
    } else {
        show();
    }
    MSG(("relayout %d %d : %d %d", w, h, width(), height()));
    if (w != width() || h != height()) {
        setSize(w, h);
        if (fNotifier)
            fNotifier->trayChanged();
    }
    for (IterType ec = fDocked.iterator(); ++ec; ) {
        if (ec->fVisible)
            ec->show();
    }

    MSG(("clients %d width: %d %d", fDocked.getCount(), width(), visible() ? 1 : 0));
}

bool YXTray::kdeRequestDock(Window win) {
    if (fDocked.getCount() == 0)
        return false;
    puts("trying to dock");
    char trayatom[64];
    sprintf(trayatom, "_NET_SYSTEM_TRAY_S%d", xapp->screen());
    Atom tray = XInternAtom(xapp->display(), trayatom, False);
    Atom opcode = XInternAtom(xapp->display(), "_NET_SYSTEM_TRAY_OPCODE", False);
    Window w = XGetSelectionOwner(xapp->display(), tray);

    if (w && w != handle()) {
        XClientMessageEvent xev;
        memset(&xev, 0, sizeof(xev));

        xev.type = ClientMessage;
        xev.window = w;
        xev.message_type = opcode; //_NET_SYSTEM_TRAY_OPCODE;
        xev.format = 32;
        xev.data.l[0] = CurrentTime;
        xev.data.l[1] = SYSTEM_TRAY_REQUEST_DOCK;
        xev.data.l[2] = win; //fTray2->handle();

        XSendEvent(xapp->display(), w, False, StructureNotifyMask, (XEvent *) &xev);
        return true;
    }
    return false;
}

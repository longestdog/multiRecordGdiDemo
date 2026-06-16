#include "WebViewWindow.h"

#include <QEvent>
#include <QQmlContext>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

WebViewWindow::WebViewWindow(const QString& title, const QUrl& url)
{
    setFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    setTitle(title);
    resize(480, 640);
    setColor(QColor(QStringLiteral("#0f0f0f")));
    setResizeMode(QQuickView::SizeRootObjectToView);

    rootContext()->setContextProperty(QStringLiteral("pageUrl"), url);
    rootContext()->setContextProperty(QStringLiteral("windowTitle"), title);
    setSource(QUrl(QStringLiteral("qrc:/qml/WebPage.qml")));
}

HWND WebViewWindow::nativeHandle() const
{
    auto* self = const_cast<WebViewWindow*>(this);
    if (!self->handle())
        self->create();
    return reinterpret_cast<HWND>(self->winId());
}

void WebViewWindow::placeRightOf(HWND anchor, int spacing)
{
    if (!anchor || !IsWindow(anchor))
        return;

    RECT rect{};
    GetWindowRect(anchor, &rect);
    const int x = rect.right + spacing;
    const int y = rect.top;
    SetWindowPos(nativeHandle(), HWND_TOP, x, y, width(), height(), SWP_SHOWWINDOW);
}

bool WebViewWindow::event(QEvent* event)
{
    if (event->type() == QEvent::Close) {
        emit closed();
    }
    return QQuickView::event(event);
}

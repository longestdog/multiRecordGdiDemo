#pragma once

#include <QQuickView>

class WebViewWindow : public QQuickView
{
    Q_OBJECT
public:
    explicit WebViewWindow(const QString& title, const QUrl& url);

    HWND nativeHandle() const;
    void placeRightOf(HWND anchor, int spacing);

signals:
    void closed();

protected:
    bool event(QEvent* event) override;
};

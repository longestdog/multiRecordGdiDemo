#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

class MainWindow : public QWidget
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    HWND nativeHandle() const;
    void setStatusText(const QString& text);
    void setCanvasInfo(const QSize& canvasSize, int windowCount, int rowCount, int pageCount, const QString& layoutSummary);
    void setRecording(bool recording);
    void setWebViewOpen(int childId, bool open);

signals:
    void startRecordRequested();
    void stopRecordRequested();
    void toggleWebViewRequested(int childId);

private:
    QLabel* statusLabel_ = nullptr;
    QLabel* canvasLabel_ = nullptr;
    QPushButton* recordBtn_ = nullptr;
    QPushButton* webViewBtns_[6] = {};
    bool recording_ = false;
};

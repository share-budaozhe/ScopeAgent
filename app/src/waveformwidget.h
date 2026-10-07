#pragma once
#include <QWidget>
#include <QTimer>
#include "deviceconfig.h"

// 示波器波形显示控件：网格 + 辉光轨迹 + 触发线 + 刻度标签
class WaveformWidget : public QWidget {
    Q_OBJECT
public:
    explicit WaveformWidget(QWidget* parent = nullptr);
    void setDriver(IDriver* driver);
    IDriver* driver() const { return m_driver; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void drawGrid(QPainter& p, const QRectF& r);
    void drawTrace(QPainter& p, const QRectF& r);
    void drawLabels(QPainter& p, const QRectF& r);

    IDriver* m_driver = nullptr;
    QTimer m_timer;
    double m_triggerPos = 0.5;        // 触发点水平位置（0~1，屏幕比例）
    bool m_draggingTrigger = false;   // 正在拖动触发点
};

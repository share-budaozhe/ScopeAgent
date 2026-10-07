#include "waveformwidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <cmath>

// 配色（示波器暗色风格）
static const QColor BG(10, 14, 20);
static const QColor GRID_MAJOR(40, 52, 66);
static const QColor GRID_MINOR(24, 32, 42);
static const QColor AXIS(70, 90, 110);
static const QColor TRACE(0, 229, 195);        // 青绿辉光
static const QColor TRACE_GLOW(0, 229, 195, 60);
static const QColor TRIGGER(255, 170, 40);      // 橙色触发线
static const QColor TEXT(150, 180, 200);

WaveformWidget::WaveformWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumSize(360, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);   // 悬停时也能收到移动事件，用于靠近触发点显示手型
    m_timer.setInterval(33);  // ~30 FPS
    connect(&m_timer, &QTimer::timeout, this, [this]() { update(); });
    m_timer.start();
}

void WaveformWidget::setDriver(IDriver* driver) {
    m_driver = driver;
    update();
}

void WaveformWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    update();
}

void WaveformWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        double trigX = rect().left() + m_triggerPos * rect().width();
        if (std::abs(event->pos().x() - trigX) <= 10.0) {
            m_draggingTrigger = true;
            setCursor(Qt::SizeHorCursor);
        }
    }
    QWidget::mousePressEvent(event);
}

void WaveformWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_draggingTrigger) {
        double w = rect().width();
        if (w > 1.0) {
            m_triggerPos = qBound(0.0, (event->pos().x() - rect().left()) / w, 1.0);
            update();
        }
    } else {
        double trigX = rect().left() + m_triggerPos * rect().width();
        bool near = std::abs(event->pos().x() - trigX) <= 10.0;
        setCursor(near ? Qt::SizeHorCursor : Qt::ArrowCursor);
    }
    QWidget::mouseMoveEvent(event);
}

void WaveformWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_draggingTrigger) {
        m_draggingTrigger = false;
        unsetCursor();
    }
    QWidget::mouseReleaseEvent(event);
}

void WaveformWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    QRectF r = rect().adjusted(1, 1, -1, -1);
    p.fillRect(rect(), BG);

    drawGrid(p, r);
    drawTrace(p, r);
    drawLabels(p, r);
}

void WaveformWidget::drawGrid(QPainter& p, const QRectF& r) {
    const int cols = 10, rows = 8, minor = 5;
    const double cw = r.width() / cols;
    const double ch = r.height() / rows;

    // 次刻度
    QPen minorPen(GRID_MINOR, 1, Qt::DotLine);
    p.setPen(minorPen);
    for (int i = 0; i <= cols * minor; ++i) {
        double x = r.left() + i * cw / minor;
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    }
    for (int j = 0; j <= rows * minor; ++j) {
        double y = r.top() + j * ch / minor;
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    }

    // 主网格
    QPen majorPen(GRID_MAJOR, 1);
    p.setPen(majorPen);
    for (int i = 0; i <= cols; ++i) {
        double x = r.left() + i * cw;
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    }
    for (int j = 0; j <= rows; ++j) {
        double y = r.top() + j * ch;
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    }

    // 中心轴
    QPen axisPen(AXIS, 1.4);
    p.setPen(axisPen);
    double cx = r.left() + r.width() / 2.0;
    double cy = r.top() + r.height() / 2.0;
    p.drawLine(QPointF(cx, r.top()), QPointF(cx, r.bottom()));
    p.drawLine(QPointF(r.left(), cy), QPointF(r.right(), cy));
}

void WaveformWidget::drawTrace(QPainter& p, const QRectF& r) {
    if (!m_driver) return;
    DeviceConfig cfg = m_driver->state();
    const int rows = 8;
    const double ch = r.height() / rows;
    const double cy = r.top() + r.height() / 2.0;

    int points = qMax(256, (int)r.width());
    QVector<double> data = m_driver->acquire(points, m_triggerPos);
    if (data.size() != points) return;

    QPainterPath path;
    for (int i = 0; i < points; ++i) {
        double x = r.left() + (double)i / (points - 1) * r.width();
        double y = cy - (data[i] / cfg.voltsPerDiv) * ch;
        if (i == 0) path.moveTo(x, y);
        else path.lineTo(x, y);
    }

    // 辉光层
    QPen glow(TRACE_GLOW, 5.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(glow);
    p.drawPath(path);
    // 亮线层
    QPen line(TRACE, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(line);
    p.drawPath(path);

    // ---- 触发标记（画在轨迹之上，保证可见） ----
    double trigY = cy - (cfg.triggerLevelV / cfg.voltsPerDiv) * ch;
    double trigX = r.left() + m_triggerPos * r.width();
    bool trigInView = (trigY >= r.top() - 2 && trigY <= r.bottom() + 2);

    // 触发电平虚线（水平）
    if (trigInView) {
        QPen tp(TRIGGER, 1.2, Qt::DashLine);
        p.setPen(tp);
        p.drawLine(QPointF(r.left(), trigY), QPointF(r.right(), trigY));
    }

    // 触发位置虚线（垂直，可左右拖动）
    QPen vp(TRIGGER, 1.0, Qt::DashLine);
    p.setPen(vp);
    p.drawLine(QPointF(trigX, r.top()), QPointF(trigX, r.bottom()));

    // 顶部拖动把手（▼ 指向触发位置）
    QPainterPath handle;
    const double hh = 7.0, hw = 5.0;
    handle.moveTo(trigX, r.top());
    handle.lineTo(trigX - hw, r.top() + hh);
    handle.lineTo(trigX + hw, r.top() + hh);
    handle.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(TRIGGER);
    p.drawPath(handle);

    // 触发点斜率箭头（上升▲ / 下降▼，画在电平线×位置线交点处）
    if (trigInView) {
        double dir = (cfg.triggerSlope == "rising") ? 1.0 : -1.0;
        const double sh = 5.0, sw = 5.0;
        QPainterPath slope;
        slope.moveTo(trigX, trigY - dir * sh);
        slope.lineTo(trigX - sw, trigY + dir * sh);
        slope.lineTo(trigX + sw, trigY + dir * sh);
        slope.closeSubpath();
        p.setBrush(TRIGGER);
        p.drawPath(slope);
    }
}

void WaveformWidget::drawLabels(QPainter& p, const QRectF& r) {
    if (!m_driver) return;
    DeviceConfig cfg = m_driver->state();

    QFont f = p.font();
    f.setPointSizeF(9.5);
    f.setFamily("Consolas");
    p.setFont(f);
    p.setPen(TEXT);

    auto fmt = [](double v) {
        QString s;
        if (std::abs(v) >= 1e6) s = QString::number(v / 1e6, 'f', 2) + " M";
        else if (std::abs(v) >= 1e3) s = QString::number(v / 1e3, 'f', 2) + " k";
        else if (std::abs(v) < 1e-3 && std::abs(v) >= 1e-6) s = QString::number(v * 1e6, 'f', 2) + " u";
        else if (std::abs(v) < 1e-6 && std::abs(v) >= 1e-9) s = QString::number(v * 1e9, 'f', 2) + " n";
        else s = QString::number(v, 'f', 4);
        return s;
    };

    p.drawText(QRectF(r.left() + 10, r.top() + 8, 500, 20), Qt::AlignLeft | Qt::AlignTop,
               QString("%1  %2 Hz  %3 Vpp")
                   .arg(cfg.waveform).arg(fmt(cfg.frequencyHz)).arg(fmt(cfg.amplitudeVpp)));

    p.drawText(QRectF(r.left() + 10, r.bottom() - 28, 500, 20), Qt::AlignLeft | Qt::AlignBottom,
               QString("时基 %1 s/div    垂直 %2 V/div")
                   .arg(fmt(cfg.secPerDiv)).arg(fmt(cfg.voltsPerDiv)));

    p.drawText(QRectF(r.right() - 240, r.top() + 8, 230, 20), Qt::AlignRight | Qt::AlignTop,
               QString("触发 %1 V (%2)")
                   .arg(fmt(cfg.triggerLevelV)).arg(cfg.triggerSlope == "rising" ? "上升" : "下降"));

    if (m_driver && !m_driver->connected()) {
        p.setPen(QColor(255, 90, 90));
        p.drawText(QRectF(r.left() + 10, r.top() + 8, 500, 20), Qt::AlignLeft | Qt::AlignTop,
                   QString("未连接"));
    }
}

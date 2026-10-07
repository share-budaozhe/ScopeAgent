#pragma once
#include "deviceconfig.h"

// 虚拟示波器驱动：按配置实时合成波形，行为模拟真实示波器
class VirtualScopeDriver : public IDriver {
public:
    VirtualScopeDriver();

    QString id() const override { return "virtual"; }
    QString name() const override { return "虚拟示波器 VirtualScope-1000"; }
    DeviceConfig state() const override { return m_cfg; }
    bool apply(const DeviceConfig& c) override;
    QVector<double> acquire(int points, double triggerPos = 0.0) const override;
    QStringList capabilities() const override;
    bool connected() const override { return true; }

private:
    double signalCore(double t) const;          // 不含噪声的纯波形
    double findTriggerPhase() const;            // 触发相位对齐，使显示稳定
    DeviceConfig m_cfg;
};

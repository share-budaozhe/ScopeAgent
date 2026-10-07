#pragma once
#include "deviceconfig.h"
#include <QStringList>

// SCPI/VISA 驱动：通过 visa64.dll / visa32.dll 动态加载 VISA，
// 可真实连接仪器（*IDN? 查询 + SCPI 命令收发）。未连仪器时优雅降级。
class ScpiDriver : public IDriver {
public:
    ScpiDriver();
    ~ScpiDriver() override;

    QString id() const override { return "scpi"; }
    QString name() const override { return "SCPI 示波器 (VISA)"; }
    DeviceConfig state() const override { return m_cfg; }
    bool apply(const DeviceConfig& c) override;
    QVector<double> acquire(int points, double triggerPos = 0.0) const override;
    QStringList capabilities() const override;
    bool connected() const override { return m_connected; }

    bool connectTo(const QString& addr);   // 真实连接（viOpen + *IDN?）
    void disconnect();

    void setVisaAddress(const QString& addr) { m_visaAddress = addr; }
    QString visaAddress() const { return m_visaAddress; }
    QString idn() const { return m_idn; }
    QString lastError() const { return m_lastError; }
    QStringList lastCommands() const { return m_lastCommands; }

private:
    struct VisaImpl;
    VisaImpl* m_visa = nullptr;
    QString sendScpi(const QString& cmd) const;   // 写命令，返回响应
    QString queryScpi(const QString& cmd) const;  // 写 + 读（查询）

    DeviceConfig m_cfg;
    QString m_visaAddress = "TCPIP0::192.168.1.100::inst0::INSTR";
    QStringList m_lastCommands;
    QString m_idn;
    QString m_lastError;
    bool m_connected = false;
};

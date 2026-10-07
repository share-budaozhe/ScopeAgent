#pragma once
#include <QString>
#include <QJsonObject>
#include <QVector>

// 设备配置契约（与 agent/schema.py 保持一致）
struct DeviceConfig {
    DeviceConfig() = default;

    QString waveform = "sine";
    double frequencyHz = 1000.0;
    double amplitudeVpp = 3.0;
    double dcOffsetV = 0.0;
    double secPerDiv = 0.0005;
    double voltsPerDiv = 1.0;
    double triggerLevelV = 0.0;
    QString triggerSlope = "rising";
    double noiseLevel = 0.02;

    static DeviceConfig defaults();

    // 从 JSON 解析（缺失字段继承 base），随后做范围钳制
    static DeviceConfig fromJson(const QJsonObject& o);
    static DeviceConfig fromJson(const QJsonObject& o, const DeviceConfig& base);
    QJsonObject toJson() const;

    // 范围钳制（双层校验的第二层，Qt 侧兜底）
    static DeviceConfig clamped(const DeviceConfig& c);

    QString summary() const;
    static QStringList waveforms();
    static QStringList triggerSlopes();
};

// 驱动抽象接口
class IDriver {
public:
    virtual ~IDriver() = default;
    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual DeviceConfig state() const = 0;
    virtual bool apply(const DeviceConfig& c) = 0;   // 内部会钳制
    virtual QVector<double> acquire(int points, double triggerPos = 0.0) const = 0;
    virtual QStringList capabilities() const = 0;
    virtual bool connected() const = 0;
};

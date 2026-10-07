#include "deviceconfig.h"
#include <QJsonValue>
#include <cmath>

static double clampd(double v, double lo, double hi, double def) {
    if (std::isnan(v) || std::isinf(v)) return def;
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static QStringList WAVEFORMS = {"sine", "square", "triangle", "sawtooth", "noise"};
static QStringList SLOPES = {"rising", "falling"};

QStringList DeviceConfig::waveforms() { return WAVEFORMS; }
QStringList DeviceConfig::triggerSlopes() { return SLOPES; }

DeviceConfig DeviceConfig::defaults() {
    return DeviceConfig();
}

DeviceConfig DeviceConfig::clamped(const DeviceConfig& c) {
    DeviceConfig o = c;
    if (!WAVEFORMS.contains(o.waveform)) o.waveform = "sine";
    if (!SLOPES.contains(o.triggerSlope)) o.triggerSlope = "rising";
    o.frequencyHz = clampd(o.frequencyHz, 1.0, 10000000.0, 1000.0);
    o.amplitudeVpp = clampd(o.amplitudeVpp, 0.01, 100.0, 3.0);
    o.dcOffsetV = clampd(o.dcOffsetV, -100.0, 100.0, 0.0);
    o.secPerDiv = clampd(o.secPerDiv, 0.0000005, 1.0, 0.0005);
    o.voltsPerDiv = clampd(o.voltsPerDiv, 0.005, 20.0, 1.0);
    o.triggerLevelV = clampd(o.triggerLevelV, -100.0, 100.0, 0.0);
    o.noiseLevel = clampd(o.noiseLevel, 0.0, 1.0, 0.02);
    return o;
}

DeviceConfig DeviceConfig::fromJson(const QJsonObject& o) {
    return fromJson(o, DeviceConfig());
}

DeviceConfig DeviceConfig::fromJson(const QJsonObject& o, const DeviceConfig& base) {
    DeviceConfig c = base;
    if (o.contains("waveform")) c.waveform = o.value("waveform").toString();
    if (o.contains("frequency_hz")) c.frequencyHz = o.value("frequency_hz").toDouble();
    if (o.contains("amplitude_vpp")) c.amplitudeVpp = o.value("amplitude_vpp").toDouble();
    if (o.contains("dc_offset_v")) c.dcOffsetV = o.value("dc_offset_v").toDouble();
    if (o.contains("sec_per_div")) c.secPerDiv = o.value("sec_per_div").toDouble();
    if (o.contains("volts_per_div")) c.voltsPerDiv = o.value("volts_per_div").toDouble();
    if (o.contains("trigger_level_v")) c.triggerLevelV = o.value("trigger_level_v").toDouble();
    if (o.contains("trigger_slope")) c.triggerSlope = o.value("trigger_slope").toString();
    if (o.contains("noise_level")) c.noiseLevel = o.value("noise_level").toDouble();
    return clamped(c);
}

QJsonObject DeviceConfig::toJson() const {
    QJsonObject o;
    o.insert("waveform", waveform);
    o.insert("frequency_hz", frequencyHz);
    o.insert("amplitude_vpp", amplitudeVpp);
    o.insert("dc_offset_v", dcOffsetV);
    o.insert("sec_per_div", secPerDiv);
    o.insert("volts_per_div", voltsPerDiv);
    o.insert("trigger_level_v", triggerLevelV);
    o.insert("trigger_slope", triggerSlope);
    o.insert("noise_level", noiseLevel);
    return o;
}

QString DeviceConfig::summary() const {
    return QString("波形=%1, 频率=%2 Hz, 峰峰值=%3 V, 直流=%4 V, 时基=%5 s/div, "
                   "垂直=%6 V/div, 触发=%7 V(%8), 噪声=%9")
        .arg(waveform)
        .arg(frequencyHz, 0, 'g', 6)
        .arg(amplitudeVpp, 0, 'g', 6)
        .arg(dcOffsetV, 0, 'g', 6)
        .arg(secPerDiv, 0, 'g', 6)
        .arg(voltsPerDiv, 0, 'g', 6)
        .arg(triggerLevelV, 0, 'g', 6)
        .arg(triggerSlope)
        .arg(noiseLevel, 0, 'g', 3);
}

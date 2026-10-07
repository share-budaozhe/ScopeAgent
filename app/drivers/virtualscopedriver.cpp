#include "virtualscopedriver.h"
#include <cmath>
#include <random>
#include <chrono>

static const double PI = 3.14159265358979323846;

VirtualScopeDriver::VirtualScopeDriver() {
    m_cfg = DeviceConfig::defaults();
}

bool VirtualScopeDriver::apply(const DeviceConfig& c) {
    m_cfg = DeviceConfig::clamped(c);
    return true;
}

double VirtualScopeDriver::signalCore(double t) const {
    double A = m_cfg.amplitudeVpp / 2.0;
    double off = m_cfg.dcOffsetV;
    double f = m_cfg.frequencyHz;
    double ph = 2.0 * PI * f * t;
    double v = 0.0;

    if (m_cfg.waveform == "sine") {
        v = A * std::sin(ph);
    } else if (m_cfg.waveform == "square") {
        v = A * (std::sin(ph) >= 0.0 ? 1.0 : -1.0);
    } else if (m_cfg.waveform == "triangle") {
        v = A * (2.0 / PI) * std::asin(std::sin(ph));
    } else if (m_cfg.waveform == "sawtooth") {
        v = A * (2.0 * (f * t - std::floor(f * t + 0.5)));
    } else { // noise
        v = 0.0;
    }
    return off + v;
}

double VirtualScopeDriver::findTriggerPhase() const {
    // 在 [0, period) 内找到满足触发斜率、穿越触发电平的相位偏移；找不到返回 NaN（未触发）
    double f = m_cfg.frequencyHz;
    if (f <= 0.0) return std::nan("");
    double period = 1.0 / f;
    double trig = m_cfg.triggerLevelV;
    bool rising = (m_cfg.triggerSlope == "rising");
    const int N = 4000;
    double dt = period / N;
    for (int i = 0; i < N; ++i) {
        double t = i * dt;
        double v1 = signalCore(t);
        double v2 = signalCore(t + dt);
        if (rising) {
            if (v1 <= trig && v2 > trig) return t;
        } else {
            if (v1 >= trig && v2 < trig) return t;
        }
    }
    return std::nan("");
}

QVector<double> VirtualScopeDriver::acquire(int points, double triggerPos) const {
    QVector<double> out(points);
    if (points <= 0) return out;

    double span = m_cfg.secPerDiv * 10.0;   // 屏幕横向 10 格
    double dt = span / points;
    double t0 = findTriggerPhase();
    if (std::isnan(t0)) {
        // 未触发：触发电平超出信号范围，自由运行（相位随时间漂移，模拟未锁定）
        double period = 1.0 / m_cfg.frequencyHz;
        double tNow = std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        t0 = std::fmod(tNow, period);
    }
    // 触发点水平位置：把穿越点放到屏幕 triggerPos 比例处（0=最左，1=最右）
    t0 -= triggerPos * span;

    // 动态种子的伪随机噪声：每帧变化，呈现真实示波器的“活”的刷新效果
    std::mt19937 rng((unsigned)std::chrono::steady_clock::now().time_since_epoch().count());
    std::normal_distribution<double> gauss(0.0, 1.0);
    double noiseAmp = (m_cfg.amplitudeVpp / 2.0) * m_cfg.noiseLevel;

    for (int i = 0; i < points; ++i) {
        double t = t0 + i * dt;
        double v = signalCore(t);
        if (noiseAmp > 0.0) {
            v += noiseAmp * gauss(rng);
        }
        out[i] = v;
    }
    return out;
}

QStringList VirtualScopeDriver::capabilities() const {
    return {
        "带宽 1 GHz，最高采样率 10 GS/s",
        "波形：sine/square/triangle/sawtooth/noise",
        "频率范围 1 Hz ~ 10 MHz",
        "幅值范围 0.01 ~ 100 Vpp",
        "时基 500 ns/div ~ 1 s/div",
        "垂直 5 mV/div ~ 20 V/div",
        "触发斜率 rising/falling",
    };
}

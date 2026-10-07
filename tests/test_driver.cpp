// 驱动单元测试（非 GUI，验证波形合成/触发/钳制）
#include <cstdio>
#include <cmath>
#include "virtualscopedriver.h"
#include "deviceconfig.h"

static int failures = 0;
static void check(bool ok, const char* msg) {
    printf("%s %s\n", ok ? "[PASS]" : "[FAIL]", msg);
    if (!ok) failures++;
}

int main() {
    VirtualScopeDriver d;

    // 1. 正弦波：3Vpp 应落在 [-1.5, 1.5]
    DeviceConfig c; c.waveform = "sine"; c.frequencyHz = 1000; c.amplitudeVpp = 3.0;
    c.secPerDiv = 0.0002; c.voltsPerDiv = 1.0; c.noiseLevel = 0.0;
    d.apply(c);
    auto s = d.acquire(1000);
    double mn = 1e9, mx = -1e9;
    for (double v : s) { if (v < mn) mn = v; if (v > mx) mx = v; }
    check(std::fabs(mn + 1.5) < 0.01 && std::fabs(mx - 1.5) < 0.01, "正弦波幅值范围 [-1.5, 1.5]");

    // 2. 触发相位：rising 且电平 0.5V，首个采样应接近 0.5（从穿越点开始）
    DeviceConfig c2; c2.frequencyHz = 1000; c2.amplitudeVpp = 2.0; c2.triggerLevelV = 0.5;
    c2.triggerSlope = "rising"; c2.secPerDiv = 0.001; c2.noiseLevel = 0.0;
    d.apply(c2);
    auto s2 = d.acquire(1000);
    check(std::fabs(s2[0] - 0.5) < 0.02, "触发相位对齐（rising @0.5V）");

    // 3. 方波：取值应为 ±A（A=1.0）
    DeviceConfig c3; c3.waveform = "square"; c3.amplitudeVpp = 2.0; c3.frequencyHz = 1000;
    c3.secPerDiv = 0.001; c3.noiseLevel = 0.0;
    d.apply(c3);
    auto s3 = d.acquire(100);
    bool squareOk = true;
    for (double v : s3) { if (std::fabs(std::fabs(v) - 1.0) > 1e-6) { squareOk = false; break; } }
    check(squareOk, "方波取值 ±1.0");

    // 4. 钳制：非法波形与越界频率
    DeviceConfig bad; bad.waveform = "bogus"; bad.frequencyHz = 999999999; bad.amplitudeVpp = -5;
    DeviceConfig cc = DeviceConfig::clamped(bad);
    check(cc.waveform == "sine" && cc.frequencyHz == 10000000.0 && cc.amplitudeVpp == 0.01,
          "配置钳制（波形/频率/幅值）");

    // 5. JSON 往返
    DeviceConfig r = DeviceConfig::fromJson(c.toJson());
    check(r.frequencyHz == c.frequencyHz && r.waveform == c.waveform, "JSON 序列化往返");

    printf("\n%s (%d failures)\n", failures == 0 ? "ALL DRIVER TESTS PASSED" : "TESTS FAILED", failures);
    return failures == 0 ? 0 : 1;
}

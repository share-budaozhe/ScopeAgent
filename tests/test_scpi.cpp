// SCPI/VISA 驱动测试：验证 VISA 库动态加载、无效地址优雅失败、命令生成
#include <cstdio>
#include "scpidriver.h"

static int failures = 0;
static void check(bool ok, const char* msg) {
    printf("%s %s\n", ok ? "[PASS]" : "[FAIL]", msg);
    if (!ok) failures++;
}

int main() {
    ScpiDriver d;

    // 1. 连接一个不存在的地址：应优雅失败（不崩溃），并给出错误信息
    bool ok = d.connectTo("TCPIP0::192.168.1.250::inst0::INSTR");
    check(ok == false && d.connected() == false, "无效地址连接优雅失败");
    printf("      [info] lastError = %s\n", d.lastError().toUtf8().constData());
    check(!d.lastError().isEmpty(), "失败时给出错误信息");

    // 2. apply 生成 SCPI 命令
    DeviceConfig c; c.voltsPerDiv = 0.5; c.secPerDiv = 0.0001; c.triggerLevelV = 0.5; c.triggerSlope = "falling";
    d.apply(c);
    check(d.lastCommands().size() == 6, "apply 生成 6 条 SCPI 命令");
    bool hasSlope = false;
    for (const QString& cmd : d.lastCommands()) {
        if (cmd.contains("NEGative")) hasSlope = true;
    }
    check(hasSlope, "下降沿映射为 NEGative");

    // 3. capabilities 反映状态
    check(d.capabilities().size() >= 3, "capabilities 包含状态信息");

    d.disconnect();
    check(d.connected() == false, "disconnect 后未连接");

    printf("\n%s (%d failures)\n", failures == 0 ? "ALL SCPI TESTS PASSED" : "TESTS FAILED", failures);
    return failures == 0 ? 0 : 1;
}

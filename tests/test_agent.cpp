// Agent 集成测试：验证 AgentClient 完整链路（启动 sidecar → 自然语言 → 配置 → 驱动应用）
#include <QCoreApplication>
#include <QTimer>
#include <cstdio>
#include "agentclient.h"
#include "virtualscopedriver.h"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    AgentClient client;
    VirtualScopeDriver driver;

    // 退出前务必停止 sidecar，避免遗留 python 子进程
    auto cleanup = [&](int code) {
        client.stop();
        QCoreApplication::exit(code);
    };

    QObject::connect(&client, &AgentClient::configured, [&](const QJsonObject& result) {
        DeviceConfig cfg = DeviceConfig::fromJson(result.value("config").toObject(), driver.state());
        driver.apply(cfg);
        printf("[配置] %s\n", cfg.summary().toUtf8().constData());
        QString rationale = result.value("rationale").toString();
        printf("[理由] %.200s\n", rationale.toUtf8().constData());
        printf("RESULT_OK\n");
        fflush(stdout);
        cleanup(0);
    });
    QObject::connect(&client, &AgentClient::configureError, [&](const QString& msg) {
        printf("RESULT_ERROR: %s\n", msg.toUtf8().constData());
        fflush(stdout);
        cleanup(2);
    });
    QObject::connect(&client, &AgentClient::statusChanged, [&](const QString& t, bool ready) {
        printf("[状态] %s\n", t.toUtf8().constData());
        fflush(stdout);
        if (ready) {
            client.configure("显示一个 2kHz、2V 峰峰值的方波，时基调到能看到大约两个周期",
                             driver.state().toJson());
        }
    });

    client.start();
    QTimer::singleShot(90000, [&]() { printf("RESULT_TIMEOUT\n"); fflush(stdout); cleanup(1); });
    return app.exec();
}

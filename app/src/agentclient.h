#pragma once
#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QTimer>

// Agent Sidecar 客户端：管理 Python 子进程 + HTTP 通信（全程异步，不阻塞 UI）
class AgentClient : public QObject {
    Q_OBJECT
public:
    explicit AgentClient(QObject* parent = nullptr);
    ~AgentClient() override;

    void start();                                   // 启动 sidecar 并等待就绪
    void stop();                                    // 关闭 sidecar
    bool isReady() const { return m_ready; }
    QString model() const { return m_model; }

    // 自然语言 → 配置（异步）
    void configure(const QString& intent, const QJsonObject& state);

    // 知识库（RAG）操作（异步，离线可用）
    void importKnowledge(const QString& path);
    void removeKnowledge(const QString& id);
    void listKnowledge();

signals:
    void statusChanged(const QString& text, bool ready);   // 状态文本 + 是否就绪
    void configureStarted();
    void configured(const QJsonObject& result);            // 成功
    void configureError(const QString& message);           // 失败
    void knowledgeImported(const QJsonObject& result);
    void knowledgeListed(const QJsonArray& documents);
    void knowledgeRemoved(const QJsonObject& result);
    void knowledgeError(const QString& message);

private:
    QString findPython() const;
    QString findAgentDir() const;
    void pollHealth();

    QProcess* m_process = nullptr;
    QNetworkAccessManager m_nam;
    QTimer m_healthTimer;
    QString m_host = "127.0.0.1";
    int m_port = 8765;
    QString m_baseUrl;
    bool m_ready = false;
    QString m_model;
    int m_healthTries = 0;
    bool m_configurePending = false;
    bool m_kbListed = false;
};

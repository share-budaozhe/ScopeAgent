#include "agentclient.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonValue>
#include <QJsonArray>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

AgentClient::AgentClient(QObject* parent) : QObject(parent) {
    m_baseUrl = QString("http://%1:%2").arg(m_host).arg(m_port);
    m_healthTimer.setInterval(400);
    connect(&m_healthTimer, &QTimer::timeout, this, &AgentClient::pollHealth);
}

AgentClient::~AgentClient() {
    stop();
}

QString AgentClient::findPython() const {
    QStringList candidates = {"python", "python3", "py"};
    for (const QString& c : candidates) {
        QString path = QStandardPaths::findExecutable(c);
        if (!path.isEmpty()) return path;
    }
    return QString();
}

QString AgentClient::findAgentDir() const {
    QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates = {
        appDir + "/agent",
        appDir + "/../agent",
        appDir + "/../../agent",
        QDir::currentPath() + "/agent",
    };
    for (const QString& d : candidates) {
        if (QFileInfo::exists(d + "/server.py")) return QDir(d).absolutePath();
    }
    return QString();
}

void AgentClient::start() {
    if (m_process) return;

    QString agentDir = findAgentDir();
    if (agentDir.isEmpty()) {
        emit statusChanged("未找到 agent 目录，请检查 sidecar 文件", false);
        return;
    }

    // 优先使用打包好的独立 exe（无需 Python），否则回退 python server.py
    QString sidecarExe = agentDir + "/scopeagent-sidecar.exe";
    QString program;
    QStringList args;
    if (QFileInfo::exists(sidecarExe)) {
        program = sidecarExe;
        args << "--port" << QString::number(m_port);
    } else {
        program = findPython();
        if (program.isEmpty()) {
            emit statusChanged("未找到 Python（且无 sidecar 可执行文件）", false);
            return;
        }
        args << "-u" << "server.py" << "--port" << QString::number(m_port);
    }

    m_process = new QProcess(this);
    m_process->setWorkingDirectory(agentDir);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
    // 隐藏子进程控制台窗口（对 python 与打包 exe 均生效）
    m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) {
        a->flags |= CREATE_NO_WINDOW;
    });
#endif
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        QByteArray d = m_process->readAllStandardOutput();
        Q_UNUSED(d);
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
        m_ready = false;
        m_healthTimer.stop();
        emit statusChanged("Agent 已退出", false);
        m_process->deleteLater();
        m_process = nullptr;
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            m_healthTimer.stop();
            emit statusChanged("Agent 启动失败：无法运行 sidecar", false);
        }
    });

    emit statusChanged("正在启动 Agent sidecar…", false);
    m_process->start(program, args);
    m_healthTries = 0;
    m_healthTimer.start();
}

void AgentClient::stop() {
    m_healthTimer.stop();
    if (m_process) {
        m_process->kill();
        m_process->waitForFinished(2000);
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_ready = false;
}

void AgentClient::pollHealth() {
    if (m_ready) { m_healthTimer.stop(); return; }
    if (++m_healthTries > 30) {   // ~12s 超时
        m_healthTimer.stop();
        emit statusChanged("Agent 启动超时", false);
        return;
    }
    QNetworkRequest req(QUrl(m_baseUrl + "/health"));
    QNetworkReply* reply = m_nam.get(req);
    // Qt 5.12 无 setTransferTimeout，用单次定时器兜底中止
    QTimer* abortTimer = new QTimer(reply);
    abortTimer->setSingleShot(true);
    connect(abortTimer, &QTimer::timeout, reply, &QNetworkReply::abort);
    abortTimer->start(1000);
    connect(reply, &QNetworkReply::finished, this, [this, reply, abortTimer]() {
        abortTimer->stop();
        reply->deleteLater();
        if (m_ready) return;
        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
            m_model = obj.value("model").toString();
            bool ready = obj.value("ready").toBool();
            if (ready) {
                m_ready = true;
                m_healthTimer.stop();
                emit statusChanged(QString("Agent 在线（%1）").arg(m_model), true);
            } else {
                emit statusChanged("Agent 未配置 API Key", false);
            }
            // 服务端已可达（无论是否配 key），拉取一次知识库列表
            if (!m_kbListed) {
                m_kbListed = true;
                listKnowledge();
            }
        }
    });
}

void AgentClient::configure(const QString& intent, const QJsonObject& state) {
    if (!m_ready) {
        emit configureError("Agent 尚未就绪");
        return;
    }
    QJsonObject body;
    body.insert("intent", intent);
    body.insert("state", state);

    QNetworkRequest req(QUrl(m_baseUrl + "/v1/configure"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    emit configureStarted();
    QNetworkReply* reply = m_nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        QByteArray data = reply->readAll();
        QJsonObject obj = QJsonDocument::fromJson(data).object();
        if (reply->error() != QNetworkReply::NoError || !obj.value("ok").toBool()) {
            QString err = obj.value("error").toString();
            if (err.isEmpty()) err = reply->errorString();
            emit configureError(err);
            return;
        }
        emit configured(obj);
    });
}

void AgentClient::importKnowledge(const QString& path) {
    QJsonObject body;
    body.insert("path", path);
    QNetworkRequest req(QUrl(m_baseUrl + "/v1/kb/import"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = m_nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        QByteArray data = reply->readAll();
        QJsonObject obj = QJsonDocument::fromJson(data).object();
        if (reply->error() != QNetworkReply::NoError || !obj.value("ok").toBool()) {
            QString err = obj.value("error").toString();
            if (err.isEmpty()) {
                QJsonArray errs = obj.value("errors").toArray();
                if (!errs.isEmpty())
                    err = errs.first().toObject().value("error").toString();
            }
            if (err.isEmpty()) err = reply->errorString();
            emit knowledgeError(err);
            return;
        }
        emit knowledgeImported(obj);
    });
}

void AgentClient::removeKnowledge(const QString& id) {
    QJsonObject body;
    body.insert("id", id);
    QNetworkRequest req(QUrl(m_baseUrl + "/v1/kb/remove"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = m_nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        QByteArray data = reply->readAll();
        QJsonObject obj = QJsonDocument::fromJson(data).object();
        if (reply->error() != QNetworkReply::NoError || !obj.value("ok").toBool()) {
            QString err = obj.value("error").toString();
            if (err.isEmpty()) err = reply->errorString();
            emit knowledgeError(err);
            return;
        }
        emit knowledgeRemoved(obj);
    });
}

void AgentClient::listKnowledge() {
    QNetworkRequest req(QUrl(m_baseUrl + "/v1/kb/list"));
    QNetworkReply* reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        QByteArray data = reply->readAll();
        QJsonObject obj = QJsonDocument::fromJson(data).object();
        if (reply->error() != QNetworkReply::NoError || !obj.value("ok").toBool()) {
            QString err = obj.value("error").toString();
            if (err.isEmpty()) err = reply->errorString();
            emit knowledgeError(err);
            return;
        }
        emit knowledgeListed(obj.value("documents").toArray());
    });
}

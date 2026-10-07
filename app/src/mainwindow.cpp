#include "mainwindow.h"
#include "waveformwidget.h"
#include "agentclient.h"
#include "virtualscopedriver.h"
#include "scpidriver.h"

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QTextEdit>
#include <QPushButton>
#include <QListWidget>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QScrollArea>
#include <QFrame>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTimer>
#include <cmath>

static QString fmt(double v) {
    if (std::abs(v) >= 1e6) return QString::number(v / 1e6, 'f', 3) + " M";
    if (std::abs(v) >= 1e3) return QString::number(v / 1e3, 'f', 3) + " k";
    if (std::abs(v) < 1e-3 && std::abs(v) >= 1e-6) return QString::number(v * 1e6, 'f', 3) + " u";
    if (std::abs(v) < 1e-6 && std::abs(v) >= 1e-9) return QString::number(v * 1e9, 'f', 3) + " n";
    return QString::number(v, 'f', 4);
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    m_virtualDriver = new VirtualScopeDriver();
    m_scpiDriver = new ScpiDriver();
    m_currentDriver = m_virtualDriver;
    m_agent = new AgentClient(this);

    buildUi();
    applyStyle();

    connect(m_agent, &AgentClient::statusChanged, this, [this](const QString& text, bool ready) {
        QString color = ready ? "#3ddc84" : "#f0a13a";
        m_statusLabel->setText(QString("<span style='color:%1'>●</span> %2").arg(color, text));
        if (!ready) appendChat("[Agent] " + text, "#f0a13a");
    });
    connect(m_agent, &AgentClient::configureStarted, this, [this]() {
        m_sendButton->setEnabled(false);
        m_intentEdit->setEnabled(false);
        m_statusLabel->setText("<span style='color:#f0a13a'>●</span> Agent 推理中…");
        appendChat("你：" + m_intentEdit->text(), "#8fb4d8");
    });
    connect(m_agent, &AgentClient::configured, this, [this](const QJsonObject& result) {
        m_sendButton->setEnabled(true);
        m_intentEdit->setEnabled(true);
        m_statusLabel->setText(QString("<span style='color:#3ddc84'>●</span> Agent 在线（%1）")
                                   .arg(result.value("model").toString()));

        DeviceConfig cfg = DeviceConfig::fromJson(result.value("config").toObject(), m_currentDriver->state());
        applyConfig(cfg, "Agent");

        QString rationale = result.value("rationale").toString();
        QJsonArray tools = result.value("tool_calls").toArray();
        QStringList tnames;
        for (const auto& t : tools) tnames << t.toString();
        renderOutput(cfg, rationale, tnames);
    });
    connect(m_agent, &AgentClient::configureError, this, [this](const QString& msg) {
        m_sendButton->setEnabled(true);
        m_intentEdit->setEnabled(true);
        m_statusLabel->setText("<span style='color:#ff6b6b'>●</span> 配置失败");
        m_log->append(QString("<div style='margin:4px 0;'><span style='color:#ff6b6b;font-weight:600;'>◆ 错误</span><br>"
                              "<span style='color:#ff6b6b;'>%1</span></div>").arg(msg.toHtmlEscaped()));
    });

    connect(m_agent, &AgentClient::knowledgeImported, this, [this](const QJsonObject& result) {
        int docs = result.value("stats").toObject().value("documents").toInt();
        appendChat(QString("[知识库] 导入成功，当前共 %1 篇文档").arg(docs), "#3ddc84");
        m_agent->listKnowledge();
    });
    connect(m_agent, &AgentClient::knowledgeListed, this, [this](const QJsonArray& documents) {
        m_kbList->clear();
        int chunks = 0;
        for (const QJsonValue& v : documents) {
            QJsonObject d = v.toObject();
            m_kbList->addItem(QString("%1（%2 块）").arg(d.value("title").toString())
                                                    .arg(d.value("chunk_count").toInt()));
            m_kbList->item(m_kbList->count() - 1)->setData(Qt::UserRole, d.value("id").toString());
            chunks += d.value("chunk_count").toInt();
        }
        m_kbStats->setText(QString("共 %1 篇文档 / %2 块").arg(documents.size()).arg(chunks));
    });
    connect(m_agent, &AgentClient::knowledgeRemoved, this, [this](const QJsonObject&) {
        appendChat("[知识库] 已删除文档", "#f0a13a");
        m_agent->listKnowledge();
    });
    connect(m_agent, &AgentClient::knowledgeError, this, [this](const QString& msg) {
        appendChat("[知识库] " + msg, "#ff6b6b");
    });

    updateReadouts();
    syncControlsFromConfig();
    m_agent->start();
}

MainWindow::~MainWindow() {
    m_agent->stop();
}

void MainWindow::buildUi() {
    setWindowTitle("ScopeAgent — 大模型驱动的智能示波器");
    resize(1280, 800);

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(12, 10, 12, 10);
    root->setSpacing(10);

    // 顶栏
    auto* top = new QHBoxLayout();
    auto* title = new QLabel("<span style='font-size:16px;font-weight:600;color:#e6edf3'>ScopeAgent</span>"
                             "&nbsp;&nbsp;<span style='color:#6f8fae'>大模型预研控制 · 虚拟示波器</span>");
    m_statusLabel = new QLabel("● Agent 启动中…");
    m_driverCombo = new QComboBox();
    m_driverCombo->addItem(m_virtualDriver->name(), m_virtualDriver->id());
    m_driverCombo->addItem(m_scpiDriver->name(), m_scpiDriver->id());
    top->addWidget(title);
    top->addStretch(1);
    top->addWidget(m_statusLabel);
    top->addSpacing(12);
    top->addWidget(m_driverCombo);
    root->addLayout(top);

    // 中部：波形 + 右侧面板
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setObjectName("mainSplitter");

    m_waveform = new WaveformWidget();
    m_waveform->setDriver(m_currentDriver);
    splitter->addWidget(m_waveform);

    // 右侧面板（内容较多，用滚动区包裹，避免挤压底部日志区）
    auto* right = new QWidget();
    right->setObjectName("sidePanel");
    auto* rv = new QVBoxLayout(right);
    rv->setContentsMargins(8, 8, 8, 8);
    rv->setSpacing(8);

    // 读数面板
    auto* readout = new QGroupBox("测量读数");
    auto* rf = new QFormLayout(readout);
    rf->setLabelAlignment(Qt::AlignRight);
    m_waveValue = new QLabel("-"); m_freqValue = new QLabel("-");
    m_ampValue = new QLabel("-"); m_timebaseValue = new QLabel("-");
    m_vdivValue = new QLabel("-"); m_trigValue = new QLabel("-");
    rf->addRow("波形", m_waveValue);
    rf->addRow("频率", m_freqValue);
    rf->addRow("峰峰值", m_ampValue);
    rf->addRow("时基", m_timebaseValue);
    rf->addRow("垂直档位", m_vdivValue);
    rf->addRow("触发", m_trigValue);
    rv->addWidget(readout);

    // 手动设置（2 列参数布局）
    auto* manual = new QGroupBox("手动设置（离线可用）");
    auto* mg = new QGridLayout(manual);
    mg->setHorizontalSpacing(8);
    mg->setVerticalSpacing(6);
    mg->setColumnStretch(1, 1);   // 第一列控件列拉伸
    mg->setColumnStretch(3, 1);   // 第二列控件列拉伸

    m_waveCombo = new QComboBox();
    m_waveCombo->addItems(DeviceConfig::waveforms());
    m_freqSpin = new QDoubleSpinBox(); m_freqSpin->setRange(1, 10000000);
    m_freqSpin->setDecimals(0); m_freqSpin->setSuffix(" Hz");
    m_ampSpin = new QDoubleSpinBox(); m_ampSpin->setRange(0.01, 100);
    m_ampSpin->setDecimals(3); m_ampSpin->setSuffix(" Vpp");
    m_timebaseSpin = new QDoubleSpinBox(); m_timebaseSpin->setRange(0.0000005, 1.0);
    m_timebaseSpin->setDecimals(7); m_timebaseSpin->setSuffix(" s/div");
    m_vdivSpin = new QDoubleSpinBox(); m_vdivSpin->setRange(0.005, 20);
    m_vdivSpin->setDecimals(3); m_vdivSpin->setSuffix(" V/div");
    m_trigSpin = new QDoubleSpinBox(); m_trigSpin->setRange(-100, 100);
    m_trigSpin->setDecimals(3); m_trigSpin->setSuffix(" V");
    m_slopeCombo = new QComboBox();
    m_slopeCombo->addItem("上升沿", "rising");
    m_slopeCombo->addItem("下降沿", "falling");
    m_noiseSpin = new QDoubleSpinBox(); m_noiseSpin->setRange(0, 1.0);
    m_noiseSpin->setDecimals(3);

    mg->addWidget(new QLabel("波形"), 0, 0);
    mg->addWidget(m_waveCombo, 0, 1);
    mg->addWidget(new QLabel("频率"), 0, 2);
    mg->addWidget(m_freqSpin, 0, 3);

    mg->addWidget(new QLabel("峰峰值"), 1, 0);
    mg->addWidget(m_ampSpin, 1, 1);
    mg->addWidget(new QLabel("时基"), 1, 2);
    mg->addWidget(m_timebaseSpin, 1, 3);

    mg->addWidget(new QLabel("垂直档位"), 2, 0);
    mg->addWidget(m_vdivSpin, 2, 1);
    mg->addWidget(new QLabel("触发电平"), 2, 2);
    mg->addWidget(m_trigSpin, 2, 3);

    mg->addWidget(new QLabel("触发斜率"), 3, 0);
    mg->addWidget(m_slopeCombo, 3, 1);
    mg->addWidget(new QLabel("噪声"), 3, 2);
    mg->addWidget(m_noiseSpin, 3, 3);
    rv->addWidget(manual);

    // SCPI 连接（真实仪器）
    auto* scpiGroup = new QGroupBox("SCPI 连接（真实仪器）");
    auto* sv = new QFormLayout(scpiGroup);
    sv->setLabelAlignment(Qt::AlignRight);
    m_visaAddrEdit = new QLineEdit();
    m_visaAddrEdit->setText(m_scpiDriver->visaAddress());
    m_visaAddrEdit->setPlaceholderText("TCPIP0::192.168.1.100::inst0::INSTR");
    m_connectBtn = new QPushButton("连接仪器");
    sv->addRow("VISA 地址", m_visaAddrEdit);
    sv->addRow("", m_connectBtn);
    rv->addWidget(scpiGroup);

    // 知识库（RAG）
    auto* kbGroup = new QGroupBox("知识库（RAG）");
    auto* kv = new QVBoxLayout(kbGroup);
    m_kbStats = new QLabel("尚未加载文档");
    m_importBtn = new QPushButton("导入文档（.md / .pdf）");
    m_kbList = new QListWidget();
    m_kbList->setMaximumHeight(120);
    m_removeBtn = new QPushButton("删除选中文档");
    m_removeBtn->setEnabled(false);
    kv->addWidget(m_kbStats);
    kv->addWidget(m_importBtn);
    kv->addWidget(m_kbList);
    kv->addWidget(m_removeBtn);
    rv->addWidget(kbGroup);
    rv->addStretch(1);

    auto* rightScroll = new QScrollArea();
    rightScroll->setObjectName("sideScroll");
    rightScroll->setWidgetResizable(true);
    rightScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rightScroll->setFrameShape(QFrame::NoFrame);
    rightScroll->setWidget(right);
    rightScroll->setMinimumWidth(440);
    rightScroll->setMaximumWidth(620);

    splitter->addWidget(rightScroll);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({900, 340});

    // 底部：左「对话」+ 右「输出结果」
    auto* bottom = new QSplitter(Qt::Horizontal);
    bottom->setObjectName("bottomSplitter");

    auto* chatGroup = new QGroupBox("对话");
    auto* chatV = new QVBoxLayout(chatGroup);
    m_chat = new QTextEdit();
    m_chat->setReadOnly(true);
    m_chat->setObjectName("chatView");
    m_chat->setPlaceholderText("对话记录将显示在这里…");
    m_intentEdit = new QLineEdit();
    m_intentEdit->setPlaceholderText("例如：显示 1kHz 方波，时基 200µs，触发电平 0.5V");
    m_sendButton = new QPushButton("发送");
    m_sendButton->setObjectName("primaryBtn");
    auto* inputRow = new QHBoxLayout();
    inputRow->addWidget(m_intentEdit, 1);
    inputRow->addWidget(m_sendButton);
    chatV->addWidget(m_chat, 1);
    chatV->addLayout(inputRow);
    bottom->addWidget(chatGroup);

    auto* outGroup = new QGroupBox("输出结果");
    auto* outV = new QVBoxLayout(outGroup);
    m_log = new QTextEdit();
    m_log->setReadOnly(true);
    m_log->setObjectName("logView");
    m_log->setPlaceholderText("配置结果、理由与工具调用将格式化显示在这里…");
    outV->addWidget(m_log);
    bottom->addWidget(outGroup);

    bottom->setStretchFactor(0, 1);
    bottom->setStretchFactor(1, 1);
    bottom->setChildrenCollapsible(false);
    bottom->setSizes({560, 520});

    // 纵向分割器：波形区（上）与底部（对话+输出，下）均可拖动调整大小
    auto* vSplitter = new QSplitter(Qt::Vertical);
    vSplitter->setObjectName("mainVSplitter");
    vSplitter->addWidget(splitter);
    vSplitter->addWidget(bottom);
    vSplitter->setStretchFactor(0, 1);
    vSplitter->setStretchFactor(1, 0);
    vSplitter->setChildrenCollapsible(false);
    vSplitter->setSizes({520, 220});
    root->addWidget(vSplitter, 1);

    setCentralWidget(central);

    connect(m_driverCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onDriverChanged);
    connect(m_sendButton, &QPushButton::clicked, this, &MainWindow::onSendIntent);
    connect(m_intentEdit, &QLineEdit::returnPressed, this, &MainWindow::onSendIntent);

    // 手动控件变更
    auto connectSpin = [this](QDoubleSpinBox* s) {
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onManualChanged);
    };
    connectSpin(m_freqSpin); connectSpin(m_ampSpin); connectSpin(m_timebaseSpin);
    connectSpin(m_vdivSpin); connectSpin(m_trigSpin); connectSpin(m_noiseSpin);
    connect(m_waveCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onManualChanged);
    connect(m_slopeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onManualChanged);
    connect(m_connectBtn, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(m_importBtn, &QPushButton::clicked, this, &MainWindow::onImportKnowledge);
    connect(m_removeBtn, &QPushButton::clicked, this, &MainWindow::onRemoveKnowledge);
    connect(m_kbList, &QListWidget::itemSelectionChanged, this, [this]() {
        m_removeBtn->setEnabled(m_kbList->currentItem() != nullptr);
    });

    // 初始状态：虚拟驱动，SCPI 连接项禁用
    m_visaAddrEdit->setEnabled(false);
    m_connectBtn->setEnabled(false);
}

void MainWindow::applyStyle() {
    setStyleSheet(R"(
        QMainWindow, QWidget { background:#0d1117; color:#c9d1d9; font-family:"Microsoft YaHei","Segoe UI",sans-serif; font-size:13px; }
        QGroupBox { border:1px solid #21262d; border-radius:6px; margin-top:12px; padding-top:6px; }
        QGroupBox::title { subcontrol-origin:margin; left:10px; padding:0 4px; color:#8fb4d8; font-weight:600; }
        QLabel { color:#c9d1d9; }
        QLineEdit, QComboBox, QDoubleSpinBox, QTextEdit {
            background:#161b22; border:1px solid #30363d; border-radius:5px; padding:5px 8px;
            selection-background-color:#1f6feb;
        }
        QLineEdit:focus, QComboBox:focus, QDoubleSpinBox:focus { border:1px solid #1f6feb; }
        QComboBox::drop-down { border:none; width:20px; }
        QComboBox QAbstractItemView { background:#161b22; border:1px solid #30363d; selection-background-color:#1f6feb; }
        QPushButton { background:#21262d; border:1px solid #30363d; border-radius:5px; padding:7px 14px; }
        QPushButton:hover { background:#30363d; }
        QPushButton#primaryBtn { background:#1f6feb; border:none; color:white; font-weight:600; }
        QPushButton#primaryBtn:hover { background:#388bfd; }
        QPushButton#primaryBtn:disabled { background:#30363d; color:#6f8fae; }
        QTextEdit#logView { font-family:Consolas,"Courier New",monospace; font-size:12px; color:#8fb4d8; }
        QSplitter::handle { background:#21262d; }
        QDoubleSpinBox { min-width:90px; }
    )");
}

void MainWindow::onDriverChanged(int index) {
    QString id = m_driverCombo->itemData(index).toString();
    IDriver* next = (id == "scpi") ? static_cast<IDriver*>(m_scpiDriver)
                                   : static_cast<IDriver*>(m_virtualDriver);
    if (next == m_currentDriver) return;
    m_currentDriver = next;
    m_waveform->setDriver(m_currentDriver);
    updateReadouts();
    syncControlsFromConfig();

    bool isScpi = (id == "scpi");
    m_visaAddrEdit->setEnabled(isScpi);
    m_connectBtn->setEnabled(isScpi);
    if (isScpi) {
        m_visaAddrEdit->setText(m_scpiDriver->visaAddress());
        m_connectBtn->setText(m_scpiDriver->connected() ? "断开连接" : "连接仪器");
        appendChat("[驱动] 切换到 " + next->name() + "，点击「连接仪器」尝试连接真实设备", "#8fb4d8");
    } else {
        appendChat("[驱动] 切换到 " + next->name(), "#8fb4d8");
    }
}

void MainWindow::onSendIntent() {
    QString intent = m_intentEdit->text().trimmed();
    if (intent.isEmpty()) return;
    m_agent->configure(intent, m_currentDriver->state().toJson());
    m_intentEdit->clear();
}

void MainWindow::onManualChanged() {
    if (m_syncing) return;
    DeviceConfig cfg = configFromControls();
    applyConfig(cfg, "手动");
}

void MainWindow::onConnectClicked() {
    if (m_scpiDriver->connected()) {
        m_scpiDriver->disconnect();
        m_connectBtn->setText("连接仪器");
        appendChat("[SCPI] 已断开连接", "#f0a13a");
        updateReadouts();
        m_waveform->update();
        return;
    }

    QString addr = m_visaAddrEdit->text().trimmed();
    m_connectBtn->setEnabled(false);
    appendChat("[SCPI] 正在连接 " + addr + " …", "#8fb4d8");
    bool ok = m_scpiDriver->connectTo(addr);
    m_connectBtn->setEnabled(true);

    if (ok) {
        m_connectBtn->setText("断开连接");
        appendChat("[SCPI] 连接成功，仪器：" + m_scpiDriver->idn(), "#3ddc84");
    } else {
        m_connectBtn->setText("连接仪器");
        appendChat("[SCPI] 连接失败：" + m_scpiDriver->lastError(), "#ff6b6b");
    }
    updateReadouts();
    m_waveform->update();
}

DeviceConfig MainWindow::configFromControls() const {
    DeviceConfig c = m_currentDriver->state();
    c.waveform = m_waveCombo->currentText();
    c.frequencyHz = m_freqSpin->value();
    c.amplitudeVpp = m_ampSpin->value();
    c.secPerDiv = m_timebaseSpin->value();
    c.voltsPerDiv = m_vdivSpin->value();
    c.triggerLevelV = m_trigSpin->value();
    c.triggerSlope = m_slopeCombo->currentData().toString();
    c.noiseLevel = m_noiseSpin->value();
    return c;
}

void MainWindow::applyConfig(const DeviceConfig& cfg, const QString& source) {
    m_currentDriver->apply(cfg);
    m_waveform->update();
    updateReadouts();
    syncControlsFromConfig();
    if (source == "Agent") {
        // 展示 SCPI 驱动下本应下发的命令
        if (auto* s = dynamic_cast<ScpiDriver*>(m_currentDriver)) {
            appendChat("[SCPI 命令] " + s->lastCommands().join("  "), "#6f8fae");
        }
    }
}

void MainWindow::updateReadouts() {
    if (!m_currentDriver) return;
    DeviceConfig c = m_currentDriver->state();
    m_waveValue->setText(c.waveform);
    m_freqValue->setText(fmt(c.frequencyHz) + " Hz");
    m_ampValue->setText(fmt(c.amplitudeVpp) + " Vpp");
    m_timebaseValue->setText(fmt(c.secPerDiv) + " s/div");
    m_vdivValue->setText(fmt(c.voltsPerDiv) + " V/div");
    m_trigValue->setText(QString("%1 V (%2)").arg(fmt(c.triggerLevelV),
        c.triggerSlope == "rising" ? "上升" : "下降"));
}

void MainWindow::syncControlsFromConfig() {
    if (!m_currentDriver) return;
    DeviceConfig c = m_currentDriver->state();
    m_syncing = true;
    m_waveCombo->setCurrentText(c.waveform);
    m_freqSpin->setValue(c.frequencyHz);
    m_ampSpin->setValue(c.amplitudeVpp);
    m_timebaseSpin->setValue(c.secPerDiv);
    m_vdivSpin->setValue(c.voltsPerDiv);
    m_trigSpin->setValue(c.triggerLevelV);
    m_slopeCombo->setCurrentIndex(m_slopeCombo->findData(c.triggerSlope));
    m_noiseSpin->setValue(c.noiseLevel);
    m_syncing = false;
}

void MainWindow::appendChat(const QString& text, const QString& color) {
    QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
    m_chat->append(QString("<span style='color:#555'>[%1]</span> "
                           "<span style='color:%2'>%3</span>")
                       .arg(ts, color, text.toHtmlEscaped()));
}

void MainWindow::renderOutput(const DeviceConfig& cfg, const QString& rationale,
                              const QStringList& tools) {
    static const QStringList waveEN = {"sine", "square", "triangle", "sawtooth", "noise"};
    static const QStringList waveCN = {"正弦", "方波", "三角", "锯齿", "噪声"};
    QString wave = waveEN.contains(cfg.waveform) ? waveCN[waveEN.indexOf(cfg.waveform)]
                                                 : cfg.waveform;
    QString slope = cfg.triggerSlope == "rising" ? "上升" : "下降";

    auto cell = [](const QString& k, const QString& v) {
        return QString("<td style='color:#8b949e;padding:1px 8px 1px 0;white-space:nowrap;'>%1</td>"
                       "<td style='color:#e6edf3;padding:1px 14px 1px 0;'>%2</td>")
                   .arg(k, v);
    };

    QString html;
    html += "<div style='margin:4px 0;'>";
    html += "<span style='color:#3ddc84;font-weight:600;'>◆ 配置结果</span><br>";
    html += "<table cellspacing='0' cellpadding='0' style='margin-top:2px;'>";
    html += "<tr>" + cell("波形", wave) + cell("频率", fmt(cfg.frequencyHz) + " Hz") + "</tr>";
    html += "<tr>" + cell("峰峰值", fmt(cfg.amplitudeVpp) + " Vpp")
            + cell("时基", fmt(cfg.secPerDiv) + " s/div") + "</tr>";
    html += "<tr>" + cell("垂直档位", fmt(cfg.voltsPerDiv) + " V/div")
            + cell("触发电平", QString("%1 V (%2)").arg(fmt(cfg.triggerLevelV), slope)) + "</tr>";
    html += "<tr>" + cell("直流偏置", fmt(cfg.dcOffsetV) + " V")
            + cell("噪声", QString::number(cfg.noiseLevel, 'f', 3)) + "</tr>";
    html += "</table>";
    if (!rationale.isEmpty()) {
        html += "<br><span style='color:#8fb4d8;font-weight:600;'>◆ 理由</span><br>"
                "<span style='color:#9fd8a0;'>" + rationale.toHtmlEscaped() + "</span>";
    }
    if (!tools.isEmpty()) {
        html += "<br><span style='color:#8fb4d8;font-weight:600;'>◆ 工具调用</span><br>"
                "<span style='color:#6f8fae;'>" + tools.join(" → ").toHtmlEscaped() + "</span>";
    }
    html += "</div><br>";

    m_log->append(html);
}

void MainWindow::onImportKnowledge() {
    QString path = QFileDialog::getOpenFileName(this, "导入知识库文档", QString(),
                                                "文档 (*.md *.pdf)");
    if (path.isEmpty()) return;
    appendChat("[知识库] 导入 " + path + " …", "#8fb4d8");
    m_agent->importKnowledge(path);
}

void MainWindow::onRemoveKnowledge() {
    QListWidgetItem* item = m_kbList->currentItem();
    if (!item) return;
    QString id = item->data(Qt::UserRole).toString();
    appendChat("[知识库] 删除 " + item->text() + " …", "#8fb4d8");
    m_agent->removeKnowledge(id);
}

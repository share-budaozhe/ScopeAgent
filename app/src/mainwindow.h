#pragma once
#include <QMainWindow>
#include <QJsonObject>
#include <QStringList>
#include "deviceconfig.h"

class WaveformWidget;
class AgentClient;
class VirtualScopeDriver;
class ScpiDriver;
class QLabel;
class QLineEdit;
class QComboBox;
class QDoubleSpinBox;
class QTextEdit;
class QPushButton;
class QListWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onDriverChanged(int index);
    void onSendIntent();
    void onManualChanged();
    void onConnectClicked();
    void onImportKnowledge();
    void onRemoveKnowledge();

private:
    void buildUi();
    void applyStyle();
    void updateReadouts();
    void syncControlsFromConfig();
    void appendChat(const QString& text, const QString& color = "#8fb4d8");
    void renderOutput(const DeviceConfig& cfg, const QString& rationale, const QStringList& tools);
    DeviceConfig configFromControls() const;
    void applyConfig(const DeviceConfig& cfg, const QString& source);

    // UI
    WaveformWidget* m_waveform = nullptr;
    QLabel* m_statusLabel = nullptr;
    QComboBox* m_driverCombo = nullptr;
    QLineEdit* m_intentEdit = nullptr;
    QPushButton* m_sendButton = nullptr;
    QTextEdit* m_log = nullptr;
    QTextEdit* m_chat = nullptr;

    // 读数
    QLabel* m_freqValue = nullptr;
    QLabel* m_ampValue = nullptr;
    QLabel* m_timebaseValue = nullptr;
    QLabel* m_vdivValue = nullptr;
    QLabel* m_trigValue = nullptr;
    QLabel* m_waveValue = nullptr;

    // 手动控件
    QComboBox* m_waveCombo = nullptr;
    QDoubleSpinBox* m_freqSpin = nullptr;
    QDoubleSpinBox* m_ampSpin = nullptr;
    QDoubleSpinBox* m_timebaseSpin = nullptr;
    QDoubleSpinBox* m_vdivSpin = nullptr;
    QDoubleSpinBox* m_trigSpin = nullptr;
    QComboBox* m_slopeCombo = nullptr;
    QDoubleSpinBox* m_noiseSpin = nullptr;
    QLineEdit* m_visaAddrEdit = nullptr;
    QPushButton* m_connectBtn = nullptr;

    // 知识库
    QLabel* m_kbStats = nullptr;
    QPushButton* m_importBtn = nullptr;
    QPushButton* m_removeBtn = nullptr;
    QListWidget* m_kbList = nullptr;

    // 驱动与 Agent
    VirtualScopeDriver* m_virtualDriver = nullptr;
    ScpiDriver* m_scpiDriver = nullptr;
    IDriver* m_currentDriver = nullptr;
    AgentClient* m_agent = nullptr;

    bool m_syncing = false;
};

#include "scpidriver.h"
#include <cmath>
#include <cstring>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

// ===== VISA 动态加载封装（避免链接期依赖 visa 库）=====
struct ScpiDriver::VisaImpl {
    typedef unsigned long ViUInt32;
    typedef long ViStatus;
    typedef ViUInt32 ViSession;
    typedef char* ViRsrc;

    typedef ViStatus (__stdcall* FnOpenDefaultRM)(ViSession*);
    typedef ViStatus (__stdcall* FnOpen)(ViSession, ViRsrc, ViUInt32, ViUInt32, ViSession*);
    typedef ViStatus (__stdcall* FnWrite)(ViSession, const char*, ViUInt32, ViUInt32*);
    typedef ViStatus (__stdcall* FnRead)(ViSession, char*, ViUInt32, ViUInt32*);
    typedef ViStatus (__stdcall* FnClose)(ViSession);

    HMODULE lib = nullptr;
    FnOpenDefaultRM openDefaultRM = nullptr;
    FnOpen open = nullptr;
    FnWrite write = nullptr;
    FnRead read = nullptr;
    FnClose close = nullptr;
    ViSession rm = 0;
    ViSession vi = 0;
    bool loaded = false;

    bool load() {
#ifdef Q_OS_WIN
        if (loaded) return true;
        lib = LoadLibraryA("visa64.dll");
        if (!lib) lib = LoadLibraryA("visa32.dll");
        if (!lib) return false;
        openDefaultRM = (FnOpenDefaultRM)GetProcAddress(lib, "viOpenDefaultRM");
        open = (FnOpen)GetProcAddress(lib, "viOpen");
        write = (FnWrite)GetProcAddress(lib, "viWrite");
        read = (FnRead)GetProcAddress(lib, "viRead");
        close = (FnClose)GetProcAddress(lib, "viClose");
        loaded = openDefaultRM && open && write && read && close;
        return loaded;
#else
        return false;
#endif
    }

    void unload() {
        if (close && vi) { close(vi); vi = 0; }
        if (close && rm) { close(rm); rm = 0; }
        if (lib) { FreeLibrary(lib); lib = nullptr; }
        loaded = false;
    }
};

ScpiDriver::ScpiDriver() {
    m_cfg = DeviceConfig::defaults();
    m_visa = new VisaImpl();
}

ScpiDriver::~ScpiDriver() {
    if (m_visa) {
        m_visa->unload();
        delete m_visa;
        m_visa = nullptr;
    }
}

bool ScpiDriver::connectTo(const QString& addr) {
    m_visaAddress = addr;
    m_connected = false;
    m_idn.clear();
    m_lastError.clear();

    if (!m_visa->load()) {
        m_lastError = "未找到 VISA 库 (visa64.dll/visa32.dll)，请安装 IVI/Keysight VISA";
        return false;
    }

    VisaImpl::ViStatus st = m_visa->openDefaultRM(&m_visa->rm);
    if (st != 0) {
        m_lastError = QString("viOpenDefaultRM 失败，状态码 0x%1").arg((long)st, 0, 16);
        return false;
    }

    QByteArray rsrc = addr.toLatin1();
    st = m_visa->open(m_visa->rm, rsrc.data(), 0, 2000, &m_visa->vi);
    if (st != 0) {
        m_lastError = "viOpen 失败：未找到仪器 " + addr;
        m_visa->close(m_visa->rm);
        m_visa->rm = 0;
        return false;
    }

    m_connected = true;
    m_idn = queryScpi("*IDN?").trimmed();
    if (m_idn.isEmpty()) {
        m_lastError = "已连接但 *IDN? 无响应";
    }
    return true;
}

void ScpiDriver::disconnect() {
    m_connected = false;
    m_idn.clear();
    if (m_visa) m_visa->unload();
}

QString ScpiDriver::sendScpi(const QString& cmd) const {
    if (!m_visa || !m_connected) return QString();
    QByteArray w = (cmd + "\n").toLatin1();
    VisaImpl::ViUInt32 written = 0;
    m_visa->write(m_visa->vi, w.constData(), (VisaImpl::ViUInt32)w.size(), &written);
    return QString();
}

QString ScpiDriver::queryScpi(const QString& cmd) const {
    if (!m_visa || !m_connected) return QString();
    QByteArray w = (cmd + "\n").toLatin1();
    VisaImpl::ViUInt32 written = 0;
    m_visa->write(m_visa->vi, w.constData(), (VisaImpl::ViUInt32)w.size(), &written);
    char buf[512];
    std::memset(buf, 0, sizeof(buf));
    VisaImpl::ViUInt32 readn = 0;
    m_visa->read(m_visa->vi, buf, sizeof(buf) - 1, &readn);
    return QString::fromLatin1(buf, (int)readn);
}

bool ScpiDriver::apply(const DeviceConfig& c) {
    m_cfg = DeviceConfig::clamped(c);
    m_lastCommands.clear();

    m_lastCommands << QString(":CHANnel1:SCALe %1").arg(m_cfg.voltsPerDiv);
    m_lastCommands << QString(":CHANnel1:OFFSet %1").arg(-m_cfg.dcOffsetV);
    m_lastCommands << QString(":TIMebase:SCALe %1").arg(m_cfg.secPerDiv);
    m_lastCommands << QString(":TRIGger:EDGE:SOURce CHANnel1");
    m_lastCommands << QString(":TRIGger:EDGE:LEVel %1").arg(m_cfg.triggerLevelV);
    m_lastCommands << QString(":TRIGger:EDGE:SLOPe %1")
                          .arg(m_cfg.triggerSlope == "rising" ? "POSitive" : "NEGative");

    for (const QString& cmd : m_lastCommands) {
        sendScpi(cmd);   // 未连接时静默忽略
    }
    return true;
}

QVector<double> ScpiDriver::acquire(int points, double triggerPos) const {
    Q_UNUSED(triggerPos);
    QVector<double> out(points);
    if (m_connected) {
        // 真实实现点：:WAVeform:PREamble? 解析波形参数，:WAVeform:DATA? 读取二进制样本
        // 此处返回零线，波形显示区会提示当前为 SCPI 驱动。
    }
    return out;
}

QStringList ScpiDriver::capabilities() const {
    QStringList caps;
    caps << "VISA 地址：" + m_visaAddress;
    caps << "连接状态：" + QString(m_connected ? "已连接" : "未连接");
    if (m_connected && !m_idn.isEmpty()) caps << "仪器：" + m_idn;
    if (!m_connected && !m_lastError.isEmpty()) caps << "错误：" + m_lastError;
    caps << "支持 SCPI：:CHANnel1:SCALe / :TIMebase:SCALe / :TRIGger:LEVel 等";
    return caps;
}

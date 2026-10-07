# ScopeAgent — 大模型驱动的智能示波器 MVP 方案（PR）

> 版本：v1.0　状态：✅ 已实现并交付（见 README.md / docs/ / dist/）　作者：pi

---

## 1. 背景与目标

传统仪器仪表（以示波器为例）设置复杂：用户需要手动配置垂直档位、时基、触发、耦合、采样率等大量参数，学习成本高、易出错。

**目标**：实现一个「大模型预研控制」MVP，让用户用**自然语言**驱动一台（虚拟）示波器，由 DeepSeek 大模型完成「预研 + 参数推理 + 配置生成」，自动完成复杂的仪器设置。同时沉淀一套**可扩展的 Agent 集成框架**，为后续接入真实仪器（SCPI/VISA 等驱动）打下基础。

**MVP 范围**：
- 一台**虚拟示波器**（软件合成波形，行为模拟真实示波器）
- 自然语言输入 → DeepSeek Agent 推理 → 生成结构化配置 → Qt 应用下发 → 波形实时更新
- **驱动接口抽象**：内置虚拟驱动 + SCPI/VISA 驱动骨架（演示不同驱动如何兼容接入）

**非目标（本期不做）**：真实硬件采集、多仪器协同、本地模型部署、鉴权/多用户。

---

## 2. 环境勘察结论（已实测）

| 项 | 结果 |
|---|---|
| Qt | 5.12.2，`mingw73_64` 套件（g++ 7.3.0 + mingw32-make），qmake/windeployqt 齐全 |
| Python | 3.10.8（sidecar 仅用标准库，零第三方依赖） |
| 大模型 | DeepSeek API：`https://api.deepseek.com`，模型 `deepseek-v4-pro`（默认）/ `deepseek-flash`，OpenAI 兼容，**支持 function calling + reasoning_content** |
| 网络 | DeepSeek API 可达（已验证 /chat/completions 与 /models） |
| 编译器 | MinGW 7.3.0 x86_64（Qt 自带，无需额外安装） |

---

## 3. 总体架构

采用**「Qt 宿主 + Agent Sidecar 独立进程」**模式，二者通过本地 HTTP/JSON 通信。

```
┌────────────────────────── Qt 应用 (C++/Qt 5.12) ──────────────────────────┐
│                                                                            │
│  MainWindow (主界面，暗色示波器风格)                                        │
│   ├─ WaveformWidget    示波器波形绘制（网格/辉光轨迹/触发线/刻度）           │
│   ├─ ControlPanel      参数读数 + 自然语言输入框                             │
│   └─ ChatLog           Agent 推理过程 / rationale / 已应用配置               │
│                                                                            │
│  AgentClient    ── 管理 sidecar 进程 + HTTP 调用（异步，不卡 UI）            │
│                                                                            │
│  IDriver (驱动抽象接口)                                                     │
│   ├─ VirtualScopeDriver   虚拟示波器（合成正弦/方波/三角/噪声）              │
│   └─ ScpiDriver           真实仪器驱动骨架（VISA/SCPI，演示接入模式）        │
│                                                                            │
│  DeviceConfig ── 配置结构体 + JSON 序列化 + 范围钳制校验                     │
└───────────────────────────────┬──────────────────────────────▲─────────────┘
                POST /v1/configure│                              │ JSON 响应
                (intent + state) │                              │(config+rationale)
                                 ▼                              │
┌───────────────────────────────┴──────────────────────────────┴─────────────┐
│                    Agent Sidecar (Python 3.10, 纯标准库)                    │
│                                                                            │
│  server.py          HTTP Server (http.server, localhost:8765)               │
│   ├─ GET  /health         健康检查（含模型名/版本）                         │
│   └─ POST /v1/configure   自然语言 → 配置                                    │
│                                                                            │
│  agent.py           Function-Calling Agent 循环（最多 N 轮）                │
│  deepseek_client.py DeepSeek API 客户端（urllib，含 reasoning_content）     │
│  tools.py           工具集：get_spec / get_state / configure_scope          │
│  schema.py          DeviceConfig 定义 + 校验 + 范围钳制（无第三方依赖）      │
└────────────────────────────────────────────────────────────────────────────┘
```

**核心设计原则**：
1. **Agent 只建议，Qt 是唯一 truth source** —— LLM 输出必须经 schema 校验 + 范围钳制后才下发。
2. **契约先行** —— 两侧共享同一份 `DeviceConfig` 字段定义（JSON）。
3. **驱动可插拔** —— 所有驱动实现 `IDriver` 接口，Agent 无需关心底层是虚拟还是真实仪器。
4. **零第三方 Python 依赖** —— 交付即用，不依赖 pip 安装。

---

## 4. 通信协议

Base URL：`http://127.0.0.1:8765`

### 4.1 `GET /health`
```json
{"ok": true, "version": "0.1.0", "model": "deepseek-v4-pro"}
```

### 4.2 `POST /v1/configure`
请求：
```json
{
  "intent": "测量一个 1kHz、3V 峰峰值的正弦波，触发稳定一点",
  "state": {                                   // Qt 上报当前真实状态
    "waveform": "sine", "frequency_hz": 1000.0, "amplitude_vpp": 3.0,
    "dc_offset_v": 0.0, "sec_per_div": 0.0005, "volts_per_div": 1.0,
    "trigger_level_v": 0.0, "trigger_slope": "rising", "noise_level": 0.02
  }
}
```
响应：
```json
{
  "ok": true,
  "config": {                                  // 已校验/钳制的最终配置
    "waveform": "sine", "frequency_hz": 1000.0, "amplitude_vpp": 3.0,
    "dc_offset_v": 0.0, "sec_per_div": 0.0002, "volts_per_div": 1.0,
    "trigger_level_v": 0.0, "trigger_slope": "rising", "noise_level": 0.02
  },
  "rationale": "1kHz 正弦波周期 1ms，为在屏幕上显示约 2 个周期，时基设为 200µs/div；3Vpp 用 1V/div 可完整显示……",
  "confidence": 0.92,
  "model": "deepseek-v4-pro"
}
```

---

## 5. 数据模型（DeviceConfig 契约）

| 字段 | 类型 | 范围（虚拟示波器） | 说明 |
|---|---|---|---|
| waveform | enum | sine/square/triangle/sawtooth/noise | 波形 |
| frequency_hz | double | 1 ~ 10_000_000 | 信号频率 |
| amplitude_vpp | double | 0.01 ~ 100 | 峰峰值 |
| dc_offset_v | double | -100 ~ 100 | 直流偏置 |
| sec_per_div | double | 0.000_000_5 ~ 1.0 | 时基 |
| volts_per_div | double | 0.005 ~ 20 | 垂直档位 |
| trigger_level_v | double | -100 ~ 100 | 触发电平 |
| trigger_slope | enum | rising/falling | 触发斜率 |
| noise_level | double | 0 ~ 1.0 | 噪声幅度（占 Vpp 比例） |

> 校验策略：Python 与 Qt **双层钳制**（clamp 到合法范围 + enum 白名单），非法字段拒绝或取默认值。

---

## 6. 驱动接口抽象（IDriver）

```cpp
class IDriver {
public:
    virtual ~IDriver() = default;
    virtual QString id() const = 0;                       // "virtual" / "scpi:TCPIP0::..."
    virtual QString name() const = 0;                     // 显示名
    virtual DeviceConfig state() const = 0;               // 读当前配置
    virtual bool apply(const DeviceConfig& c) = 0;        // 下发配置（含钳制）
    virtual QVector<double> acquire(int points) const = 0;// 采集波形样本
    virtual QStringList capabilities() const = 0;         // 能力描述（供 Agent spec）
    virtual bool connected() const = 0;                   // 连接状态
};
```

- `VirtualScopeDriver`：按 config 实时合成波形（正弦/方波/三角/锯齿 + 噪声）。
- `ScpiDriver`：演示真实仪器接入——内部把 config 映射为 SCPI 命令（如 `:TIM:SCAL`、`:TRIG:LEV`），通过 VISA 发送；MVP 下未连硬件时返回「未连接」，仅展示命令生成与连接检测逻辑，后续接真实仪器时只需补全收发实现。

---

## 7. 技术栈汇总

| 层 | 选型 | 理由 |
|---|---|---|
| GUI | Qt 5.12 Widgets + QPainter + QSS 暗色主题 | 环境已具备，QPainter 可精确绘制示波器效果 |
| 构建 | qmake + MinGW 7.3.0 | 稳定，规避 cmake4/Qt5 兼容问题 |
| Agent | DeepSeek `deepseek-v4-pro`（function calling） | 已实测可用，OpenAI 兼容 |
| Sidecar | Python 3.10 纯标准库（http.server/urllib/json/threading） | 零依赖、易交付、易跨平台 |
| 通信 | 本地 HTTP/JSON（127.0.0.1:8765） | 简单可靠，后续可平滑升级 MCP/gRPC |
| 打包 | windeployqt + 脚本 | 生成绿色可执行目录 |

---

## 8. 分步实施计划（每步自测）

| 步 | 内容 | 自测方法 |
|---|---|---|
| **S0** | 项目骨架 + PR + 目录结构 | 目录/文档齐全 |
| **S1** | Python sidecar：deepseek_client + schema + tools + agent + server | `curl /health`、`curl /v1/configure` 返回合法 JSON；非法输入被钳制 |
| **S2** | Qt 工程骨架：.pro + main + 空 MainWindow 暗色主题 | qmake + mingw32-make 编译通过，窗口可启动 |
| **S3** | DeviceConfig + IDriver + VirtualScopeDriver + WaveformWidget | 编译运行，显示动态波形、网格、刻度 |
| **S4** | AgentClient（QProcess + HTTP）+ 自然语言驱动闭环 | 输入中文意图 → 配置变更 → 波形实时变化 |
| **S5** | UI 美化（示波器级）、ScpiDriver 骨架、参数读数面板、rationale 展示 | 整体效果验收、驱动切换可用 |
| **S6** | 打包（windeployqt + sidecar 集成）+ 文档（环境搭建/测试说明）+ 交付 | dist 目录双击即可运行 |

---

## 9. 交付物清单

```
ScopeAgent/
├── PR.md                      本方案
├── README.md                  项目总览 + 快速开始
├── docs/
│   ├── 环境搭建说明.md
│   └── 测试说明.md
├── app/                       Qt C++ 源码（qmake 工程）
│   ├── ScopeAgent.pro
│   ├── src/*.cpp *.h
│   └── drivers/               IDriver / Virtual / Scpi
├── agent/                     Python sidecar（纯标准库）
│   ├── server.py  agent.py  deepseek_client.py  tools.py  schema.py
│   └── config.example.json
├── scripts/
│   ├── build.bat              一键编译
│   └── run.bat                一键运行
└── dist/                      可执行程序（含 Qt DLL + sidecar + 启动脚本）
```

---

## 10. 风险与对策

| 风险 | 对策 |
|---|---|
| LLM 输出越界/幻觉 | 双层 schema 校验 + 范围钳制；Qt 侧兜底 |
| DeepSeek 调用延迟（5~30s）卡 UI | HTTP 异步 + 工作线程；UI 显示「推理中」状态 |
| sidecar 启动失败 | 健康检查 + 超时重试 + 离线提示（仍可手动操作示波器） |
| 无 API Key | 读取 `DEEPSEEK_API_KEY` 环境变量或 `agent/config.json`，缺失时明确提示 |
| 打包后 Qt DLL 缺失 | windeployqt 自动收集依赖 |
| Python 未安装（目标机） | 交付说明要求 Python 3.8+；后续可用 PyInstaller 打独立 exe（本次不做） |

---

## 11. 验收标准

1. 双击 `dist/run.bat`（或 exe）即可启动，无需手工配置环境（除 API Key）。
2. 在输入框输入「显示 1kHz 方波，时基 200µs，触发电平 0.5V」等自然语言，波形与参数读数随之正确变化。
3. 无 API Key 或断网时，界面给出清晰提示，且手动参数调整仍可用。
4. 驱动下拉框可在「虚拟示波器」与「SCPI 示波器(骨架)」间切换。
5. 代码结构满足 IDriver 抽象，新增驱动只需实现接口 + 注册。

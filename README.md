# ScopeAgent — 大模型驱动的智能示波器（MVP）

用**自然语言**控制一台虚拟示波器，由 DeepSeek 大模型完成「预研 + 参数推理 + 配置生成」，自动完成复杂的仪器设置。

```
┌──────────────────────── Qt 应用 (C++/Qt 5.12) ────────────────────────┐
│  WaveformWidget 示波器波形 · ControlPanel 读数/输入 · ChatLog 推理日志  │
│  AgentClient (QProcess + HTTP，异步)                                    │
│  IDriver 抽象 ── VirtualScopeDriver(虚拟) / ScpiDriver(VISA 骨架)      │
└───────────────────────────────┬────────────────────────────────────────┘
                    HTTP/JSON (127.0.0.1:8765)
                                │
┌───────────────────────────────▼────────────────────────────────────────┐
│  Agent Sidecar (Python 3.10 纯标准库)                                   │
│  server.py · agent.py(函数调用循环) · deepseek_client.py · tools.py     │
│  schema.py (DeviceConfig 契约 + 双层钳制校验)                           │
└───────────────────────────────┬────────────────────────────────────────┘
                                │  function calling
                     DeepSeek API (deepseek-v4-pro)
```

## 功能

- ✅ **自然语言驱动**：输入「显示 1kHz 方波，时基 200µs，触发电平 0.5V」，Agent 自动推理并下发配置
- ✅ **预研控制**：Agent 先查询设备能力（`get_spec`）、当前状态（`get_state`），再计算合理参数（`configure_scope`），并给出**配置理由 rationale**
- ✅ **虚拟示波器**：正弦/方波/三角/锯齿/噪声，触发相位对齐，辉光轨迹，暗色示波器界面
- ✅ **驱动接口抽象**：`IDriver` 统一接口，内置虚拟驱动 + **真实 SCPI/VISA 驱动**（动态加载 `visa64.dll`，支持 `*IDN?` 查询与命令收发）
- ✅ **安全钳制**：LLM 输出经 Python + Qt **双层** schema 校验与范围钳制，杜绝越界配置
- ✅ **离线可用**：无 API Key / 断网时仍可手动调整参数

## 快速开始（打包版）

1. **无需安装 Python**（sidecar 已打包为独立 exe；仅在源码开发/打包时需要 Python）
2. 配置 DeepSeek Key（二选一）：
   - 环境变量：`set DEEPSEEK_API_KEY=sk-xxx`
   - 或编辑 `dist\agent\config.json` 的 `deepseek_api_key`
3. 双击 `dist\run.bat` 启动

## 项目结构

```
ScopeAgent/
├── PR.md                   方案 PR
├── README.md               本文件
├── docs/
│   ├── 环境搭建说明.md
│   └── 测试说明.md
├── app/                    Qt C++ 工程（qmake）
│   ├── ScopeAgent.pro
│   ├── src/                mainwindow / waveformwidget / agentclient / deviceconfig
│   └── drivers/            idriver 抽象 + virtualscopedriver + scpidriver
├── agent/                  Python sidecar（纯标准库）
│   ├── server.py           HTTP 服务（/health、/v1/configure、/mcp）
│   ├── mcp_server.py       MCP 服务端（stdio + HTTP，4 个工具）
│   ├── agent.py            function-calling Agent 循环
│   ├── deepseek_client.py  DeepSeek 客户端
│   ├── tools.py            工具集（get_spec/get_state/configure_scope）
│   ├── schema.py           DeviceConfig 契约 + 校验钳制
│   └── config.json         模型/密钥配置（可选，优先读环境变量）
├── tests/                  自动化测试源码
├── scripts/
│   ├── build.bat           一键编译
│   ├── test.bat            一键测试
│   ├── run.bat             开发运行
│   └── package.bat         打包到 dist/
└── dist/                   可执行程序（含 Qt DLL + sidecar + 启动脚本）
```

## 如何扩展驱动（接入真实仪器）

所有驱动实现 `IDriver` 接口（见 `app/src/deviceconfig.h`）：

```cpp
class IDriver {
public:
    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual DeviceConfig state() const = 0;          // 读当前配置
    virtual bool apply(const DeviceConfig& c) = 0;   // 下发配置
    virtual QVector<double> acquire(int points) const = 0; // 采集波形
    virtual QStringList capabilities() const = 0;
    virtual bool connected() const = 0;
};
```

以 `ScpiDriver` 为模板：它已通过 `visa64.dll` 动态加载 VISA，实现了 `viOpen`/`viWrite`/`viRead` 的真实收发——在界面输入 VISA 地址（如 `TCPIP0::192.168.1.100::inst0::INSTR`）点击「连接仪器」即可查询 `*IDN?` 并下发 SCPI 命令。接真实示波器只需在 `acquire()` 中补全 `:WAVeform:DATA?` 的波形读取。**Agent 侧无需任何改动**——它只与 `DeviceConfig` 契约交互，与底层是虚拟还是真实仪器无关。

## 通信协议

| 接口 | 方法 | 说明 |
|---|---|---|
| `/health` | GET | 健康检查，返回模型/版本/就绪状态 |
| `/v1/configure` | POST | `{intent, state}` → `{config, rationale, confidence, model}` |
| `/mcp` | POST | MCP 协议端点（JSON-RPC 2.0） |

## MCP 支持（标准化 Agent 工具接入）

sidecar 同时是一个 **MCP server**，把示波器能力暴露为 4 个标准工具，任意 MCP 客户端（Cursor、Claude Desktop、其他 Agent 框架）都可直接驱动：

| MCP 工具 | 说明 |
|---|---|
| `get_spec` | 查询示波器能力规格 |
| `get_state` | 读取当前配置 |
| `configure_scope` | 生成/下发配置（双层钳制校验） |
| `acquire` | 采集波形样本 |

接入方式：
- **stdio**：`python agent\mcp_server.py`（或 `scopeagent-sidecar.exe --mcp`）
- **HTTP**：`POST http://127.0.0.1:8765/mcp`

客户端配置示例见 `agent/mcp_config.example.json`：
```json
{
  "mcpServers": {
    "scopeagent": {
      "command": "C:\\path\\to\\scopeagent-sidecar.exe",
      "args": ["--mcp"]
    }
  }
}
```

## 技术栈

- GUI：Qt 5.12 Widgets + QPainter（自绘示波器，无第三方 UI 依赖）
- Agent：DeepSeek `deepseek-v4-pro`（function calling，OpenAI 兼容）
- Sidecar：Python 3 纯标准库，打包为**独立 exe**（PyInstaller，运行时无需 Python）
- 仪器驱动：虚拟合成 + **SCPI/VISA**（动态加载 `visa64.dll`/`visa32.dll`）
- 构建：qmake + MinGW 7.3.0

详见 `PR.md` 与 `docs/`。

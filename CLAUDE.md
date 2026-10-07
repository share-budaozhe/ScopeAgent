# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

ScopeAgent is an LLM-driven virtual oscilloscope MVP: a Qt 5.12 C++ GUI host that a user controls with natural language, backed by a Python "sidecar" that calls the DeepSeek API (OpenAI-compatible function calling) and returns a validated scope configuration. The two processes communicate over local HTTP/JSON on `127.0.0.1:8765`. The same sidecar is also exposed as an MCP server. It additionally ships a local RAG knowledge base (import `.md`/`.pdf`, embed locally, semantic search) that the Agent can query before answering.

Two delivery models coexist: the source tree (`app/` C++ + `agent/` Python) and a pre-built `dist/` directory (exe + Qt DLLs + bundled sidecar). `build/` and `dist/` are generated and gitignored.

## Commands

All scripts are Windows batch files that assume Qt 5.12.2 is installed at `C:\Qt\Qt5.12.2` with the `mingw73_64` kit (paths are hardcoded in each script's `QT_BIN`/`MINGW_BIN`/`QT_INC`/`QT_LIB` — edit them if Qt lives elsewhere). Run them from the repo root.

- **Build**: `scripts\build.bat` — runs `qmake ..\app\ScopeAgent.pro` then `mingw32-make -j4`. Output: `build\release\ScopeAgent.exe`.
- **Run (dev)**: `scripts\run.bat` — launches `build\release\ScopeAgent.exe` (requires a prior build).
- **Test**: `scripts\test.bat` — 6-stage suite, aborts on first failure (details below).
- **Package**: `scripts\package.bat` — `windeployqt` + optional PyInstaller sidecar → `dist/`.
- **Run (packaged)**: `dist\run.bat` — uses bundled `agent\scopeagent-sidecar.exe` if present, else falls back to `python`.

Single tests (no build needed, no network):

- `python agent\schema_test.py` — DeviceConfig clamp/inherit/enum whitelist.
- `python agent\mcp_test.py` — MCP initialize/tools-list/tools-call + stdio round-trip.
- `python agent\knowledge_test.py` — chunking + HashEmbedder embed/search + import/list/remove/search full chain (no network).

Sidecar run modes (Python 3.8+; the core is pure-stdlib — RAG PDF/ONNX features need the deps in `agent/requirements.txt` and fall back to the pure-stdlib `HashEmbedder` when missing):

- `python agent\server.py [--port 8765] [--host 127.0.0.1]` — HTTP server (`/health`, `/v1/configure`, `/mcp`, plus KB endpoints `/v1/kb/import|list|remove|stats`).
- `python agent\server.py --mcp` or `python agent\mcp_server.py` — MCP stdio server.

The C++ tests in `tests/` are not part of the qmake project; `test.bat` compiles each one directly with `g++` and `moc` (any test touching a `Q_OBJECT` class — `waveformwidget.h`, `agentclient.h` — must be moc'd first). Stage 6 (`test_agent.cpp`) is the only networked test; it is auto-skipped unless `DEEPSEEK_API_KEY` is set.

## Architecture

**Process model**: Qt C++ host (`app/`) + Python sidecar (`agent/`) as independent processes. The C++ side never imports Python; it spawns the sidecar and talks HTTP.

- `app/src/agentclient.cpp` (`AgentClient`) spawns the sidecar via `QProcess` (prefers bundled `agent/scopeagent-sidecar.exe`, else `python server.py`), polls `/health` until ready, then POSTs `/v1/configure`. All async via `QNetworkAccessManager` so the UI never blocks.

**The DeviceConfig contract is the spine of the system**, and it is defined in two places that must be kept in sync:

- `agent/schema.py` — `CONFIG_FIELDS` (type/range/default/enum), `SCOPE_SPEC`, `validate_config()`.
- `app/src/deviceconfig.h` / `.cpp` — `struct DeviceConfig`, `DeviceConfig::clamped()`, JSON (de)serialization.

Adding/renaming a config field requires editing both, plus the JSON wire format uses snake_case keys (`frequency_hz`, `amplitude_vpp`, …) while the C++ struct uses camelCase members.

**Trust model — double-layer clamping**: LLM output is never trusted. `agent/schema.py::validate_config()` clamps the model's `configure_scope` arguments (range clamp + enum whitelist, missing fields inherited from current state), and `DeviceConfig::clamped()` re-clamps on the Qt side as a final backstop. Qt holds the single source of truth; the Agent only *suggests*.

**Driver abstraction**: `IDriver` (declared in `deviceconfig.h`) is the pluggable interface — `state()` / `apply()` / `acquire()` / `capabilities()` / `connected()`. Two implementations in `app/drivers/`:

- `VirtualScopeDriver` — synthesizes sine/square/triangle/sawtooth/noise with trigger-phase alignment.
- `ScpiDriver` — real instrument skeleton: loads `visa64.dll`/`visa32.dll` at runtime (no link-time VISA dependency), sends SCPI commands; `acquire()` is a stub that returns a zero line.

`MainWindow` (`app/src/mainwindow.cpp`) wires the UI to the current `IDriver` and the `AgentClient`. Layout: a top bar; a horizontal splitter (`WaveformWidget` | right control panel wrapped in a `QScrollArea` — readouts, 2-column manual settings, SCPI, knowledge base); and a bottom splitter with a left **对话** chat window (NL input + history) and a right **输出结果** window where each agent response is rendered as a formatted block (config table / rationale / tool calls). `WaveformWidget` paints grid/trace/trigger/labels with `QPainter` on a `QTimer`.

**Sidecar internals** (`agent/`): `server.py` (HTTP + config loading) → `agent.py` (`run_agent`, function-calling loop up to `MAX_TURNS`, system prompt) → `deepseek_client.py` (urllib client) → `tools.py` (`get_spec` / `get_state` / `configure_scope` / `search_knowledge`). `mcp_server.py` wraps the same tools as MCP JSON-RPC (stdio + the `/mcp` HTTP endpoint) and adds `acquire` + `import_knowledge`.

**RAG knowledge base** (`agent/embedder.py` + `agent/knowledge.py`): import `.md`/`.pdf` (via `pypdf`), chunk text (`size=500`, `overlap=50`), embed, and cosine-search. `get_embedder()` picks the backend from `config.json`'s `embedding` section — `OnnxEmbedder` (local ONNX, e.g. bge-small, 512-dim, lazy-imported `onnxruntime`/`tokenizers`) with a pure-stdlib `HashEmbedder` (char-bigram feature hashing) fallback when the model/deps are missing. `KnowledgeBase` persists to `<app_dir>/knowledge/index.json` + `chunks/<doc_id>.json` and dedups by path hash. The Agent only uses it as extra context (`search_knowledge`) — the double-layer clamping trust model is unchanged.

**Config precedence**: env vars `DEEPSEEK_API_KEY` / `DEEPSEEK_MODEL` / `DEEPSEEK_BASE_URL` override `agent/config.json`. Missing key → `/v1/configure` returns 503 and the UI falls back to manual-only operation (offline mode is a supported state, not an error).

## Gotchas

- **Two source-of-truth copies of waveform synthesis**: `VirtualScopeDriver::acquire()` (C++) and `mcp_server.py::synth_samples()` (Python) implement the same waveforms + trigger-phase alignment independently, so MCP `acquire` matches the GUI. Change one, change the other.
- Build is **qmake + MinGW 7.3.0, C++14** (`CONFIG += c++14`), not CMake/MSVC. Qt Creator also produces a shadow build dir (`build-ScopeAgent-…-Debug`); the canonical build dir is `build/`.
- Qt 5.12 lacks `QNetworkReply::setTransferTimeout` — `agentclient.cpp` uses a manual single-shot `QTimer` to abort hung health-check requests.
- `findAgentDir()` (in `agentclient.cpp`) locates the sidecar by searching for `agent/server.py` relative to the exe dir and cwd — relevant if you move the sidecar.
- Source files are UTF-8 with Chinese strings; the `.pro` sets `-utf-8` for MSVC, and all Python files are `utf-8` encoded.
- The `scripts/*.bat` files must use **CRLF** line endings — cmd.exe misparses LF-only batch files (garbled errors like `'make.exe' 不是内部或外部命令`). Convert byte-level (`\n`→`\r\n`, preserving UTF-8) if they ever get re-saved as LF. `build.bat`'s trailing `echo` lines with Chinese full-width brackets also print a harmless `'.bat'` warning under the GBK codepage; cosmetic only.
- The embedding model (`agent/models/embedding.onnx` + `tokenizer.json`) is not committed/downloaded by default; without it the KB still works via `HashEmbedder` (lexical-ish, non-semantic). Deps: `pypdf`, `onnxruntime`, `numpy`, `tokenizers` (`agent/requirements.txt`).
- `dist/agent/config.json` ships with an empty `deepseek_api_key`; `agent/config.example.json` is the template. Don't commit a real key (though this repo has no git history).

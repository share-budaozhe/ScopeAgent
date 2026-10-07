# -*- coding: utf-8 -*-
"""
mcp_server.py — MCP (Model Context Protocol) 服务端

把示波器能力标准化为 MCP 工具，供任意 MCP 客户端（Cursor / Claude Desktop /
其他 Agent 框架）发现与调用。

协议：JSON-RPC 2.0
  - stdio 模式：python mcp_server.py           （每行一个 JSON-RPC 消息）
  - HTTP 模式 ：server.py 的 POST /mcp 端点    （streamable HTTP，JSON 响应）

暴露的工具：
  get_spec          查询示波器能力规格
  get_state         读取当前配置
  configure_scope   生成/下发配置（含 rationale，双层钳制校验）
  acquire           采集当前配置下的波形样本（虚拟合成）
  search_knowledge  检索知识库中的相关文档片段（RAG）
  import_knowledge  导入 .md/.pdf 文档到知识库
"""
import json
import math
import random
import sys
import time

from schema import SCOPE_SPEC, default_config, validate_config, config_summary
from tools import build_tools, ToolContext

PROTOCOL_VERSION = "2024-11-05"
SERVER_NAME = "scopeagent"
SERVER_VERSION = "0.1.0"


# ---------- 虚拟波形合成（与 C++ VirtualScopeDriver 行为一致） ----------
def synth_samples(cfg, points=1000):
    A = cfg["amplitude_vpp"] / 2.0
    off = cfg["dc_offset_v"]
    f = max(cfg["frequency_hz"], 1e-9)
    span = cfg["sec_per_div"] * 10.0
    dt = span / max(points, 1)
    period = 1.0 / f
    wf = cfg["waveform"]

    def core(t):
        ph = 2.0 * math.pi * f * t
        if wf == "sine":
            return A * math.sin(ph)
        if wf == "square":
            return A * (1.0 if math.sin(ph) >= 0 else -1.0)
        if wf == "triangle":
            return A * (2.0 / math.pi) * math.asin(math.sin(ph))
        if wf == "sawtooth":
            return A * (2.0 * (f * t - math.floor(f * t + 0.5)))
        return 0.0

    # 触发相位对齐
    trig = cfg["trigger_level_v"]
    rising = cfg["trigger_slope"] == "rising"
    t0 = None
    N = 4000
    pdt = period / N
    for i in range(N):
        t = i * pdt
        v1 = off + core(t)
        v2 = off + core(t + pdt)
        if rising and v1 <= trig < v2:
            t0 = t
            break
        if (not rising) and v1 >= trig > v2:
            t0 = t
            break

    if t0 is None:
        # 未触发：触发电平超出信号范围，自由运行（相位随时间漂移，模拟未锁定）
        t0 = time.time() % period

    rnd = random.Random(12345)
    noise = A * cfg["noise_level"]
    out = []
    for i in range(points):
        t = t0 + i * dt
        v = off + core(t)
        if noise > 0.0:
            v += noise * rnd.gauss(0.0, 1.0)
        out.append(round(v, 6))
    return out


def build_mcp_tools():
    # OpenAI function-calling 格式 -> MCP 扁平格式
    tools = []
    for t in build_tools():
        fn = t["function"]
        tools.append({"name": fn["name"], "description": fn["description"],
                      "inputSchema": fn["parameters"]})
    tools.append({
        "name": "acquire",
        "description": "采集当前配置下的波形样本，返回指定数量的电压值数组（虚拟合成）。",
        "inputSchema": {
            "type": "object",
            "properties": {"points": {"type": "integer", "minimum": 10, "maximum": 100000, "default": 1000}},
        },
    })
    tools.append({
        "name": "import_knowledge",
        "description": "导入 .md 或 .pdf 文档到知识库（用于 RAG 检索）。返回文档元信息。",
        "inputSchema": {
            "type": "object",
            "properties": {"path": {"type": "string", "description": "本地 .md 或 .pdf 文件路径"}},
            "required": ["path"],
        },
    })
    return tools


class McpServer:
    def __init__(self, kb=None):
        self.ctx = ToolContext(default_config(), kb=kb)
        self.tools = build_mcp_tools()
        self._tool_map = {t["name"]: t for t in self.tools}
        self.kb = kb

    # ---------- 工具分发 ----------
    def _call_tool(self, name, arguments):
        if name in ("get_spec", "get_state", "configure_scope", "search_knowledge"):
            return self.ctx.execute(name, arguments)
        if name == "acquire":
            points = int(arguments.get("points", 1000))
            points = max(10, min(100000, points))
            samples = synth_samples(self.ctx.state, points)
            return {"ok": True, "count": len(samples), "samples": samples,
                    "summary": config_summary(self.ctx.state)}
        if name == "import_knowledge":
            if self.kb is None:
                return {"ok": False, "error": "MCP stdio 模式未接入知识库（请通过 sidecar HTTP 导入）"}
            path = (arguments.get("path") or "").strip()
            if not path:
                return {"ok": False, "error": "缺少 path 参数"}
            try:
                doc = self.kb.import_file(path)
            except Exception as e:  # noqa
                return {"ok": False, "error": str(e)}
            return {"ok": True, "doc": doc}
        return {"ok": False, "error": "未知工具: %s" % name}

    # ---------- JSON-RPC 请求处理 ----------
    def handle(self, msg):
        """处理单条 JSON-RPC 消息，返回响应 dict（通知返回 None）。"""
        method = msg.get("method", "")
        rid = msg.get("id")
        params = msg.get("params") or {}

        if method == "initialize":
            return {"jsonrpc": "2.0", "id": rid, "result": {
                "protocolVersion": PROTOCOL_VERSION,
                "capabilities": {"tools": {}},
                "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION},
            }}
        if method == "notifications/initialized":
            return None
        if method == "ping":
            return {"jsonrpc": "2.0", "id": rid, "result": {}}
        if method == "tools/list":
            return {"jsonrpc": "2.0", "id": rid, "result": {"tools": self.tools}}
        if method == "tools/call":
            name = params.get("name", "")
            args = params.get("arguments") or {}
            result = self._call_tool(name, args)
            return {"jsonrpc": "2.0", "id": rid, "result": {
                "content": [{"type": "text", "text": json.dumps(result, ensure_ascii=False)}],
                "isError": not result.get("ok", True),
            }}
        return {"jsonrpc": "2.0", "id": rid, "error": {"code": -32601, "message": "方法未找到: %s" % method}}

    # ---------- stdio 模式 ----------
    def run_stdio(self):
        for line in sys.stdin:
            line = line.strip()
            if not line:
                continue
            try:
                msg = json.loads(line)
            except json.JSONDecodeError:
                continue
            resp = self.handle(msg)
            if resp is not None:
                sys.stdout.write(json.dumps(resp, ensure_ascii=False) + "\n")
                sys.stdout.flush()

    # ---------- HTTP 模式（供 server.py 的 /mcp 端点调用） ----------
    def handle_http(self, body_bytes):
        try:
            msg = json.loads(body_bytes.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            return 400, {"jsonrpc": "2.0", "id": None,
                         "error": {"code": -32700, "message": "无效 JSON"}}
        resp = self.handle(msg)
        if resp is None:  # 通知无需响应
            return 202, None
        return 200, resp


MCP = McpServer()


def main():
    print("ScopeAgent MCP server (stdio) v%s" % SERVER_VERSION, file=sys.stderr, flush=True)
    MCP.run_stdio()


if __name__ == "__main__":
    main()

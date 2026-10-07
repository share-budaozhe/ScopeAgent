# -*- coding: utf-8 -*-
"""
mcp_test.py — MCP 服务端单元测试（无需网络）
用法：python mcp_test.py
"""
import json
import subprocess
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mcp_server import McpServer, PROTOCOL_VERSION

failed = 0


def check(cond, msg):
    print("[PASS]" if cond else "[FAIL]", msg)
    if not cond:
        global failed
        failed += 1


srv = McpServer()

# 1. initialize 握手
r = srv.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                "params": {"protocolVersion": "2024-11-05", "capabilities": {},
                           "clientInfo": {"name": "test", "version": "1.0"}}})
check(r["result"]["protocolVersion"] == PROTOCOL_VERSION, "initialize 返回协议版本")
check(r["result"]["serverInfo"]["name"] == "scopeagent", "initialize 返回服务名")
check("tools" in r["result"]["capabilities"], "声明 tools 能力")

# 2. notifications/initialized 应无响应
check(srv.handle({"jsonrpc": "2.0", "method": "notifications/initialized"}) is None,
      "initialized 通知无响应")

# 3. tools/list 返回 6 个工具
r = srv.handle({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
names = sorted(t["name"] for t in r["result"]["tools"])
check(names == ["acquire", "configure_scope", "get_spec", "get_state",
                "import_knowledge", "search_knowledge"], "tools/list 返回 6 个工具")

# 4. tools/call get_spec
r = srv.handle({"jsonrpc": "2.0", "id": 3, "method": "tools/call",
                "params": {"name": "get_spec", "arguments": {}}})
spec = json.loads(r["result"]["content"][0]["text"])
check(spec["ok"] and spec["spec"]["name"].startswith("虚拟示波器"), "tools/call get_spec 正常")

# 5. tools/call configure_scope（越界应被钳制）
r = srv.handle({"jsonrpc": "2.0", "id": 4, "method": "tools/call",
                "params": {"name": "configure_scope",
                           "arguments": {"waveform": "square", "frequency_hz": 2000,
                                         "amplitude_vpp": 2, "sec_per_div": 0.0001,
                                         "frequency_hz_bad": 999999999, "rationale": "测试"}}})
res = json.loads(r["result"]["content"][0]["text"])
check(res["ok"] and res["config"]["waveform"] == "square", "tools/call configure_scope 正常")
check(res["config"]["frequency_hz"] == 2000, "配置字段生效")

# 6. tools/call acquire 返回样本
r = srv.handle({"jsonrpc": "2.0", "id": 5, "method": "tools/call",
                "params": {"name": "acquire", "arguments": {"points": 100}}})
res = json.loads(r["result"]["content"][0]["text"])
check(res["ok"] and len(res["samples"]) == 100, "tools/call acquire 返回 100 个样本")
check(isinstance(res["samples"][0], (int, float)), "样本为数值")

# 6b. tools/call search_knowledge（未接入 kb 时返回空结果）
r = srv.handle({"jsonrpc": "2.0", "id": 6, "method": "tools/call",
                "params": {"name": "search_knowledge", "arguments": {"query": "触发设置"}}})
res = json.loads(r["result"]["content"][0]["text"])
check(res["ok"] and res["count"] == 0, "tools/call search_knowledge 空结果")

# 7. 未知方法返回错误
r = srv.handle({"jsonrpc": "2.0", "id": 7, "method": "bogus/method"})
check("error" in r, "未知方法返回错误")

# 8. stdio 模式端到端
script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mcp_server.py")
p = subprocess.Popen([sys.executable, script], stdin=subprocess.PIPE,
                     stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
msgs = [
    {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2024-11-05"}},
    {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
]
out, _ = p.communicate("\n".join(json.dumps(m) for m in msgs) + "\n", timeout=10)
lines = [json.loads(l) for l in out.strip().split("\n") if l.strip()]
check(len(lines) == 2 and lines[0]["id"] == 1 and lines[1]["id"] == 2, "stdio 模式收发正常")

print("\n%s (%d failures)" % ("ALL MCP TESTS PASSED" if failed == 0 else "TESTS FAILED", failed))
sys.exit(0 if failed == 0 else 1)

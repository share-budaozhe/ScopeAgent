# -*- coding: utf-8 -*-
"""
server.py — Agent Sidecar HTTP 服务（纯标准库）

接口：
  GET  /health          健康检查
  POST /v1/configure    自然语言 → 配置

启动：python server.py [--port 8765] [--host 127.0.0.1]
配置：优先读环境变量 DEEPSEEK_API_KEY，其次 agent/config.json。
"""
import argparse
import json
import os
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from deepseek_client import DeepSeekClient, DeepSeekError, DEFAULT_MODEL, DEFAULT_BASE_URL
from agent import run_agent
from schema import validate_config, config_summary
from mcp_server import MCP
from embedder import get_embedder
from knowledge import KnowledgeBase

VERSION = "0.1.0"
SERVER = None  # 供 /shutdown 与单测使用


def _app_dir():
    """返回配置文件所在目录：冻结后为 exe 同目录，否则为脚本目录。"""
    if getattr(sys, "frozen", False):
        return os.path.dirname(os.path.abspath(sys.executable))
    return os.path.dirname(os.path.abspath(__file__))


def load_settings():
    """加载 API Key / 模型 / base_url / embedding 配置。"""
    cfg = {"api_key": os.environ.get("DEEPSEEK_API_KEY", ""),
           "model": os.environ.get("DEEPSEEK_MODEL", DEFAULT_MODEL),
           "base_url": os.environ.get("DEEPSEEK_BASE_URL", DEFAULT_BASE_URL),
           "embedding": {}}

    cfg_path = os.path.join(_app_dir(), "config.json")
    if os.path.exists(cfg_path):
        try:
            with open(cfg_path, "r", encoding="utf-8") as f:
                file_cfg = json.load(f)
            cfg["api_key"] = file_cfg.get("deepseek_api_key") or cfg["api_key"]
            cfg["model"] = file_cfg.get("model") or cfg["model"]
            cfg["base_url"] = file_cfg.get("base_url") or cfg["base_url"]
            if isinstance(file_cfg.get("embedding"), dict):
                cfg["embedding"] = file_cfg["embedding"]
        except Exception as e:  # noqa
            print("[warn] 读取 config.json 失败: %s" % e)
    return cfg


SETTINGS = load_settings()
EMBEDDER = get_embedder(SETTINGS.get("embedding"), _app_dir())
KB = KnowledgeBase(EMBEDDER, _app_dir())
MCP.kb = KB


class Handler(BaseHTTPRequestHandler):
    server_version = "ScopeAgent/" + VERSION

    def _send_json(self, code, obj):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _read_json(self):
        """读取请求体并解析 JSON，失败返回 None。"""
        length = int(self.headers.get("Content-Length", 0))
        try:
            return json.loads(self.rfile.read(length).decode("utf-8"))
        except Exception:
            return None

    def do_GET(self):
        if self.path.rstrip("/") == "/health":
            ready = bool(SETTINGS.get("api_key"))
            self._send_json(200, {
                "ok": True,
                "ready": ready,
                "version": VERSION,
                "model": SETTINGS.get("model"),
                "base_url": SETTINGS.get("base_url"),
            })
        elif self.path.rstrip("/") == "/v1/kb/list":
            self._send_json(200, {"ok": True, "documents": KB.list_documents()})
        elif self.path.rstrip("/") == "/v1/kb/stats":
            self._send_json(200, {"ok": True, **KB.stats()})
        else:
            self._send_json(404, {"ok": False, "error": "not found"})

    def do_POST(self):
        # MCP 端点（JSON-RPC，供任意 MCP 客户端）
        if self.path.rstrip("/") == "/mcp":
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length)
            code, result = MCP.handle_http(body)
            if result is None:
                self.send_response(code)
                self.send_header("Content-Length", "0")
                self.end_headers()
            else:
                self._send_json(code, result)
            return

        # 知识库：导入 / 删除（离线可用，不依赖 DeepSeek key）
        if self.path.rstrip("/") == "/v1/kb/import":
            req = self._read_json()
            if req is None:
                self._send_json(400, {"ok": False, "error": "请求体必须是合法 JSON"})
                return
            paths = req.get("paths")
            if paths is None:
                paths = [req["path"]] if req.get("path") else []
            if not paths:
                self._send_json(400, {"ok": False, "error": "缺少 path 或 paths 字段"})
                return
            imported, errors = [], []
            for p in paths:
                try:
                    imported.append(KB.import_file(p))
                except Exception as e:  # noqa
                    errors.append({"path": p, "error": str(e)})
            self._send_json(200, {"ok": len(errors) == 0, "imported": imported,
                                  "errors": errors, "stats": KB.stats()})
            return

        if self.path.rstrip("/") == "/v1/kb/remove":
            req = self._read_json() or {}
            doc_id = (req.get("id") or "").strip()
            if not doc_id:
                self._send_json(400, {"ok": False, "error": "缺少 id 字段"})
                return
            try:
                self._send_json(200, {"ok": True, **KB.remove_document(doc_id)})
            except Exception as e:  # noqa
                self._send_json(404, {"ok": False, "error": str(e)})
            return

        if self.path.rstrip("/") != "/v1/configure":
            self._send_json(404, {"ok": False, "error": "not found: " + self.path})
            return

        length = int(self.headers.get("Content-Length", 0))
        try:
            req = json.loads(self.rfile.read(length).decode("utf-8"))
        except Exception:
            self._send_json(400, {"ok": False, "error": "请求体必须是合法 JSON"})
            return

        intent = (req.get("intent") or "").strip()
        if not intent:
            self._send_json(400, {"ok": False, "error": "缺少 intent 字段"})
            return

        # 先对上报的状态做一次钳制，保证 Agent 输入合法
        state, _ = validate_config(req.get("state") or {})

        if not SETTINGS.get("api_key"):
            self._send_json(503, {"ok": False, "error": "未配置 DEEPSEEK_API_KEY，请在环境变量或 agent/config.json 中设置"})
            return

        try:
            client = DeepSeekClient(SETTINGS["api_key"], SETTINGS["base_url"], SETTINGS["model"])
            result = run_agent(intent, state, client, kb=KB)
            self._send_json(200, result)
        except DeepSeekError as e:
            self._send_json(502, {"ok": False, "error": str(e)})
        except Exception as e:  # noqa
            self._send_json(500, {"ok": False, "error": "内部错误: %s" % e})

    def log_message(self, fmt, *args):
        sys.stderr.write("[http] %s\n" % (fmt % args))


def main():
    global SERVER
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--mcp", action="store_true", help="以 MCP stdio 模式运行（供 MCP 客户端直接拉起）")
    args = ap.parse_args()

    if args.mcp:
        print("ScopeAgent MCP server (stdio) v%s" % VERSION, file=sys.stderr, flush=True)
        MCP.run_stdio()
        return

    SERVER = ThreadingHTTPServer((args.host, args.port), Handler)
    print("ScopeAgent sidecar v%s listening on http://%s:%d (model=%s)" %
          (VERSION, args.host, args.port, SETTINGS.get("model")), flush=True)
    if not SETTINGS.get("api_key"):
        print("[warn] 未检测到 DEEPSEEK_API_KEY，/v1/configure 将返回 503", flush=True)
    try:
        SERVER.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        SERVER.server_close()


if __name__ == "__main__":
    main()

# -*- coding: utf-8 -*-
"""
tools.py — Agent 工具集定义与执行（function calling）

工具：
  get_spec          查询示波器能力规格（预研用）
  get_state         读取当前配置
  configure_scope   生成/下发最终配置（含 rationale）
  search_knowledge  检索知识库中的相关文档片段（RAG）
"""
import json
from schema import SCOPE_SPEC, CONFIG_FIELDS, validate_config, config_summary

# 传给 LLM 的工具 JSON Schema
def build_tools():
    props = {}
    for name, spec in CONFIG_FIELDS.items():
        if spec["type"] == "enum":
            props[name] = {"type": "string", "enum": spec["values"]}
        else:
            props[name] = {"type": "number", "minimum": spec["min"], "maximum": spec["max"]}

    return [
        {
            "type": "function",
            "function": {
                "name": "get_spec",
                "description": "查询示波器能力规格（带宽、采样率、可选波形、各档位范围等）。在给出配置建议前应先调用本工具了解设备能力。",
                "parameters": {"type": "object", "properties": {}, "required": []},
            },
        },
        {
            "type": "function",
            "function": {
                "name": "get_state",
                "description": "读取示波器当前配置状态。",
                "parameters": {"type": "object", "properties": {}, "required": []},
            },
        },
        {
            "type": "function",
            "function": {
                "name": "configure_scope",
                "description": "生成并下发示波器最终配置。根据用户测量意图设置波形、频率、幅值、时基、垂直档位、触发等。未指定的参数保持当前值或取默认值。必须提供 rationale 说明配置理由。",
                "parameters": {
                    "type": "object",
                    "properties": {
                        **props,
                        "rationale": {
                            "type": "string",
                            "description": "配置理由，用简洁中文说明为什么这样设置（例如根据信号频率计算时基使屏幕显示1~2个周期）",
                        },
                    },
                    "required": ["rationale"],
                },
            },
        },
        {
            "type": "function",
            "function": {
                "name": "search_knowledge",
                "description": "检索知识库中与查询最相关的文档片段。当用户的问题可能涉及已导入的手册/应用笔记时，先调用本工具获取背景知识，再基于检索结果作答。",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "query": {
                            "type": "string",
                            "description": "检索查询，用关键词或自然语言描述想查找的内容",
                        },
                    },
                    "required": ["query"],
                },
            },
        },
    ]


class ToolContext:
    """工具执行上下文：持有当前状态，记录最终配置。"""

    def __init__(self, state, kb=None):
        self.state = dict(state)
        self.final_config = None
        self.spec = SCOPE_SPEC
        self.calls = []
        self.kb = kb

    def execute(self, name, arguments):
        self.calls.append(name)
        if name == "get_spec":
            return {"ok": True, "spec": self.spec}
        if name == "get_state":
            return {"ok": True, "state": self.state, "summary": config_summary(self.state)}
        if name == "configure_scope":
            cfg, warnings = validate_config(arguments, base=self.state)
            self.final_config = cfg
            self.state = dict(cfg)
            return {"ok": True, "config": cfg, "summary": config_summary(cfg), "warnings": warnings}
        if name == "search_knowledge":
            query = (arguments.get("query") or "").strip()
            if not query:
                return {"ok": False, "error": "缺少 query 参数"}
            if self.kb is None:
                return {"ok": True, "results": [], "count": 0, "empty": True}
            try:
                results = self.kb.search(query)
            except Exception as e:  # noqa
                return {"ok": False, "error": "检索失败：%s" % e}
            return {"ok": True, "results": results, "count": len(results)}
        return {"ok": False, "error": "未知工具: %s" % name}


def execute_tool_calls(tool_calls, ctx):
    """执行一批 tool_calls，返回 OpenAI 格式的 tool 消息列表。"""
    results = []
    for tc in tool_calls:
        fn = tc.get("function", {})
        name = fn.get("name", "")
        try:
            arguments = json.loads(fn.get("arguments") or "{}")
        except json.JSONDecodeError:
            arguments = {}
        result = ctx.execute(name, arguments)
        results.append({
            "role": "tool",
            "tool_call_id": tc.get("id", ""),
            "content": json.dumps(result, ensure_ascii=False),
        })
    return results

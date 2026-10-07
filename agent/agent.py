# -*- coding: utf-8 -*-
"""
agent.py — Function-Calling Agent 循环

输入：用户意图（自然语言） + 当前状态
输出：{config, rationale, confidence, model, tool_calls}
"""
import json
from deepseek_client import DeepSeekClient
from tools import build_tools, ToolContext, execute_tool_calls
from schema import validate_config, config_summary

SYSTEM_PROMPT = (
    "你是一名专业的示波器测量配置专家。用户会用自然语言描述测量意图，"
    "你需要：\n"
    "1) 先调用 get_spec 了解设备能力；\n"
    "2) 调用 get_state 了解当前配置；\n"
    "3) 根据测量意图与信号特性（频率、幅度、波形）计算出合理的时基、垂直档位、触发电平等参数，"
    "   例如：时基 = 周期 / 目标周期数（屏幕上显示 1~2 个完整周期为宜）；"
    "   垂直档位 = 峰峰值 / (2~4 格)；触发电平设在波形中段；\n"
    "4) 最后调用 configure_scope 下发完整配置，并在 rationale 中用简洁中文说明理由。\n"
    "只做配置生成，不要虚构设备不存在的能力。"
)

MAX_TURNS = 6


def run_agent(intent, state, client: DeepSeekClient, kb=None):
    """执行 Agent 循环，返回结果 dict。kb 非空时接入 RAG 知识库检索。"""
    tools = build_tools()
    ctx = ToolContext(state, kb=kb)

    kb_has_docs = kb is not None and kb.stats().get("documents", 0) > 0
    system_prompt = SYSTEM_PROMPT
    if kb_has_docs:
        system_prompt += (
            "\n此外，本设备已接入知识库（含用户导入的示波器手册/应用笔记）。"
            "当用户的问题可能涉及文档知识时，先调用 search_knowledge 检索相关片段，"
            "再结合检索结果作答并给出配置。"
        )

    messages = [
        {"role": "system", "content": system_prompt},
        {"role": "user", "content": "测量意图：%s\n当前配置：%s" % (intent, config_summary(ctx.state))},
    ]

    final_text = ""
    model = client.model
    for _ in range(MAX_TURNS):
        msg, model = client.chat(messages, tools=tools)
        content = msg.get("content") or ""

        # 回填 assistant 消息
        assistant = {"role": "assistant", "content": content}
        tool_calls = msg.get("tool_calls")
        if tool_calls:
            assistant["tool_calls"] = tool_calls
        messages.append(assistant)

        if tool_calls:
            tool_msgs = execute_tool_calls(tool_calls, ctx)
            messages.extend(tool_msgs)
            continue

        # 无工具调用，视为最终回答
        final_text = content
        break

    # 若 Agent 未调用 configure_scope，则用当前状态兜底（尽量不改动）
    if ctx.final_config is None:
        ctx.final_config, _ = validate_config({}, base=ctx.state)

    rationale = final_text.strip() or "（Agent 未给出文字说明）"
    confidence = 0.9 if "configure_scope" in ctx.calls else 0.4

    return {
        "ok": True,
        "config": ctx.final_config,
        "rationale": rationale,
        "confidence": round(confidence, 2),
        "model": model,
        "tool_calls": ctx.calls,
    }

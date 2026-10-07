# -*- coding: utf-8 -*-
"""
deepseek_client.py — DeepSeek API 客户端（OpenAI 兼容，纯标准库 urllib）

支持 function calling 与 reasoning_content 的透传。
"""
import json
import urllib.request
import urllib.error

DEFAULT_BASE_URL = "https://api.deepseek.com"
DEFAULT_MODEL = "deepseek-v4-pro"


class DeepSeekError(Exception):
    pass


class DeepSeekClient:
    def __init__(self, api_key, base_url=DEFAULT_BASE_URL, model=DEFAULT_MODEL, timeout=120):
        if not api_key:
            raise DeepSeekError("缺少 DEEPSEEK_API_KEY：请设置环境变量或在 agent/config.json 中配置")
        self.api_key = api_key
        self.base_url = base_url.rstrip("/")
        self.model = model
        self.timeout = timeout

    def chat(self, messages, tools=None, temperature=0.2, max_tokens=2000):
        """发送一次对话，返回 choices[0].message（dict）。"""
        payload = {
            "model": self.model,
            "messages": messages,
            "temperature": temperature,
            "max_tokens": max_tokens,
            "stream": False,
        }
        if tools:
            payload["tools"] = tools
            payload["tool_choice"] = "auto"

        url = self.base_url + "/chat/completions"
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        req = urllib.request.Request(url, data=data, method="POST", headers={
            "Content-Type": "application/json",
            "Authorization": "Bearer " + self.api_key,
        })

        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                body = resp.read().decode("utf-8")
        except urllib.error.HTTPError as e:
            detail = e.read().decode("utf-8", "replace")
            raise DeepSeekError("DeepSeek HTTP %s: %s" % (e.code, detail[:300]))
        except urllib.error.URLError as e:
            raise DeepSeekError("无法连接 DeepSeek：%s" % e.reason)

        try:
            obj = json.loads(body)
        except json.JSONDecodeError:
            raise DeepSeekError("DeepSeek 返回非 JSON：%s" % body[:200])

        if "choices" not in obj or not obj["choices"]:
            raise DeepSeekError("DeepSeek 响应缺少 choices：%s" % body[:300])

        return obj["choices"][0]["message"], obj.get("model", self.model)

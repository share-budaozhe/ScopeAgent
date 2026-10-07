# -*- coding: utf-8 -*-
"""
embedder.py — 文本向量化（本地 ONNX 模型 + 纯标准库哈希兜底）

后端：
  OnnxEmbedder  —— onnxruntime + tokenizers，语义向量（默认，需模型文件）
  HashEmbedder  —— 字符 bigram 特征哈希，词法近似检索（无依赖，离线可用）

Embedding 依赖均为懒加载：模块顶层不 import 第三方库，避免 server.py
在缺库环境下直接崩溃。get_embedder() 会在 ONNX 不可用时自动回退 Hash。
"""
import math
import os
import sys

DEFAULT_DIM = 512
MAX_LEN = 512


def _l2_normalize(vec):
    norm = math.sqrt(sum(v * v for v in vec))
    if norm == 0.0:
        return [0.0] * len(vec)
    return [v / norm for v in vec]


class Embedder:
    """向量化抽象接口。"""
    dim = DEFAULT_DIM
    name = "embedder"

    def embed(self, texts):
        raise NotImplementedError


class HashEmbedder(Embedder):
    """纯标准库兜底：字符 bigram 特征哈希到固定维，退化为词法近似检索。"""

    def __init__(self, dim=DEFAULT_DIM):
        self.dim = dim
        self.name = "hash(bigram)"

    def _featurize(self, text):
        t = (text or "").lower()
        vec = [0.0] * self.dim
        # 单字符 + 字符 bigram，兼顾中英文
        grams = [t[i:i + 1] for i in range(len(t))]
        grams += [t[i:i + 2] for i in range(len(t) - 1)]
        for g in grams:
            h = 0
            for ch in g:
                h = (h * 31 + ord(ch)) & 0x7fffffff
            vec[h % self.dim] += 1.0
        return _l2_normalize(vec)

    def embed(self, texts):
        return [self._featurize(t) for t in texts]


class OnnxEmbedder(Embedder):
    """本地 ONNX 句子向量模型（mean-pool + L2 归一）。"""

    def __init__(self, model_dir, model_file="embedding.onnx",
                 tokenizer_file="tokenizer.json", dim=DEFAULT_DIM):
        try:
            import onnxruntime as ort  # noqa
            from tokenizers import Tokenizer  # noqa
        except ImportError as e:
            raise RuntimeError("缺少 onnxruntime/tokenizers 依赖：%s" % e)

        model_path = os.path.join(model_dir, model_file)
        tok_path = os.path.join(model_dir, tokenizer_file)
        if not os.path.exists(model_path) or not os.path.exists(tok_path):
            raise RuntimeError("embedding 模型文件缺失：%s" % model_path)

        self._session = ort.InferenceSession(model_path, providers=["CPUExecutionProvider"])
        self._tok = Tokenizer.from_file(tok_path)
        try:
            self._tok.enable_padding(pad_id=0, pad_token="[PAD]")
            self._tok.enable_truncation(max_length=MAX_LEN)
        except Exception:
            pass
        self.dim = dim
        self.name = "onnx(%s)" % os.path.basename(model_path)

    def embed(self, texts):
        import numpy as np  # noqa
        texts = [t or "" for t in texts]
        enc = self._tok.encode_batch(texts)
        input_ids = np.array([e.ids for e in enc], dtype=np.int64)
        attention_mask = np.array([e.attention_mask for e in enc], dtype=np.int64)

        feeds = {}
        for inp in self._session.get_inputs():
            if inp.name == "input_ids":
                feeds[inp.name] = input_ids
            elif inp.name == "attention_mask":
                feeds[inp.name] = attention_mask
            elif inp.name == "token_type_ids":
                feeds[inp.name] = np.zeros_like(input_ids)

        outputs = self._session.run(None, feeds)
        out = outputs[0]

        if out.ndim == 3:  # (batch, seq, hidden) → 按 attention mask 加权平均
            mask = attention_mask[..., None].astype(np.float32)
            summed = (out.astype(np.float32) * mask).sum(axis=1)
            counts = mask.sum(axis=1)
            counts = np.where(counts == 0, 1.0, counts)
            vec = summed / counts
        else:  # 已聚合的句子向量 (batch, hidden)
            vec = out.astype(np.float32)

        norms = np.linalg.norm(vec, axis=1, keepdims=True)
        norms = np.where(norms == 0, 1.0, norms)
        vec = vec / norms
        return [[round(float(x), 6) for x in row] for row in vec]


def get_embedder(cfg, base_dir=None):
    """按配置构造 embedder；onnx 后端不可用时回退 HashEmbedder。

    cfg:      config.json 中的 embedding 段
    base_dir: 应用目录（model_dir 为相对路径时据此解析）
    """
    cfg = cfg or {}
    try:
        dim = int(cfg.get("dim") or DEFAULT_DIM)
    except (TypeError, ValueError):
        dim = DEFAULT_DIM

    backend = str(cfg.get("backend") or "onnx").lower()
    if backend == "hash":
        return HashEmbedder(dim)

    model_dir = cfg.get("model_dir") or "models"
    if not os.path.isabs(model_dir) and base_dir:
        model_dir = os.path.join(base_dir, model_dir)
    try:
        return OnnxEmbedder(model_dir,
                            model_file=cfg.get("model_file") or "embedding.onnx",
                            tokenizer_file=cfg.get("tokenizer_file") or "tokenizer.json",
                            dim=dim)
    except Exception as e:  # noqa
        print("[warn] ONNX embedding 不可用，回退 HashEmbedder（词法检索）：%s" % e,
              file=sys.stderr, flush=True)
        return HashEmbedder(dim)

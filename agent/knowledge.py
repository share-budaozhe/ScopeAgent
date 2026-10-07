# -*- coding: utf-8 -*-
"""
knowledge.py — RAG 知识库（文档导入 / 分块 / 向量化 / 检索）

支持导入 .md 与 .pdf（PDF 依赖 pypdf，缺失时给出可读错误）。
持久化在 base_dir/knowledge/ 下：
  index.json              文档索引
  chunks/<doc_id>.json    分块文本 + 向量
"""
import hashlib
import json
import math
import os
import re
import threading
import time

CHUNK_SIZE = 500
CHUNK_OVERLAP = 50
TOP_K = 4


def extract_text(path):
    """按扩展名提取文档文本。"""
    ext = os.path.splitext(path)[1].lower()
    if ext == ".md":
        with open(path, "r", encoding="utf-8") as f:
            return f.read()
    if ext == ".pdf":
        try:
            from pypdf import PdfReader
        except ImportError:
            raise RuntimeError("缺少 pypdf 依赖，无法解析 PDF（pip install pypdf）")
        pages = []
        reader = PdfReader(path)
        for page in reader.pages:
            try:
                pages.append(page.extract_text() or "")
            except Exception:  # noqa 单页失败不阻塞整体
                pages.append("")
        return "\n".join(pages)
    raise RuntimeError("不支持的文件类型：%s（仅支持 .md / .pdf）" % (ext or "<无扩展名>"))


def chunk_text(text, size=CHUNK_SIZE, overlap=CHUNK_OVERLAP):
    """把文本切成约 size 字符的块，块间重叠 overlap 字符，优先按段落/句边界。"""
    text = re.sub(r"\r\n?", "\n", text)
    paras = [p.strip() for p in re.split(r"\n\s*\n", text) if p.strip()]
    if not paras:
        return []

    chunks = []
    for para in paras:
        if len(para) <= size:
            chunks.append(para)
            continue
        # 长段按句边界（中英文句末标点）切分
        sentences = re.split(r"(?<=[。！？；.!?;])", para)
        buf = ""
        for s in sentences:
            if not s:
                continue
            if len(buf) + len(s) > size and buf:
                chunks.append(buf.strip())
                buf = buf[-overlap:] + s if overlap and len(buf) > overlap else s
            else:
                buf += s
        if buf.strip():
            chunks.append(buf.strip())

    # 兜底：仍有超长块（无标点长文）按长度硬切
    out = []
    for c in chunks:
        while len(c) > size:
            out.append(c[:size])
            c = c[size - overlap:] if overlap else c[size:]
        if c.strip():
            out.append(c)
    return [c for c in out if c]


class KnowledgeBase:
    """文档知识库：导入 → 分块 → 向量化 → 检索。"""

    def __init__(self, embedder, base_dir):
        self.embedder = embedder
        self.root = os.path.join(base_dir, "knowledge")
        self._chunks_dir = os.path.join(self.root, "chunks")
        try:
            os.makedirs(self._chunks_dir, exist_ok=True)
        except OSError:
            pass
        self._lock = threading.Lock()
        self._index = self._load_index()

    # ---------- 持久化 ----------
    def _index_path(self):
        return os.path.join(self.root, "index.json")

    def _doc_path(self, doc_id):
        return os.path.join(self._chunks_dir, doc_id + ".json")

    def _load_index(self):
        try:
            with open(self._index_path(), "r", encoding="utf-8") as f:
                data = json.load(f)
            return data if isinstance(data, list) else []
        except (OSError, ValueError):
            return []

    def _save_index(self):
        tmp = self._index_path() + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(self._index, f, ensure_ascii=False, indent=2)
        os.replace(tmp, self._index_path())

    # ---------- 查询 ----------
    def list_documents(self):
        with self._lock:
            return list(self._index)

    def stats(self):
        with self._lock:
            return {"documents": len(self._index),
                    "chunks": sum(d.get("chunk_count", 0) for d in self._index)}

    # ---------- 导入 / 删除 ----------
    def import_file(self, path):
        path = os.path.abspath(path)
        if not os.path.exists(path):
            raise RuntimeError("文件不存在：%s" % path)
        doc_id = hashlib.sha1(path.encode("utf-8")).hexdigest()[:16]

        with self._lock:
            if any(d["id"] == doc_id for d in self._index):
                raise RuntimeError("文档已导入：%s" % os.path.basename(path))

        text = extract_text(path)
        if not text.strip():
            raise RuntimeError("未能从文档提取到文本")
        chunks = chunk_text(text)
        if not chunks:
            raise RuntimeError("文档分块后为空")

        try:
            vectors = self.embedder.embed(chunks)
        except Exception as e:  # noqa
            raise RuntimeError("向量化失败：%s" % e)

        doc = {
            "id": doc_id,
            "title": os.path.basename(path),
            "path": path,
            "file_type": os.path.splitext(path)[1].lower().lstrip("."),
            "chunk_count": len(chunks),
            "chars": len(text),
            "added_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
        }
        record = {
            "doc": doc,
            "chunks": [{"id": "%s-%d" % (doc_id, i), "text": c}
                       for i, c in enumerate(chunks)],
            "vectors": vectors,
        }

        with self._lock:
            try:
                with open(self._doc_path(doc_id), "w", encoding="utf-8") as f:
                    json.dump(record, f, ensure_ascii=False)
            except OSError as e:
                raise RuntimeError("写入知识库失败：%s" % e)
            self._index.append(doc)
            self._save_index()
        return doc

    def remove_document(self, doc_id):
        with self._lock:
            for i, d in enumerate(self._index):
                if d["id"] == doc_id:
                    self._index.pop(i)
                    break
            else:
                raise RuntimeError("文档不存在：%s" % doc_id)
            try:
                os.remove(self._doc_path(doc_id))
            except OSError:
                pass
            self._save_index()
        return {"removed": doc_id}

    # ---------- 检索 ----------
    def search(self, query, top_k=TOP_K):
        query = (query or "").strip()
        if not query:
            return []
        try:
            qvec = self.embedder.embed([query])[0]
        except Exception as e:  # noqa
            raise RuntimeError("查询向量化失败：%s" % e)

        results = []
        for d in list(self._index):
            try:
                with open(self._doc_path(d["id"]), "r", encoding="utf-8") as f:
                    rec = json.load(f)
            except (OSError, ValueError):
                continue
            for chunk, vec in zip(rec.get("chunks", []), rec.get("vectors", [])):
                results.append({
                    "doc_id": d["id"],
                    "title": d["title"],
                    "chunk_id": chunk["id"],
                    "text": chunk["text"],
                    "score": round(self._cosine(qvec, vec), 4),
                })
        results.sort(key=lambda r: r["score"], reverse=True)
        return results[:top_k]

    @staticmethod
    def _cosine(a, b):
        if len(a) != len(b):
            return 0.0
        dot = sum(x * y for x, y in zip(a, b))
        na = math.sqrt(sum(x * x for x in a))
        nb = math.sqrt(sum(y * y for y in b))
        if na == 0.0 or nb == 0.0:
            return 0.0
        return dot / (na * nb)

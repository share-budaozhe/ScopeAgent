# -*- coding: utf-8 -*-
"""
knowledge_test.py — 知识库单元测试（无网络，使用 HashEmbedder 兜底）
用法：python knowledge_test.py
"""
import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from embedder import HashEmbedder
from knowledge import KnowledgeBase, chunk_text, extract_text

failed = 0


def check(cond, msg):
    print("[PASS]" if cond else "[FAIL]", msg)
    if not cond:
        global failed
        failed += 1


# T1 分块：短文本单块
chunks = chunk_text("短段落。", size=500)
check(len(chunks) == 1 and chunks[0] == "短段落。", "短文本单块")

# T2 分块：超长文本按长度硬切，且不越界
long = "。".join("句子%d" % i for i in range(300))
chunks2 = chunk_text(long, size=100, overlap=10)
check(len(chunks2) > 1, "超长文本切成多块")
check(all(len(c) <= 100 for c in chunks2), "分块长度不越界")

# T3 空文本
check(chunk_text("") == [], "空文本返回空列表")
check(chunk_text("\n\n \n") == [], "空白文本返回空列表")

# T4 不支持的扩展名应报错
try:
    extract_text("x.txt")
    check(False, "不支持的扩展名应报错")
except RuntimeError as e:
    check("不支持" in str(e), "不支持的扩展名报错")

# T5 知识库全链路（临时目录 + HashEmbedder）
tmp = tempfile.mkdtemp(prefix="kb_test_")
try:
    kb = KnowledgeBase(HashEmbedder(64), tmp)

    md1 = os.path.join(tmp, "手册.md")
    with open(md1, "w", encoding="utf-8") as f:
        f.write("# 示波器使用手册\n\n"
                "测量方波时应把触发电平设在中段。\n\n"
                "时基应使屏幕显示一到两个完整周期。")

    doc = kb.import_file(md1)
    check(doc["file_type"] == "md", "导入 md 文档")
    check(doc["chunk_count"] >= 1, "文档已分块")

    # 重复导入去重
    try:
        kb.import_file(md1)
        check(False, "重复导入应报错")
    except RuntimeError as e:
        check("已导入" in str(e), "重复导入报错")

    check(len(kb.list_documents()) == 1, "list_documents 返回 1 篇")
    stats = kb.stats()
    check(stats["documents"] == 1 and stats["chunks"] >= 1, "stats 统计正确")

    # 检索命中（词法近似，关键词应命中）
    res = kb.search("测量方波时触发怎么设置", top_k=2)
    check(len(res) >= 1, "检索返回结果")
    check(res[0]["title"] == "手册.md", "检索命中来源标题")
    check(res[0]["score"] > 0, "相似度为正")

    # 删除
    kb.remove_document(doc["id"])
    check(len(kb.list_documents()) == 0, "删除后列表为空")

    # 重建实例，确认删除已持久化
    kb2 = KnowledgeBase(HashEmbedder(64), tmp)
    check(len(kb2.list_documents()) == 0, "删除已持久化")
finally:
    shutil.rmtree(tmp, ignore_errors=True)

print("\n%s (%d failures)" % ("ALL KNOWLEDGE TESTS PASSED" if failed == 0 else "TESTS FAILED", failed))
sys.exit(0 if failed == 0 else 1)

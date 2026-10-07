# 配图清单

文章：《给传统仪器装上「大模型」：一套可落地的通用升级架构》

| 文件 | 尺寸 | 用途 | 对应正文 |
|---|---|---|---|
| `img/arch-layers.png` | 1240×1045（@2x） | 图1　仪器升级五层结构 | 第一章「核心结论先给」 |
| `img/trust-model.png` | 1240×872（@2x） | 图2　双层钳制信任链路 | 第四章「信任模型」 |
| `img/render-scope.png` | 922×560 | 图3　自绘波形显示界面 | 第八章「界面设计」 |

三张图均可**独立阅读**：即便脱离正文单独转发，也能表达清楚各自的主题。

## 图1 设计要点

- 纸底`#fbf8f1` + 荧光笔标记，弱化"设计稿"感，适合公众号长图文
- 右侧四色标签区分改造量级：**存量资产 / 核心新增 / 安全闸门 / 改造重点 / 存量硬件**
- 底部「读图要点」独立卡片，承载结论，便于只转发图片的传播场景

## 图2 设计要点

- 三道闸门用**红色描边加粗**区分，视觉上强化"拦截"的语义
- 闸门一内嵌 `越界→ 就地钳制 +产出告警` 的告警示例，说明钳制是"响"的
- 下方左右两卡分别讲「为什么双层」和「钳制要响」，覆盖正文两个论点
- 标题即结论（LLM 输出永不可信），适合作为单独传播的判断句

## 图3 说明

直接复用项目根目录的 `render_scope.png`，即Qt 应用真实运行截图，未经修饰，体现「零第三方UI 依赖」的事实。

## 重新生成

配图源文件在 `img/src-*.html`，修改 CSS 后用 Edge 无头重渲：

```bash
export PATH="/usr/bin:/bin:/c/Windows/System32:$PATH"
EDGE="/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"
"$EDGE" --headless=new --disable-gpu --hide-scrollbars --no-first-run \
  --force-device-scale-factor=2 --window-size=1240,1045 \
  --user-data-dir="$TEMP/edge-x-$RANDOM" \
  --screenshot="C:/.../img/arch-layers.png" \
  "file:///C:/.../img/src-arch.html?v=2"
```

窗口高度需与 `src-*.html` 的 `body` 高度一致，否则底部留白或截断（用Pillow 扫最后一行墨迹测量）。改 HTML 后必须换新 `--user-data-dir` 并递增 `?v=`，否则 Edge 缓存会让改动静默不生效。
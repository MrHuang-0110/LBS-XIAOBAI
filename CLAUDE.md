# LBS-XiaoBai · 项目规矩

## Memory（MCP）使用铁律

**本项目使用 MCP `memory` 服务器持久化知识图谱**（文件在 `e:/LBS-XiaoBai/.memory/memory.jsonl`）。它是**被动**的 —— 你不调工具它不会记，也不会自动读。所以：

### 1. 会话开头第一件事：读图谱

每次新会话（或 `/compact` 之后的续跑）**做任何回复/工具调用之前**，先跑一次：

```
mcp__memory__read_graph
```

把项目已有的踩坑、决策、用户偏好读进上下文。然后再开始回答用户。

**失败处理：**
- 如果工具不在（`mcp__memory__*` 系列没加载）→ 说明本项目 MCP memory 服务没启用。**告诉用户**，不要自己去手改 `.memory/memory.jsonl`（会踩 [[memory-jsonl-append-must-newline]] 那种换行坑）。
- 如果 `read_graph` 报 JSONL 解析错 → 文件被追加坏了，**告诉用户**，用 Python `json.JSONDecoder().raw_decode` 分块修复；不要盲写。

### 2. 聊完重要事情主动存

**触发条件（满足任一就该写）：**

- 用户明确说"记住"/"存一下"/"记一下"。
- 踩了一个非平凡的坑并修好了（尤其硬件/寄存器/HAL/协议帧长度这类"猜错就掉进去半天出不来"的）。
- 用户表达了明确的**做事偏好**（"以后 commit 完自动 push"、"用 C 风格"、"这个宏一律用 xxx"）。
- 定了一个**架构/接口决策**，未来别的 Task 会依赖它。
- 核对了某份**外部真值**（datasheet 里的 AF 号、协议文档里的字段位、真机测出来的时序）。

**别存的东西：**
- 代码里已经写清楚的（读代码就能知道，不用记）。
- Git 历史能追的（commit message 已经在讲了）。
- 只对当前这轮对话有意义、下轮就没用的（比如"刚才那段临时调试怎么打印"）。
- 具体文件的行号偏移（改一次就过期）—— 存**决策**，不存**位置**。

**存的姿势：**

优先走 MCP 工具，**不要自己拼 JSONL 追加**（血泪教训见 [[memory-jsonl-append-must-newline]]）：

```
mcp__memory__create_entities   # 新事实
mcp__memory__add_observations  # 补充到已有 entity
mcp__memory__create_relations  # 建两条 entity 的关联（relates_to / has_preference 等）
```

**entity 命名规则：** `<域>-<主题>-<关键字>`，kebab-case，全小写，比如：
- `py32-dma-channel2-3-shared-irq`
- `remote-frame-length-17`
- `task7-bsp-key-initial-state`

**observation 内容规范：**
- 用 Markdown。第一行 `# <标题>`。
- 明确 **事实/症状 / Why / How to apply** 三段（可选，但强推）。
- 涉及代码位置用可跳转链接：`[Bsp_Motor.c](e:/LBS-XiaoBai/MCU_XiaoBai/BSP_Drivers/Bsp_Motor/Bsp_Motor.c)` 或 `file.c:42`。
- 交叉引用其他 entity 用 `[[entity-name]]`。
- 日期写绝对日期（`2026-07-23`），不写"今天"/"上周"。

### 3. 更新 vs 新增

- 已有的 entity 里加内容 → `add_observations`（不要重新 `create_entities`，会报 duplicate）。
- 已有观察被证明错了 → `delete_observations` 掉旧的，再 `add_observations` 新的。**不要留下自相矛盾的堆积**。

---

## 其他

（如需在此项目 pin 更多规矩，往下加即可。用户已有的通用偏好——称呼「贤哥」、C 代码风格、commit 后自动 push——存在 memory 图谱里，会话开头 read_graph 时会读进来，不在此文件重复。）

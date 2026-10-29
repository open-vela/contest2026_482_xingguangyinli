# 画间 Living Canvas

星光引力｜2026 首届 openvela AI 硬件全球开发者大赛｜AI 硬件产品创新

画间是一套以实体画框为主形态的家庭共享空间 AI 系统。Gemini S1 运行 openvela，负责界面、确认、语音会话和安全边界；毫米波仅提供匿名 presence 事件；两只 ESP32 猫形配件只负责角色化触摸签到。当前主场景是晚餐协商：系统收集预算与明确限制，给出不超过三个候选，只有用户在画框上明确确认后，才生成手机接续页面；仓库代码不执行付款。

## 事实状态

本分支是决赛软件候选，不把宿主测试、目标构建或历史硬件结果写成最新真机验收。

- 已有：openvela 主控应用源码、双猫局域网协议、毫米波过滤、录音/ASR/TTS 客户端、结构化推荐后端、显式确认状态机、可删除偏好记忆、自动化测试与隐私门禁。
- 已验证：相关软件模块的宿主测试；本轮 UDP/TCP 回环用例因测试沙箱禁止端口绑定而明确跳过，不计为通过。历史 Gemini S1 显示/触控与旧版 ESP32 原型结果另有记录。
- 尚未在本分支证明：最新候选固件已刷入 Gemini S1、最新双猫固件已同时烧录、毫米波已接入最终硬件、真实麦克风到扬声器的整条 v29 闭环。
- AI Coding 日志不会自动上传；`logs/` 仅接受官方工具导出且人工脱敏后的真实会话。本分支不以空日志冒充已提交日志。

详细边界见 [`docs/FINALS_SUBMISSION_STATUS.md`](docs/FINALS_SUBMISSION_STATUS.md)。

## 系统边界

```text
毫米波 presence ─┐
双猫触摸签到 ────┼─> Gemini S1 / openvela ─> 本地状态机与显式确认
麦克风语音 ──────┘              │
                                 └─> Decision Backend ─> 候选/ASR/TTS
                                                      └─> 手机接续（不付款）
```

- Gemini S1 是唯一 openvela 主控；ESP32 是可降级配件。
- 模型只返回受限结构化结果，不能直接写记忆、控制硬件、下单或付款。
- 预算、过敏、临时口味、是否做饭等约束由确定性代码再次校验。
- 临时意愿只属于当前会话；长期偏好必须在确认后保存，并可查看、纠正和删除。
- 毫米波不上传图像，只产生去抖后的匿名 presence 布尔事件。
- 主控 owner/product 与 voice API 在当前候选中共用一个板端 token；accessory 使用独立 token，且运行时拒绝二者取相同值。凭据不得进入源码、测试输出或 AI 日志。

## 代码结构

```text
app/hello_app/              Gemini S1/openvela 主应用
  include/ + src/           状态机、显示、语音、配件、隐私 HTTP/TLS
  skills/                   Dinner Assistant 运行时契约与可复用 Skill
  tests/host/               C/Python/Shell 宿主测试与回环测试入口
backend/                    结构化推荐、会话、鉴权、语音和配件 API
demo/esp32s3_preview/       双猫配件固件、角色 UI、本地协议与测试
scripts/                    软件门禁和只读设备检查工具
```

赛事 manifest 将 `app/hello_app/` 映射到 openvela 工作区的：

```text
packages/demos/contest2026_482_hello_app/
```

## 关键实现

1. **主控状态机**：从 presence/触摸/语音进入问候、监听、推荐、确认和接续；取消、超时、迟到响应及网络错误都有确定退出路径。
2. **本地安全复核**：结构化候选进入画框前，再经过预算、过敏、辣度、烹饪意愿、URL 和动作白名单检查。
3. **双猫协议**：角色、设备身份、序列号、事件和确认码均有边界；主控可分别轮询两只配件并处理重复/乱序消息。
4. **毫米波隐私链**：串口原始帧经严格解析、阈值、去抖和冷却后才变成 presence；不采集影像。
5. **原生语音链**：PCM 录音、会话票据、取消/超时、ASR/TTS 请求和播放生命周期分离，迟到任务不能覆盖新会话。
6. **后端原子性**：会话、推荐、确认、记忆和手机接续有鉴权与原子更新测试；失败时不产生半确认状态。
7. **自定义 Skill**：`app/hello_app/skills/dinner_assistant/SKILL.md` 定义候选数量、事实边界、确认和记忆规则。目标运行时位置按当前 ai_agent 约定为 `/data/agent/skills/`；仓库校验不等同于真机已加载。

## 可重复验证

```bash
make -C app/hello_app/tests/host clean test
PYTHONPATH=backend/src python -m pytest -q backend/tests
bash demo/esp32s3_preview/tests/run_tests.sh
python -m pytest -q scripts/host/tests scripts/device/tests
git diff --check
```

测试数量以当前提交的实际输出为准，不把不同测试套件简单相加成单一覆盖率。需要真实设备、真实供应商或 Caddy 的用例会明确跳过或失败关闭。

## 构建说明

openvela 目标应用入口为 `CONFIG_LVX_USE_DEMO_CONTEST2026_482_LIVING_CANVAS`。`app/hello_app/CMakeLists.txt` 和 `Makefile` 列出主控源码；中文界面字体由 `tools/convert_room_font.py` 从固定 SHA-256 的 Noto Sans CJK SC 源文件生成，字体数据遵循随仓 `Noto-OFL.txt`。

ESP32 配件不是 openvela 主控证据。其构建、烧录和交付验证工具位于 `demo/esp32s3_preview/`，真实 Wi-Fi、设备 token、MAC、串口和备份必须保存在仓库外。

## 许可证与隐私

- 团队源码按顶层 `LICENSE` 的 Apache License 2.0 发布。
- Noto 字体衍生数据遵循 SIL Open Font License 1.1。
- 不提交 API key、Wi-Fi、私钥、owner/accessory token、个人路径、家庭地址、设备备份、原始语音、付款数据或未授权素材。
- 公共仓库中的示例地址和凭据必须是不可用占位符。

## 官方资料

- [openvela AI 硬件赛道指南](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_hardware/ai_hardware_track_guide.md)
- [参赛代码提交指南](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/code_submission_guide.md)
- [AI Coding 日志提交指南](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md)

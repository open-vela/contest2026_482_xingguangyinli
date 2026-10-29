# 画间决赛软件候选状态

更新日期：2026-10-29

## 1. 状态标记

| 标记 | 含义 |
| --- | --- |
| `PASS-SW` | 当前提交可在宿主环境重复验证的软件结果 |
| `PASS-LOOPBACK` | 仅在非受限环境取得真实 UDP/TCP 回环通过证据后使用；本轮不使用 |
| `PASS-HW-OLD` | 历史硬件版本曾验证，但不能替代当前候选验收 |
| `CANDIDATE-OFFLINE` | 已构建/打包的候选，尚未部署到目标设备 |
| `PENDING-HW` | 必须在最终实体硬件上完成 |
| `DEFERRED` | 不进入本轮核心演示承诺 |

## 2. 三条核心闭环

### 2.1 双猫角色配件

- 主控代码：`lc_accessory_client`、`lc_accessory_sync`、`lc_local_accessory_server`、`lc_checkin*`。
- 配件代码：`living_canvas_local_link`、`living_canvas_cat_ui`、输入/渲染策略。
- 当前证据：协议交叉测试、身份/序列号边界和重复/乱序处理属于 `PASS-SW`。UDP 回环测试已执行，但因沙箱禁止端口绑定而 `SKIP`，不标记 `PASS-LOOPBACK`。
- 限制：最新两只实体猫尚未同时按本提交完成烧录、稳定绑定和整轮验收，标记 `PENDING-HW`。

### 2.2 毫米波匿名 presence

- 代码：`lc_mr60_presence`、`lc_esphome_presence`、对应 C/Python 过滤器和监督脚本。
- 当前证据：严格帧解析、阈值、去抖、冷却、异常输入和匿名事件边界属于 `PASS-SW`。
- 隐私：只把 presence 状态交给主控；不采集或上传影像。
- 限制：传感器尚未接入最终演示硬件并完成连续多轮验收，标记 `PENDING-HW`。

### 2.3 中文语音晚餐协商

- 板端代码：`lc_pcm_recorder`、`lc_native_voice`、`lc_voice_client`、`lc_pcm_player`、`lc_product_flow/transport`。
- 后端代码：`voice_api`、`voice_gateway`、结构化推荐、确认、记忆与手机接续。
- 当前证据：录音生命周期、票据隔离、取消/超时、ASR/TTS 接口、鉴权、推荐/拒绝/确认和音频擦除属于 `PASS-SW`。TCP 回环测试已执行，但因沙箱禁止端口绑定而 `SKIP`，不标记 `PASS-LOOPBACK`。
- 限制：当前候选尚未在 Gemini S1 上完成真实“麦克风 → ASR → 推荐 → TTS → 扬声器”连续验收，标记 `PENDING-HW`。

## 3. openvela 与 AI Agent

- Gemini S1 是唯一 openvela 主控，应用通过比赛 manifest 映射到 openvela 工作区。
- `lc_agent_bridge` 对接 ai_agent/VelaClaw 的文本边界；所有物理动作仍由本地确定性代码复核。
- `skills/dinner_assistant/SKILL.md` 是可复用的晚餐决策 Skill，约束候选数量、事实来源、记忆与确认。
- 运行时目标目录按当前 ai_agent 约定为 `/data/agent/skills/`。
- 当前仓库能证明 Skill 文件和契约测试存在，不能单凭此声称目标板已发现并执行 Skill；该项保持 `PENDING-HW`。

## 4. 与评分维度的代码证据映射

| 评分维度 | 仓库证据 | 本提交可证明 | 仍需现场证明 |
| --- | --- | --- | --- |
| 技术难度 30 | openvela 主控、LVGL、音频生命周期、私有 HTTP/TLS、双猫协议、毫米波过滤、后端原子性 | 模块边界、严格编译、自动化及回环测试边界 | 非受限环境回环证据、最新固件目标构建记录及真机连续运行 |
| 产品创新 20 | 家庭共享空间、实体画框、角色化配件、匿名 presence、显式确认 | 产品交互与权限边界已进入代码 | 真实家庭场景的现场演示与用户反馈 |
| 项目完整度 20 | 主控/配件/后端/测试/复现命令 | 软件候选可重复验证 | 最终三条硬件闭环和无剪辑演示 |
| AI 开发 10 | 自定义 Skill、结构化输出、AI Coding 日志规则 | Skill 和安全契约可审查 | 真机 Skill 发现/执行；经脱敏的官方日志 |
| 商业潜力 10 | 主机框 + 角色配件 + 可选 AI 服务的模块化架构 | 技术上支持分层产品形态 | BOM、报价、试点和转化数据不在代码仓伪造 |
| 展示效果 10 | 中文角色 UI、状态/错误可见性、确定性演示入口 | 演示代码和限制清晰 | 稳定真机画面、声音和答辩表达 |

## 5. 明确不作的声明

- 不声称当前提交已经在最新 Gemini S1 固件上完成整机验收。
- 不把 ESP32 配件说成 openvela 主控。
- 不声称毫米波已完成最终硬件接入。
- 不声称二维码会自动下单或付款。
- 不声称存在实际销售、合同、BOM 报价或大规模用户数据。
- 不把构建成功、宿主测试或历史硬件结果替换成当前真机结果。
- 不把未导出、空目录或未经脱敏的对话说成官方 AI Coding 日志。

## 6. 隐私提交边界

仓库只保留可公开源码、无效示例配置和可重复测试。以下内容必须留在仓库外：

- Wi-Fi、API key、owner/voice/accessory token、SSH/私钥；
- 真实公网/私网运维入口、服务器账号和内部部署命令；
- 个人绝对路径、邮箱、手机号、家庭地址；
- 设备 MAC、serial、串口映射和固件/NAND 备份；
- 原始语音、家庭影像、付款信息和未授权素材；
- 答辩私稿及尚未证实的测试结论。

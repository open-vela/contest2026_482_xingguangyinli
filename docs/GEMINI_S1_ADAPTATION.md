# Gemini-S1 / openvela 适配状态

最后核验：2026-09-26（Asia/Shanghai）

## 平台定位

Gemini-S1（Allwinner R528S3）是 Living Canvas 的正式目标主控，目标软件栈为 openvela、LVGL 与 ai_agent。仓库中的应用入口、manifest 映射、Agent 安全桥接、自定义 Skill 和目标构建均围绕这条路线维护。

本页把“源码与构建已经完成”和“实体板端运行已经验证”分开记录。构建成功不等同于已经刷入设备；辅助交互原型也不等同于 openvela 板端运行证据。

## 已验证事项

| 层级 | 结果 | 可复核证据 |
| --- | --- | --- |
| 应用逻辑 | 12 个严格 C 主机测试通过 | `app/hello_app/tests/host/` |
| openvela 映射 | 应用映射到 `packages/demos/contest2026_482_hello_app/` | `contest2026_482_xingguangyinli.xml`、`app/hello_app/` |
| 目标构建 | Gemini-S1 产品配置构建退出状态为 0，`nuttx.elf` 含 `living_canvas_main` | `tests/evidence/build/gemini-s1-product-20260918.txt` |
| 镜像封装 | 目标应用分区和 128 MB NAND LiveSuit 整包生成并校验 | `tests/evidence/build/gemini-s1-product-20260918.txt` |
| 设备基线 | USB/ADB 身份和板载麦克风非内容信号已验证 | `tests/evidence/device/` |
| 恢复通道 | FEL 身份、R528/T113 芯片 ID、Winbond 256 MiB SPI NAND 已识别 | `tests/evidence/device/gemini-s1-fel-spinand-20260920.txt` |
| 写入前保护 | 完整 SPI NAND 只读备份已生成并校验 | `tests/evidence/device/gemini-s1-fel-spinand-20260920.txt` |
| macOS 部署复核 | Apple Silicon macOS 上完成镜像校验、结构检查、FEL 稳定性和 NAND 只读通路复核；UART2 已实证 FES、DRAM、U-Boot 与 SPI NAND 初始化，USB EFEX/SRV 主机枚举仍待完成 | `tests/evidence/device/gemini-s1-macos-retest-20260925.txt`、`tests/evidence/device/gemini-s1-fes-uboot-progress-20260925.txt`、`tests/evidence/device/gemini-s1-uart2-uboot-efex-20260926.txt` |

## 当前适配边界

首次分区写入流程曾在 FES DRAM 初始化阶段超时，流程在进入存储、MBR 和分区写入阶段之前自动停止。停止后 FEL 芯片身份仍可读取，未发生持久化 NAND 写入。

2026 年 9 月 25 日在 Apple Silicon macOS 上再次执行同范围验证，结果复现：镜像 SHA-256 与结构检查通过，FEL 与 SPI NAND 只读链路稳定；写入工具加载 FES 后连续 60 次未能完成 DRAM 就绪检查。操作前后的三个 NAND 采样点逐字节一致，因此该次测试仍不记为持久化写入或板端运行成功。

同日重新枚举 USB 后继续执行受控验证，FES DRAM 初始化连续两次在首次状态读取时通过，返回 `0x4d415244`（`DRAM`）成功标志和参数更新标志。随后 U-Boot、DTB 占位项与系统配置完成内存传输，U-Boot 执行请求成功；设备离开 FEL，但在 45 秒门限内没有枚举成 FES/SRV 设备。工具侧同时补充了 FES 返回参数向 U-Boot 的传递及回归测试；完整可执行测试集 197 项通过。应用该修正后的实板结果仍停在 FES 重枚举门禁，因此没有进入存储查询、MBR、擦除或分区下载阶段。

2026 年 9 月 26 日通过 UART2（1500000 8N1）补齐启动链路实板证据。日志确认 FES 板级初始化、128 MiB DDR3 识别和 DRAM 自检通过；U-Boot 已进入 `workmode = 16`，SPI NAND 完成物理初始化并从 `SUNXI_FLASH` 加载环境。U-Boot 随后进入 `run usb efex` 并报告 `usb init ok`，但 USB 控制端点出现 `ep0 fifo_count 0 is not 8` 和 `read_request failed`，未能在主机侧完成 FES/SRV 枚举。该记录将当前问题进一步收敛到 U-Boot USB EFEX 控制传输与主机枚举环节；流程仍未进入 MBR、擦除或分区写入。

因此，当前可以准确陈述：

- Living Canvas 的 Gemini-S1/openvela 目标源码、主机逻辑、目标构建和镜像封装已经完成并留下证据；
- Gemini-S1 的 USB/ADB、麦克风、FEL、FES/DRAM、U-Boot 运行和 SPI NAND 初始化已经实板验证；
- Gemini-S1 的首次持久化写入、启动以及 LVGL、音频、网络、ai_agent 的板端端到端链路仍在适配验证中；
- 当前不把尚未完成的板端运行描述为已完成，也不把其他主控的演示结果替代为 openvela 运行结果。

当前首次刷写问题已经从 DRAM 初始化收敛到 U-Boot USB EFEX 控制传输与 FES/SRV 主机枚举链路。处理方式仍是保留原始存储备份、限制首次写入范围、核对烧录专用 U-Boot、板卡版本和恢复路径，并在每个阶段生成可回溯证据。

## 代码完成度与运行门禁

应用侧已经具备：

- 存在事件触发的主动问候和冷却控制；
- 晚餐约束收集、最多三个候选和信息不足时追问；
- 模型输出到本地动作之间的白名单与二次校验；
- 用户明确确认后的手机交接、灯光意图和可删除偏好记忆；
- 网络、超时、取消、迟到回复和时钟未同步的确定性回退；
- LVGL 状态、角色反馈、选择与二维码界面；
- 按 ai_agent 官方 Markdown 格式编写、目标部署到
  `/data/agent/skills/dinner-assistant.md` 的 Dinner Assistant Skill。

板端验收只有在以下门禁全部通过后才会标记完成：

1. 核对板卡版本、烧录专用 U-Boot/FES 组合、恢复工具和可回滚镜像；
2. 在可恢复条件下完成受控写入并校验分区；
3. 通过串口或等价通道确认 openvela 启动；
4. 分别验证 LVGL 显示、触摸、音频、网络和 ai_agent；
5. 完成从存在事件到确认、执行和手机交接的实体板端演示；
6. 将脱敏结果补入 `tests/evidence/`。

## 辅助交互原型

仓库保留一个 ESP32-S3 辅助交互原型，用来复核竖屏布局、触摸选择和产品流程。它有独立的源码与自动化测试，但不属于 Gemini-S1/openvela 运行证据，不改变正式目标平台和上述验收门禁。

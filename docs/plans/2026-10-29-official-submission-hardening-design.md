# 官方比赛仓库提交前加固设计

日期：2026-10-29

## 目标

在不改变 Gemini S1 当前凭据配置和板端协议的前提下，关闭记忆接口的未鉴权访问，校正文档中的凭据与回环验证状态，并让即将进入官方比赛仓库的提交不再使用个人邮箱元数据。

## 方案比较

1. 立即拆分 owner 与 voice token：权限隔离更细，但需要同步修改板端配置、后端部署与真机验收，决赛候选风险较高。
2. 只修改文档：改动最小，但不能关闭 `/v1/feedback` 和 `/v1/memory` 的实际访问缺口，不可接受。
3. 最小安全加固：由现有主控 owner token 保护产品写入、反馈及记忆读写；owner 与 voice 继续共用该主控 token，accessory 使用独立 token；文档准确反映这一边界。

采用方案 3。它修复真实安全问题，同时不引入新的板端凭据迁移。

## 鉴权设计

鉴权中间件使用明确的 `(HTTP method, path)` 集合，而不是只判断 POST 路径。以下接口要求主控 owner bearer：

- `POST /v1/session`
- `POST /v1/input`
- `POST /v1/dialogue`
- `POST /v1/device/event`
- `POST /v1/confirm`
- `POST /v1/feedback`
- `GET /v1/memory`
- `DELETE /v1/memory`

未配置有效 owner token 时返回 `503 product_device_not_configured`；缺失或错误 bearer 时返回 `401 product_device_unauthorized`。鉴权必须发生在请求体校验和数据访问之前。

## 事实边界

- 当前主控产品 API 与 voice API 共用同一个 owner-device token；accessory token 独立，且运行时拒绝与 owner token 相同的值。
- UDP/TCP 回环测试代码存在，但当前沙箱禁止端口绑定，本提交只能证明测试被执行并明确跳过，不能标记 `PASS-LOOPBACK`。
- Git 提交作者和提交者邮箱使用 GitHub noreply 地址；不改变源码归属或许可证。

## 验证

先增加未鉴权反馈/记忆访问的失败测试并观察红灯，再实现最小中间件修复。随后运行产品鉴权测试、完整后端测试、仓库软件门禁、隐私扫描和远端分支比较。官方 PR 只在所有阻断项关闭后创建。

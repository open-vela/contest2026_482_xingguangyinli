# Living Canvas ESP32-S3 Preview

这是小米 openvela 大赛 Living Canvas 的独立 ESP32-S3 竖屏演示版。它不替代、也不修改 Gemini-S1/openvela 工程。

- 目标板：Waveshare ESP32-S3-Touch-AMOLED-1.8 V2
- 屏幕：CO5300，368×448 原生竖屏
- 触摸：CST820（CST816 兼容协议）
- Flash / PSRAM：16 MB / 8 MB OPI
- 模式：无私有配置时保留离线触摸演示；生成私有配置后切换为猫 A/B 独立触摸打卡
- UI：全屏矢量猫脸，猫 A 暖橙、猫 B 蓝紫，不复制 Gemini S1 房间画面
- 操作：待机或重试状态下轻触任意位置，发送一次带认证的本地签到
- 反馈：明确区分配置缺失、连接中、可签到、发送中、S1 已签收和未收到回应

独立猫咪 UI 由固件实时绘制，不读取旧房间动画、菜单位图或 SPIFFS 素材。历史素材工具和
分区只为旧预览与完整 16 MiB 回滚兼容保留；当前交付不会构建或写入 `spiffs.bin`。

固件工具可通过 `ARDUINO_CLI` 和 `ESPTOOL_BIN` 指定；烧录前必须同时显式设置
`LC_DEVICE_BUILD_DIR`、`LC_EXPECTED_DEVICE_ID`、`EXPECTED_ESP32_USB_SERIAL`、
`EXPECTED_ESP32_MAC`，并用逗号分隔的私密 `LIVING_CANVAS_NON_CAT_USB_SERIALS`
列出毫米波与摄像头身份；这些值不得写入仓库。用户当时确认现场条件后，还须将
`LC_OPERATOR_CONFIRMATION` 精确设为“人在板旁、供电稳定、线缆不动”。USB 元数据
检查使用带 pyserial 的 `PYTHON_BIN`。`tools/flash_verified_device.sh` 在读取设备前先
核对现场确认和 USB serial/VID，明确排除私密登记的非猫设备，再核对 A/B 身份、
四个固件产物哈希和芯片 MAC，防止写入错误设备或混用另一只猫的固件。首次获准
烧录时，脚本还会在被 Git 忽略的 `build/device-bindings.json` 中以 `0600` 权限
原子记录 `cat_a`/`cat_b` 与 USB 序列号、芯片 MAC 的一对一关系；身份会在真正写
Flash 前保守锁定，因此烧录中断也不能把同一块物理板改刷成另一只猫。可用
`LC_DEVICE_BINDING_REGISTRY` 将此私有注册表放到其他受控位置，但不能提交或公开。

打卡固件的网络配置由 `tools/generate_private_config.py` 从进程环境生成到被忽略且权限为 `0600` 的 `generated/living_canvas_private_config.h`。必需变量为 `LC_CAT_DEVICE_ID`（只允许 `cat_a`/`cat_b`）、主网络的 `LC_WIFI_SSID`、`LC_WIFI_PASSWORD`、`LC_LOCAL_S1_HOST`，备用网络的 `LC_WIFI_FALLBACK_SSID`、`LC_WIFI_FALLBACK_PASSWORD`、`LC_LOCAL_FALLBACK_HOST`，以及共用的 `LC_LOCAL_S1_PORT` 和 `LC_LOCAL_PAIRING_KEY_HEX`（64 位小写十六进制）。两个主机字段都只接受非回环的私有 IPv4；设备在有限连接窗口后自动切换网络，并把签名事件发往与当前网络成对的本地端点。ESP32 不持有公网后台地址、后台设备令牌或公网 CA。脚本不打印这些值；不要把生成的头文件复制到比赛仓库或构建日志。两台设备应分别生成配置、构建并在核对 MAC 后逐台刷写。

双猫首次配对优先使用 `python3 tools/generate_pairing_bundle.py`。在私密终端只设置共同的
主、备用两组 Wi-Fi/本地主机变量和
`LC_LOCAL_S1_PORT` 后执行该工具；默认会用系统安全随机源生成两把不同的 256 位
密钥。若要复用已有密钥，必须同时提供 `LC_CAT_A_PAIRING_KEY_HEX` 与
`LC_CAT_B_PAIRING_KEY_HEX`，且二者不能相同。工具在被忽略的
`build/pairing/pair-*` 目录原子生成三份 `0600` 私密材料：猫 A 头文件、猫 B
头文件，以及应部署到 S1 `/data/living_canvas/local_accessory_keys` 的双密钥文件；
标准输出只显示目录，不显示 Wi-Fi 或密钥。

从同一配对包构建时，分别把对应头文件传给现有交付构建器：

```sh
LC_CAT_DEVICE_ID=cat_a \
LC_PRIVATE_CONFIG_HEADER=<pair-dir>/cat_a/living_canvas_private_config.h \
LC_DEVICE_BUILD_ROOT=<pair-dir>/deliveries \
tools/build_device_firmware.sh

LC_CAT_DEVICE_ID=cat_b \
LC_PRIVATE_CONFIG_HEADER=<pair-dir>/cat_b/living_canvas_private_config.h \
LC_DEVICE_BUILD_ROOT=<pair-dir>/deliveries \
tools/build_device_firmware.sh
```

构建器会严格解析头文件、核对 A/B 身份后才编译，并继续只交付固件、身份文件和
哈希，不把头文件放进交付目录。配对包必须作为私密恢复材料保存，不能提交、截图
或公开传输；S1 密钥文件的实际部署仍须等人在板旁时进行。

逐台正式构建使用 `tools/build_device_firmware.sh`。它在受限临时目录中生成私有头文件，分别输出到被忽略的 `build/devices/cat_a/` 或 `build/devices/cat_b/` 时间戳目录，只交付固件、身份文件和哈希；临时明文配置在退出时清理。编译后的应用固件仍含该设备连接现场 Wi-Fi 和验证局域网消息所需的凭据，必须作为私密产物保存，不能提交仓库或公开传输。不要直接复用另一只猫的构建目录。

通用 `tools/build_firmware.sh` 默认只构建无私有配置的离线预览：即使本机 `generated/` 中残留被忽略的 A/B 配置，暂存阶段也会先剔除它。只有调用者显式设置 `LC_PRIVATE_CONFIG_HEADER` 时，构建才允许把对应设备配置加入固件，避免预览包静默夹带旧 Wi-Fi 或配对密钥。

配置完成后，任意一次明确触摸只生成一个打卡事件。ESP32 使用配对密钥签名局域网 `LC2` 数据包，发送给画框侧端点；每轮最多发送三次并等待带认证的 `LC2ACK`。只有 ACK 中的设备身份、会话与序号都和当前事件一致时才显示成功；超时、篡改或错配时保留同一事件，等待用户再次触摸重发，不制造重复打卡。公网 HTTPS、后端鉴权和云端重试由画框侧边缘中继统一负责，因此云服务器或跨境链路短暂不可达时，猫耳设备与画框的现场交互仍可独立演示。

交付只写 bootloader、分区表、boot_app0 和 application 四个区域，不再写旧房间文件系统。构建与源码测试只证明代码和交付边界成立，现场网络切换、显示顺滑度和真机稳定性仍须在目标猫耳设备上验收。

# Living Canvas Development Environment

Last verified: 2026-09-14 (Asia/Shanghai)

This document records observed state. The official openvela workspace has been synchronized; both the unmodified Gemini-S1 baseline and the AI Agent + Living Canvas product configuration have built successfully. No image has been flashed.

## Host Mac

- Computer: 2021 16-inch MacBook Pro
- SoC: Apple M1 Pro
- Memory: 16 GB
- macOS: Ventura 13.2.1
- Free host storage after the verified baseline backup: about 188 GiB
- Project path: `/path/to/xiaomi-openvela-ibbie`

The project directory uses the ASCII-only name `xiaomi-openvela-ibbie` to reduce quoting and tool-path risk. All commands and documentation use this canonical English path. Local installers, VM data, full source workspaces, build artifacts, and backups remain below ignored `local-setup/` or in the Ubuntu guest.

## UTM guest

- VM name: `LivingCanvas-Ubuntu`
- Guest OS: Ubuntu Server 22.04.5 LTS
- Architecture: `aarch64`
- Kernel: `5.15.0-191-generic`
- CPU: 4 virtual cores
- RAM: 8 GB configured; guest reports 7.7 GiB
- Swap: 3.8 GiB
- Root filesystem after expansion: 123 GiB total, 72 GiB available after the baseline build
- Network: UTM shared network; resolve the current guest address after each restart instead of committing it
- Access: SSH public-key authentication for this development session; passwordless sudo is not configured
- Source root: `/path/to/openvela-workspace`

The installer ISO is ejected. Source lives on the Ubuntu virtual disk, not in a macOS shared folder. Ubuntu Server's text console is expected; a desktop environment is unnecessary for builds.

## Verified guest tools

| Tool | Verified state |
| --- | --- |
| Git | 2.34.1 |
| Git LFS | 3.0.2 |
| Python | 3.10.12 |
| curl | 7.81.0 |
| CMake | 3.22.1 |
| GCC / G++ | 11.4.0 |
| GNU Make | 4.3 |
| repo launcher | 2.65, installed at `~/.local/bin/repo` |
| QEMU user compatibility | `qemu-x86_64-static` with enabled binfmt handler |

The official `repo` launcher SHA-256 is `1211b57b57e4122a9c546295a59b37d24068f1164d0e87bef096d5323c413e4f`.

## Official source workspace

- Manifest branch: `dev-ai-contest-2026`
- Team manifest revision: `224850b197da7837e60b45573af26a479d366dd9`
- `repo sync -c -j2`: completed successfully
- Pinned manifest: `/path/to/openvela-workspace/pinned-manifest.xml`
- Workspace size after sync and baseline build: about 38 GiB
- Before AI integration, `distclean` plus exact restoration of known tracked build side effects left the intended source repositories clean.
- A full post-build `repo status` is expected to contain manifest linkfile/nested-project overlays and generated dependency/object files, so it is not described as globally clean.

The manifest maps this team's application, quick app, and board directories into the shared build tree. Product source remains authored in the team repository, not directly in shared upstream repositories.

## ARM64 host compatibility note

Most contest prebuilts are native AArch64, but the pinned source also invokes x86-64 host tools, including `jidl_gen_cpp` and the `linux-x86_64` GNU strip/objcopy binaries. The initial failures were host-tool runtime failures, not application source failures.

The verified compatibility setup uses:

- Ubuntu packages `qemu-user-static`, `binfmt-support`, `libc6-amd64-cross`, and `libgcc-s1-amd64-cross`;
- `QEMU_LD_PREFIX=/usr/x86_64-linux-gnu` for the amd64 loader/runtime;
- Ubuntu Jammy amd64 `zlib1g` extracted only below `~/.local/share/openvela-x86_64-jammy/`;
- `QEMU_SET_ENV=LD_LIBRARY_PATH=~/.local/share/openvela-x86_64-jammy/root/lib/x86_64-linux-gnu` so only emulated x86-64 tools see that library.

The downloaded `zlib1g_1.2.11.dfsg-2ubuntu9.2_amd64.deb` was obtained from Ubuntu's official security archive and matched SHA-256 `9dc17e51a1be2d9ed63b7b84ef0e4e29c5abe6f1bc62cb03e7181483cce8a2f2`. No foreign architecture was added to `dpkg`, and the system package sources were not modified.

## Current readiness

- Full source sync: **ready**
- Clean Gemini-S1 baseline build: **passed**
- AI Agent + Living Canvas product build: **passed**
- Application development and host tests: **ready**
- Read-only USB/ADB identity checks: **passed**
- First flash: **blocked pending recovery prerequisites**

Historical setup status: `READY_FOR_DEVELOPMENT / BLOCKED_FOR_FLASH`.
The current deployment status is maintained in `docs/BUILD_AND_FLASH.md` and
`docs/GEMINI_S1_ADAPTATION.md`.

See `docs/BUILD_AND_FLASH.md` for the exact build evidence and `docs/RECOVERY.md` for the remaining irreversible-operation gate.

## References

- [Official Ubuntu quick start](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/quickstart/openvela_ubuntu_quick_start.md)
- [Official team manifest](https://github.com/open-vela/contest2026_482_xingguangyinli/blob/dev-ai-contest-2026/contest2026_482_xingguangyinli.xml)
- [Gemini-S1 board README](https://github.com/open-vela/vendor_allwinnertech/blob/dev-ai-contest-2026/boards/r528/r528s3-gemini-s1/README_zh-cn.md)
- [Ubuntu Jammy amd64 zlib1g](https://packages.ubuntu.com/jammy/amd64/zlib1g)

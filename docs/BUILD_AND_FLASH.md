# Gemini-S1 Build and Flash Baseline

Last verified: 2026-09-26 (Asia/Shanghai)

Current status: `FULL_FLASH_VERIFIED / OPENVELA_BOOT_VERIFIED / DISPLAY_HANDOFF_IN_ADAPTATION`.
Sections below preserve dated build evidence. The latest hardware boundary is
recorded in `docs/GEMINI_S1_ADAPTATION.md` and supersedes earlier first-flash
planning statements where they differ.

## Source provenance

- Manifest branch: `dev-ai-contest-2026`
- Team manifest revision: `224850b197da7837e60b45573af26a479d366dd9`
- Official docs revision: `cb0389919ea4fc86774702fb36c08ee9b1366e6c`
- Allwinner board support revision: `1676386193f0e710121e710935f1757c0f34b662`
- Pinned manifest: `/path/to/openvela-workspace/pinned-manifest.xml`
- `repo sync -c -j2`: completed successfully
- Before AI integration, `distclean` plus exact restoration of known tracked build side effects left the intended source repositories clean.
- Full post-build `repo status` contains expected manifest linkfile/nested-project overlays and generated build files; it is not described as globally clean.

## Verified paths

Gemini-S1 target configuration:

```text
vendor/allwinnertech/boards/r528/r528s3-gemini-s1/configs/nsh_minidisplay/defconfig
```

AI Agent defconfig source used for the verified product build:

```text
packages/ai_agent/defconfigs/gemini-s1/gemini-s1_defconfig
```

Team application mapping:

```text
contest2026_482_xingguangyinli/app/hello_app/
packages/demos/contest2026_482_hello_app/
```

## Clean baseline result

Status: `BASELINE_BUILD_PASSED`

The clean board baseline used the unmodified `nsh_minidisplay` configuration. It did **not** copy the AI Agent defconfig and did **not** run `fix_gemini_s1.sh`; those are separate, source-changing integration steps that require their own before/after status and build evidence.

Verified build command in Ubuntu:

```bash
cd /path/to/openvela-workspace

QEMU_LD_PREFIX=/usr/x86_64-linux-gnu \
QEMU_SET_ENV=LD_LIBRARY_PATH="$HOME/.local/share/openvela-x86_64-jammy/root/lib/x86_64-linux-gnu" \
./build.sh vendor/allwinnertech/boards/r528/r528s3-gemini-s1/configs/nsh_minidisplay/ -j2
```

Result:

- Exit code: `0`
- Completed: `2026-09-14 10:24:19 UTC`
- Linker memory report: `13,639,036 B` used in the SDRAM region (`1.42%`)
- Remaining guest root storage after build: about `72 GiB`
- Remaining available guest memory after build: about `7.3 GiB`

Verified outputs:

| Output | Bytes | SHA-256 |
| --- | ---: | --- |
| `nuttx/nuttx` | 7,345,972 | `05566f786bda3b9820adb5f21f6ae6c6dbf6b3796a54c9d2b6bda1eafe4f9996` |
| `nuttx/nuttx.elf` | 155,426,276 | `5fcdf464591975cb59b7bbb9101c7ee0f2fd785a0a4b33adefca04b9c7be4904` |
| `nuttx/nuttx.bin` | 113,143,844 | `c45d7273d17e9de92f469cb63b43c6acd7962ca2abec4e6cb361e46211047f73` |
| `nuttx/vela.bin` | 7,340,976 | `73ad6568dc3c903f0492c339df7d1feca739087fadc442ffc27b6845bc7aca6f` |
| `vendor/allwinnertech/lichee/board/r528s3/gemini-s1_nand/configs/nsh.fex` | 7,340,976 | `73ad6568dc3c903f0492c339df7d1feca739087fadc442ffc27b6845bc7aca6f` |

`nuttx` and `nuttx.elf` were identified as statically linked ARM EABI5 executables. `vela.bin` and the copied board `nsh.fex` have identical size and SHA-256. A local, ignored backup contains `nsh.fex`, the successful build log, and the pinned manifest under `local-setup/backups/gemini-s1-baseline-20260914/`.

## AI Agent + Living Canvas product result

Historical status on 2026-09-14: `PRODUCT_BUILD_PASSED / NOT_FLASHED`.
The latest deployment status is recorded at the top of this document.

The official Gemini-S1 AI Agent configuration and compatibility patches were applied on local, reversible branches. The team application added only `CONFIG_LVX_USE_DEMO_CONTEST2026_482_LIVING_CANVAS=y` to that board configuration before rebuilding.

Verified build command in Ubuntu:

```bash
QEMU_LD_PREFIX=/usr/x86_64-linux-gnu \
QEMU_SET_ENV=LD_LIBRARY_PATH="$HOME/.local/share/openvela-x86_64-jammy/root/lib/x86_64-linux-gnu" \
./build.sh vendor/allwinnertech/boards/r528/r528s3-gemini-s1/configs/nsh_minidisplay/ -e -Wno-error -j2
```

Verification proved that `hello_app_main.c`, `lc_clock.c`, `lc_dinner.c`, `lc_memory.c`, `lc_state.c`, and `lc_ui.c` compiled, and `nuttx.elf` contains the public symbol `living_canvas_main`. The board image is `6,003,600` bytes with SHA-256 `876a09162538bb84415cc588a3a0240ccb8b6af5fb24401b61a0c1d27d6cc778`.

The ignored local backup is under `local-setup/backups/gemini-s1-living-canvas-20260914/`. Complete commands, commits, output hashes, and the post-build repository-state qualification are recorded in `tests/evidence/build/gemini-s1-living-canvas-20260914.txt`.

### Independent decision-backend integration

Status: `HOST_FLOW_PASSED / TARGET_REBUILD_PENDING_VM_LOGIN`

The contest repository now contains its own `backend/src/living_canvas_backend`
package. The Gemini-S1 request boundary uses only `lc_backend_contract` names and
the owned `/v1/session`, `/v1/input`, `/v1/device/event`, and `/v1/confirm`
contract. The backend applies hard constraints before either model or rules
selection, requires explicit device confirmation, and hands off to a
whitelisted phone search URL without submitting an order or payment.

On 2026-09-17 the real local HTTP flow passed from session creation through the
compact QR redirect. Thirty-three backend tests, twelve strict C host tests,
the openvela build-metadata check, the LVGL build-metadata check, and the
Living-Canvas-only branding gate also passed. See
`tests/evidence/integration/living-canvas-backend-flow-20260917.txt`.

The current source has not yet been rebuilt inside Ubuntu. The UTM guest was
running, but the guest agent was unavailable and SSH required credentials not
available to the automated build session. No source was copied to the guest and
no new target image was produced. This access boundary is recorded in
`tests/evidence/build/gemini-s1-living-canvas-backend-20260917.txt`; it is not a
compiler failure and does not alter the earlier verified image.

### Bounded-voice product rebuild

The application was then extended with a bounded, in-memory voice-session state
machine. It does not access the microphone, save audio, or use the network. A new
host regression test compiles and executes `living_canvas_main`, in addition to
the six module tests.

The first product rebuild correctly failed on a malformed entry-point `printf`;
the failure, cause, and regression response are retained in
`tests/evidence/build/gemini-s1-voice-entry-red-20260914.txt`. After the fix, all
seven strict host tests and the full Gemini-S1 product build passed. The resulting
`nsh.fex` is `6,003,600` bytes with SHA-256
`c17e48df9da1c0c068cc1cdb521be3156153a63ecaf884b4cf47588635009521`.

The ignored backup is under
`local-setup/backups/gemini-s1-living-canvas-voice-20260914/`; complete hashes and
## ARM64 compatibility record

integration evidence are in `tests/evidence/build/gemini-s1-living-canvas-voice-20260914.txt`.

Two failures were retained rather than overwritten:

1. The native AArch64 GCC prebuilt initially lacked host `libc++.so.1`; Ubuntu `libc++1` resolved it.
2. The pinned tree invoked x86-64 `jidl_gen_cpp` and later x86-64 strip/objcopy binaries. Ubuntu's `qemu-user-static` binfmt handler plus the amd64 cross-runtime resolved execution. The strip tool also required amd64 `libz.so.1`.

To avoid adding the `amd64` architecture or changing APT sources, Ubuntu Jammy's official `zlib1g_1.2.11.dfsg-2ubuntu9.2_amd64.deb` was checksum-verified and extracted to a user-owned directory. Package SHA-256:

```text
9dc17e51a1be2d9ed63b7b84ef0e4e29c5abe6f1bc62cb03e7181483cce8a2f2
```

`QEMU_SET_ENV` limits the extra `LD_LIBRARY_PATH` to emulated x86-64 programs; native AArch64 build tools do not inherit an incompatible runtime search path.

## Warnings

The successful build contains upstream LTO/type and NAND `memcpy` warnings plus an empty-loadable-segment warning from strip. They did not produce a nonzero exit or prevent image generation. They remain recorded for later upstream review and must not be rewritten as errors or silently removed from evidence.

## Current deployment gate

### 2026-09-26 full-image result

Status: `FULL_FLASH_VERIFIED / OPENVELA_BOOT_VERIFIED`

The 128 MiB factory-layout image completed full erase, MBR and partition
programming, Boot0/Boot1 programming, verification, and automatic reboot on the
physical Gemini-S1. The board then booted from NAND to the openvela NSH prompt.
LVGL opened `/dev/fb0`, the touch stack opened `/dev/input0`, and generated
pixels were confirmed in framebuffer memory. The remaining display acceptance
item is the ILI9341 CPU/TCON transfer and landscape-rotation handoff. See
`tests/evidence/device/gemini-s1-full-flash-runtime-20260926.txt`.

### Earlier controlled-deployment history

Status: `PLATFORM_DEPLOYMENT_IN_ADAPTATION`

On 2026-09-20 the board was identified in FEL mode, its Winbond W25N02KV
256 MiB SPI NAND was detected, and a full read-only pre-write backup was
verified. A controlled bootloader-partition operation was then requested with
verification enabled. It stopped during FES DRAM initialization before the
storage, MBR, or partition-write stages; persistent NAND write was not reached,
and FEL identity remained readable.

Next acceptance gates are: resolve the U-Boot-to-FES USB re-enumeration,
complete a recoverable verified write, confirm openvela boot, and then verify
LVGL, touch, audio, network, and ai_agent end to end. See
`docs/GEMINI_S1_ADAPTATION.md`, `docs/RECOVERY.md`, and
`tests/evidence/device/gemini-s1-fel-spinand-20260920.txt`.

The same bounded operation was repeated on Apple Silicon macOS on 2026-09-25.
Image integrity and structure, FEL enumeration, and SPI NAND reads passed. The
operation again stopped after 60 failed FES DRAM readiness checks, before any
storage-write stage. Three NAND samples were byte-identical before and after
the attempt. The sanitized record is
`tests/evidence/device/gemini-s1-macos-retest-20260925.txt`.

After a later physical USB re-enumeration, the same FES payload initialized
DRAM successfully twice on the first status read. U-Boot transfer and execution
were reached, including a tested correction that propagates FES-returned DRAM
parameters into the staged U-Boot header. The device did not enumerate as an
FES/SRV device within the bounded wait, so the flow still stopped before
storage query or persistent NAND write. See
`tests/evidence/device/gemini-s1-fes-uboot-progress-20260925.txt`.

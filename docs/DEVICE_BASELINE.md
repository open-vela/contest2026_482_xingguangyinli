# Gemini-S1 Device Baseline

Last verified: 2026-09-14 (Asia/Shanghai)

## Safety scope

This baseline is read-only. It does not flash, reboot, erase, configure, mount, push to, or open a shell on the board. Every ADB operation is pinned to the recorded Gemini-S1 serial `1234` when a target is required.

## Verified host-side identity

- USB product: `Debug Bridge`
- Manufacturer: `NuttX`
- VID/PID: `18d1:4e11`
- Serial: `1234`
- ADB state: one device with serial `1234` reports `device`
- ADB package: Android platform-tools 37.0.1, stored below ignored `local-setup/`
- Platform-tools archive SHA-256: `ee39ad5967e95c2a07f04dbcbde96b1a0c916ba376096db5d2f498b7727a5d1d`

The current physical inventory intentionally has the Gemini-S1 connected alone. The recorded mmWave and camera-controller serials are absent, which is the expected safe state during board baseline work:

- Seeed mmWave expected serial: configured through `LIVING_CANVAS_MMWAVE_USB_SERIAL`
- Seeed camera controller expected serial: configured through `LIVING_CANVAS_CAMERA_USB_SERIAL`

## Shell capability status

`adb shell help` did not produce a verified command response from the factory board. No Android shell behavior is assumed. Until an authoritative Gemini-S1 transport procedure or a confirmed serial NSH console is available, the collection script records only host-side ADB identity and version.

Status: `BLOCKED_FOR_SHELL_DISCOVERY`

This does not block host-side development, repository setup, or simulator/unit tests. It blocks claims about the factory system's version, filesystems, networking, display/input devices, audio nodes, installed applications, and clock.

## Evidence

- `tests/evidence/device/gemini-baseline-20260913T184253Z.txt`
- `tests/evidence/device/usb-inventory-20260913T183505Z.txt`
- Re-run USB inventory: `bash scripts/device/verify_usb_inventory.sh`
- Re-run host-side ADB capture: `bash scripts/device/collect_gemini_baseline.sh --adb local-setup/platform-tools/adb`

Evidence files replace host home-directory prefixes with `<HOME>` and contain no Wi-Fi password, API token, Ubuntu password, SSH private key, or user content.

## Next safe verification

1. Obtain the official Gemini-S1 serial-console or factory ADB procedure.
2. Re-run command discovery with only serial `1234` connected.
3. Add only commands demonstrated to be read-only to the collector.
4. Keep flash operations blocked until every item in `docs/RECOVERY.md` is resolved.

import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class SourceBoundaryTest(unittest.TestCase):
    def test_sketch_is_native_portrait_living_canvas(self):
        sketch = (ROOT / "living_canvas_esp32s3_preview.ino").read_text(encoding="utf-8")
        pins = (ROOT / "board_pins.h").read_text(encoding="utf-8")
        combined = sketch + pins
        self.assertIn("LCD_WIDTH 368", pins)
        self.assertIn("LCD_HEIGHT 448", pins)
        self.assertIn("drawCatCompanionScreen", sketch)
        self.assertIn("fillTriangle", sketch)
        self.assertIn("fillRoundRect", sketch)
        self.assertIn("CST820", sketch)
        self.assertNotIn("SPIFFS", sketch)
        self.assertNotIn("LivingCanvasState", sketch)
        self.assertNotIn("WebSockets", combined)

    def test_partition_has_dedicated_nine_megabyte_spiffs(self):
        partitions = (ROOT / "partitions.csv").read_text(encoding="utf-8")
        self.assertIn("spiffs,   data, spiffs,   0x610000, 0x960000", partitions)

    def test_flash_script_is_locked_to_the_new_board(self):
        script = (ROOT / "tools" / "flash_verified_device.sh").read_text(encoding="utf-8")
        self.assertIn("EXPECTED_ESP32_MAC", script)
        self.assertIn("EXPECTED_ESP32_USB_SERIAL", script)
        self.assertIn("verify_port_identity.py", script)
        self.assertIn("/dev/cu.usbmodem", script)
        self.assertNotIn("/dev/cu.usbserial-", script)

    def test_build_script_ships_only_the_independent_companion_firmware(self):
        script = (ROOT / "tools" / "build_firmware.sh").read_text(encoding="utf-8")
        self.assertIn("checksums.sha256", script)
        self.assertIn("shasum -a 256", script)
        self.assertIn(
            'if [ "$build_root" = "$project_dir/build" ]; then',
            script,
        )
        self.assertNotIn("mkspiffs", script)
        self.assertNotIn("spiffs.bin", script)

    def test_delivery_and_flash_do_not_write_the_obsolete_room_filesystem(self):
        sources = "\n".join(
            (ROOT / "tools" / name).read_text(encoding="utf-8")
            for name in (
                "build_device_firmware.sh",
                "verify_device_delivery.py",
                "flash_verified_device.sh",
            )
        )
        self.assertNotIn("spiffs.bin", sources)
        self.assertNotIn('0x610000', sources)

    def test_build_script_stages_sketch_under_arduino_project_name(self):
        script = (ROOT / "tools" / "build_firmware.sh").read_text(encoding="utf-8")
        self.assertIn('stage_dir="$stage_root/living_canvas_esp32s3_preview"', script)
        self.assertIn('"$project_dir/living_canvas_esp32s3_preview.ino"', script)
        self.assertIn('"$stage_dir/living_canvas_esp32s3_preview.ino"', script)
        self.assertIn('"$arduino_cli" compile', script)
        self.assertIn('"$stage_dir"', script)

    def test_scripts_are_portable_and_do_not_expose_local_home_paths(self):
        script_paths = (
            sorted((ROOT / "tools").glob("*.sh"))
            + sorted((ROOT / "tools").glob("*.py"))
            + [
            ROOT / "tests" / "run_tests.sh"
            ]
        )
        combined = "\n".join(path.read_text(encoding="utf-8") for path in script_paths)
        self.assertNotIn("/Users/", combined)
        self.assertIn("LC_ASSET_SOURCE_DIR", combined)
        self.assertIn("ARDUINO_CLI", combined)
        self.assertIn("ESPTOOL_BIN", combined)

    def test_sketch_routes_explicit_touch_to_authenticated_local_checkin(self):
        sketch = (ROOT / "living_canvas_esp32s3_preview.ino").read_text(
            encoding="utf-8"
        )
        self.assertIn('#include <WiFi.h>', sketch)
        self.assertIn('#include <WiFiUdp.h>', sketch)
        self.assertIn('#include <mbedtls/md.h>', sketch)
        self.assertIn('#include "src/living_canvas_local_link.h"', sketch)
        self.assertIn('LivingCanvasLocalLink *localLink', sketch)
        self.assertIn('localLink->beginCheckin(nowMs)', sketch)
        self.assertIn('localLink->retry(nowMs)', sketch)
        self.assertIn('localUdp.beginPacket(LC_LOCAL_S1_HOST,', sketch)
        self.assertIn('localLink->acceptAck(', sketch)
        self.assertIn('mbedtls_md_hmac(', sketch)
        self.assertNotIn('setInsecure', sketch)
        self.assertNotIn("LivingCanvasScreen::Home", sketch)
        self.assertNotIn("drawHome(", sketch)
        self.assertIn(
            '[CHECKIN] offline UI preview; no event was delivered', sketch
        )

    def test_local_delivery_is_finite_and_success_requires_signed_ack(self):
        sketch = (ROOT / "living_canvas_esp32s3_preview.ino").read_text(
            encoding="utf-8"
        )
        self.assertIn("localLink->update(nowMs)", sketch)
        self.assertIn("localLink->markSent(nowMs)", sketch)
        self.assertIn("LivingCanvasLocalLinkState::Retry", sketch)
        self.assertNotIn("catUi.onMatchingAck", sketch.split("acceptAck", 1)[0])

    def test_checkin_mode_does_not_accept_the_boot_button_as_a_touch(self):
        sketch = (ROOT / "living_canvas_esp32s3_preview.ino").read_text(
            encoding="utf-8"
        )
        self.assertIn('#include "src/living_canvas_input_policy.h"', sketch)
        self.assertIn("livingCanvasInputAction(", sketch)
        self.assertIn("localLink != nullptr", sketch)
        self.assertIn("LivingCanvasInputAction::Touch", sketch)
        self.assertIn("LivingCanvasInputAction::PreviewButton", sketch)

    def test_cat_checkin_ui_is_a_full_screen_independent_companion(self):
        sketch = (ROOT / "living_canvas_esp32s3_preview.ino").read_text(
            encoding="utf-8"
        )
        render_policy = (
            ROOT / "src" / "living_canvas_render_policy.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn('#include "src/living_canvas_cat_ui.h"', sketch)
        self.assertIn("LivingCanvasCatUi", sketch)
        self.assertIn("drawCatCompanionScreen", sketch)
        self.assertIn("livingCanvasCatRenderPlan", sketch)
        self.assertIn('"SETUP REQUIRED"', render_policy)
        self.assertIn('"CONNECTING TO FRAME"', render_policy)
        self.assertIn('"TAP TO CHECK IN"', render_policy)
        self.assertIn('"TELLING THE FRAME"', render_policy)
        self.assertIn('"FRAME RECEIVED IT"', render_policy)
        self.assertIn('"NO RESPONSE"', render_policy)
        self.assertNotIn("TAP TO HIGHLIGHT - TAP AGAIN TO OPEN", sketch)

    def test_runtime_never_returns_to_the_room_animation(self):
        sketch = (ROOT / "living_canvas_esp32s3_preview.ino").read_text(
            encoding="utf-8"
        )
        self.assertIn('#include "src/living_canvas_render_policy.h"', sketch)
        setup = sketch.split("void setup()", 1)[1].split("void loop()", 1)[0]
        loop = sketch.split("void loop()", 1)[1]
        self.assertIn("drawCatCompanionScreen", setup)
        self.assertIn("drawCatCompanionScreen", loop)
        self.assertNotIn("drawHome(", setup)
        self.assertNotIn("drawHome(", loop)
        self.assertNotIn("preloadHomeFrames", setup)
        self.assertNotIn("SPIFFS.begin", setup)

    def test_private_network_config_is_generated_but_never_tracked(self):
        ignore = (ROOT / ".gitignore").read_text(encoding="utf-8")
        generator = (
            ROOT / "tools" / "generate_private_config.py"
        ).read_text(encoding="utf-8")
        self.assertIn("generated/living_canvas_private_config.h", ignore)
        for variable in (
            "LC_CAT_DEVICE_ID",
            "LC_WIFI_SSID",
            "LC_WIFI_PASSWORD",
            "LC_LOCAL_S1_HOST",
            "LC_LOCAL_S1_PORT",
            "LC_LOCAL_PAIRING_KEY_HEX",
        ):
            self.assertIn(variable, generator)
        for legacy_variable in (
            "LC_ACCESSORY_EVENTS_URL",
            "LC_ACCESSORY_DEVICE_TOKEN",
            "LC_BACKEND_CA_PEM_FILE",
        ):
            self.assertNotIn(legacy_variable, generator)
        self.assertIn("0o600", generator)

    def test_obsolete_direct_http_cat_transport_is_not_shipped(self):
        for relative_path in (
            "src/living_canvas_checkin_event.h",
            "src/living_canvas_checkin_event.cpp",
            "src/living_canvas_checkin_response.h",
            "src/living_canvas_checkin_response.cpp",
        ):
            self.assertFalse((ROOT / relative_path).exists(), relative_path)

        runner = (ROOT / "tests" / "run_tests.sh").read_text(encoding="utf-8")
        self.assertNotIn("test_checkin_event", runner)
        self.assertNotIn("test_checkin_response", runner)


if __name__ == "__main__":
    unittest.main()

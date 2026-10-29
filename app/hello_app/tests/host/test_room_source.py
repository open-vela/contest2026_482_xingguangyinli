"""Guard renderer wiring; geometry and state are tested separately in C."""
from pathlib import Path

source = (Path(__file__).resolve().parents[2] / "src/lc_display.c").read_text()
assert "create_room_hotspot" in source, "product must use transparent table hit areas"
assert "g_product_cat_bubble" in source and "g_product_dog_bubble" in source
overlay = source.split("static void create_product_overlay(lv_obj_t *screen)\n", 1)[1].split("static void set_competition_text", 1)[0]
assert "LV_ALIGN_CENTER, 0, 4" not in overlay, "result must not cover faces"
assert "lc_bubble_cat" in overlay and "lc_bubble_dog" in overlay
assert "g_choice.armed || g_choice.confirmed" in source, "idle must not highlight default choice"
visibility = source.split("static void update_product_dialogue_visibility(void)\n", 1)[1].split("static void render_product_flow", 1)[0]
assert "g_product_flow.state == LC_PRODUCT_QR" in visibility, "timer must not reveal bubbles over QR"
assert visibility.index("g_product_flow.state == LC_PRODUCT_QR") < visibility.index("lc_product_dialogue_build"), "QR must return before bubble reveal"
label_setup = source.split("static void configure_label", 1)[1].split("static void create_preview", 1)[0]
assert "lc_font_cjk_14" in label_setup, "real Chinese recommendations need a CJK font"
makefile = (Path(__file__).resolve().parents[2] / "Makefile").read_text()
assert "src/generated/lc_font_cjk_14.c" in makefile, "font must be linked in firmware"
cmake = (Path(__file__).resolve().parents[2] / "CMakeLists.txt").read_text()
assert "src/generated/lc_font_cjk_14.c" in cmake, "CMake firmware must link font too"
assert "lc_room_hotspot_create" in source and "lc_room_bubble_create" in source
assert "src/lc_room_view.c" in makefile and "src/lc_room_view.c" in cmake
shared = (Path(__file__).resolve().parents[2] / "src/lc_room_view.c").read_text()
assert "LV_OPA_TRANSP" in shared and "LV_OBJ_FLAG_SCROLLABLE" in shared
assert "lv_obj_set_style_border_width(hotspot, 0, 0)" in shared
greeting = source.split("static bool create_greeting_overlay", 1)[1].split("static int g_touch_fd", 1)[0]
assert "lc_room_greeting_create" in greeting, "greeting must use the shared head-level bubble"
assert "LV_ALIGN_CENTER" not in greeting and "LV_OPA_COVER" not in greeting, "greeting must not obscure characters"
assert "g_greeting_character" not in source, "ASCII character panel is not the supplied room character"
for old_message in ("SET BACKEND URL FIRST", "FIVE ROLES ARE THINKING...",
                    "PROCESSING YOUR VOICE...", "LISTENING - TAP TO SEND",
                    "SCAN ON PHONE - REVIEW & PAY THERE", "TAP HERE TO TALK",
                    "WHAT SHALL WE EAT?", "PAYMENT STAYS ON PHONE",
                    "User selected takeout on the frame"):
    assert old_message not in source, "product screen must use Chinese: " + old_message
for message in ("点击开始说话", "正在聆听，再点发送", "正在识别语音",
                "正在思考", "正在回答", "扫码打开饿了么，手机付款"):
    assert message in source, "Chinese status missing: " + message
print("PASS: room renderer source wiring (not an LVGL render test)")

from pathlib import Path
import unittest


class CheckinDisplaySourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (Path(__file__).parents[2] / "src" / "lc_display.c").read_text()

    def test_checkin_has_a_runtime_separate_from_presence(self):
        self.assertIn("static lc_checkin_runtime_t g_product_checkin_runtime;", self.source)
        self.assertIn("static lc_accessory_runtime_t g_product_presence_runtime;", self.source)

    def test_product_inputs_starts_both_readers(self):
        self.assertIn("int lc_display_run_product_inputs(", self.source)
        self.assertIn("start_product_checkin_reader(checkin_event_path)", self.source)

    def test_checkin_status_is_rendered_without_a_full_screen_overlay(self):
        self.assertIn("lc_checkin_format_status", self.source)
        self.assertIn("set_choice_status(status_text, 0x65d48a);", self.source)

    def test_default_product_uses_authenticated_remote_sync(self):
        self.assertIn("lc_accessory_client_init_default", self.source)
        self.assertIn("lc_accessory_sync_poll", self.source)
        self.assertIn("presence_event_path == NULL && checkin_event_path == NULL", self.source)

    def test_remote_failure_does_not_replace_main_interaction_state(self):
        worker = self.source.split("static void *product_remote_worker", 1)[1]
        worker = worker.split("static bool start_product_remote_accessories", 1)[0]
        self.assertIn("PRODUCT_REMOTE_BACKOFF_US", worker)
        self.assertNotIn("set_choice_status", worker)

    def test_lvgl_is_only_updated_by_timer_not_remote_worker(self):
        worker = self.source.split("static void *product_remote_worker", 1)[1]
        worker = worker.split("static bool start_product_remote_accessories", 1)[0]
        self.assertNotIn("lv_", worker)
        timer = self.source.split("static void poll_product_remote", 1)[1]
        timer = timer.split("static void set_product_voice_phase", 1)[0]
        self.assertIn("start_greeting_overlay();", timer)
        self.assertIn("set_choice_status(status, 0x65d48a);", timer)

    def test_authenticated_local_udp_server_feeds_the_existing_tracker(self):
        self.assertIn('#include "lc_local_accessory_server.h"', self.source)
        self.assertIn(
            "static lc_local_accessory_server_t "
            "g_product_local_accessory_server;",
            self.source,
        )
        callback = self.source.split(
            "static bool accept_product_local_accessory", 1
        )[1].split("static void *product_local_accessory_worker", 1)[0]
        self.assertIn("pthread_mutex_lock(&g_product_checkin_lock)", callback)
        self.assertIn("lc_checkin_tracker_accept", callback)
        self.assertIn("g_product_checkin_pending =", callback)
        self.assertIn("lc_checkin_feedback_on_accepted", callback)

    def test_local_udp_worker_never_calls_lvgl_and_is_fail_soft(self):
        worker = self.source.split(
            "static void *product_local_accessory_worker", 1
        )[1].split("static bool start_product_local_accessory_server", 1)[0]
        self.assertIn("lc_local_accessory_server_poll", worker)
        self.assertNotIn("lv_", worker)
        startup = self.source.split(
            "static bool start_product_local_accessory_server", 1
        )[1].split("static void poll_product_checkin", 1)[0]
        self.assertIn("LC_LOCAL_ACCESSORY_KEYS_CONFIG_PATH", startup)
        self.assertIn("lc_local_accessory_mbedtls_hmac", startup)
        self.assertIn("pthread_create", startup)
        self.assertIn(
            "if (!lc_local_accessory_keys_load("
            "LC_LOCAL_ACCESSORY_KEYS_CONFIG_PATH,\n"
            "                                    &keys))\n"
            "    {\n"
            "      mark_product_local_accessory_error();",
            startup,
        )
        self.assertGreaterEqual(
            startup.count("mark_product_local_accessory_error();"), 5
        )
        self.assertIn("(void)start_product_local_accessory_server();", self.source)

    def test_local_checkin_feedback_is_identity_specific_and_one_shot(self):
        self.assertIn('#include "lc_checkin_feedback.h"', self.source)
        self.assertIn("lc_checkin_feedback_on_accepted", self.source)
        self.assertIn("lc_checkin_feedback_take_chime", self.source)
        self.assertIn("lc_pcm_player_play_native", self.source)
        self.assertIn("render_choice_state();", self.source)


if __name__ == "__main__":
    unittest.main()

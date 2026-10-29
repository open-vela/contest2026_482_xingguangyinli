#!/usr/bin/env python3

import unittest
from pathlib import Path


SOURCE = (Path(__file__).resolve().parents[2] / "src" / "lc_display.c").read_text()


class MmwaveDisplaySourceTests(unittest.TestCase):
    def test_local_mmwave_uses_strict_esphome_parser(self):
        self.assertIn('#include "lc_esphome_presence.h"', SOURCE)
        self.assertIn("lc_esphome_presence_feed", SOURCE)
        self.assertIn("lc_accessory_runtime_accept_binary", SOURCE)

    def test_local_mmwave_is_read_only_and_does_not_emit_wire_ack(self):
        self.assertIn("direct_mmwave", SOURCE)
        self.assertIn("open(g_product_presence_path, O_RDONLY | O_NONBLOCK)", SOURCE)
        self.assertIn("if (!direct_mmwave && writable", SOURCE)


if __name__ == "__main__":
    unittest.main()

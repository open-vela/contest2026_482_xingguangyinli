#!/usr/bin/env python3
"""End-to-end supervisor tests for one bounded MR60 relay sample."""

import importlib.util
import io
import sys
import unittest
from pathlib import Path


APP = Path(__file__).resolve().parents[2]
TOOLS = APP / "tools"
SUPERVISOR = TOOLS / "supervise_mmwave_relay.py"
CONFIRMATION = "人在板旁、供电稳定、线缆不动"
TEST_MMWAVE_SERIAL = "02:00:00:00:00:01"


def report(value=b"1.00000"):
    return (b"[17:20:03][D][sensor:094]: 'Target number': Sending state "
            + value + b" with 0 decimals of accuracy\n")


class FakeRelay:
    def __init__(self, accepted=True):
        self.accepted = accepted
        self.observations = []

    def deliver(self, observation):
        self.observations.append(observation)
        return self.accepted


class SuperviseMmwaveRelayTests(unittest.TestCase):
    def load(self):
        self.assertTrue(SUPERVISOR.is_file(), "mmWave relay supervisor is missing")
        sys.path.insert(0, str(TOOLS))
        try:
            spec = importlib.util.spec_from_file_location("supervise_mmwave_relay", SUPERVISOR)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            return module
        finally:
            sys.path.pop(0)

    @staticmethod
    def port(module):
        return type("Port", (), {
            "device": "/dev/cu.usbmodem21401",
            "serial_number": TEST_MMWAVE_SERIAL,
            "vid": 0x303A,
        })()

    def test_one_readonly_sample_delivers_only_boolean_observations(self):
        module = self.load()
        chunks = [report() + report(b"0.00000"), b""]
        relay = FakeRelay()
        out, err = io.StringIO(), io.StringIO()

        result = module.run_relay_sample(
            [self.port(module)], CONFIRMATION, relay, out, err,
            expected_serial=TEST_MMWAVE_SERIAL,
            opener=lambda _path, _flags: 17,
            closer=lambda _fd: None,
            reader=lambda _fd, _count: chunks.pop(0),
            waiter=lambda *_args: ([17], [], []),
            clock=lambda: 0.0,
        )

        self.assertEqual(result, 0)
        self.assertEqual(relay.observations, [{"detected": True}, {"detected": False}])
        self.assertEqual(out.getvalue(), "MMWAVE_RELAY_SAMPLE_OK\n")
        self.assertEqual(err.getvalue(), "")

    def test_sample_or_delivery_failure_is_fail_closed(self):
        module = self.load()
        for chunks, relay in (([b"not a report\n", b""], FakeRelay()),
                              ([report(), b""], FakeRelay(accepted=False))):
            with self.subTest(chunks=chunks):
                out, err = io.StringIO(), io.StringIO()
                result = module.run_relay_sample(
                    [self.port(module)], CONFIRMATION, relay, out, err,
                    expected_serial=TEST_MMWAVE_SERIAL,
                    opener=lambda _path, _flags: 17,
                    closer=lambda _fd: None,
                    reader=lambda _fd, _count: chunks.pop(0),
                    waiter=lambda *_args: ([17], [], []),
                    clock=lambda: 0.0,
                )
                self.assertNotEqual(result, 0)
                self.assertEqual(out.getvalue(), "")
                self.assertIn("unavailable", err.getvalue())

    def test_bad_confirmation_cannot_even_enumerate_host_ports(self):
        module = self.load()
        calls = []

        def port_provider():
            calls.append(True)
            return []

        with self.assertRaises(ValueError):
            module.confirmed_ports("人在板旁", port_provider)
        self.assertEqual(calls, [])

        self.assertEqual(module.confirmed_ports(CONFIRMATION, port_provider), [])
        self.assertEqual(calls, [True])

    def test_source_does_not_log_raw_sensor_data_or_configure_serial(self):
        source = SUPERVISOR.read_text(encoding="utf-8")
        self.assertIn("configured_mmwave_serial", source)
        for forbidden in ("serial.Serial", "O_RDWR", "O_WRONLY", "tcsetattr", "print(raw"):
            self.assertNotIn(forbidden, source)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3

import importlib.util
import io
import json
import unittest
import urllib.error
from pathlib import Path


APP_ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = APP_ROOT / "tools" / "accessory_http_relay.py"


class FakeResponse:
    def __init__(self, status, body):
        self.status = status
        self._body = body

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        return False

    def read(self, limit):
        return self._body[:limit]


class AccessoryHttpRelayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location("accessory_http_relay", MODULE_PATH)
        assert spec is not None and spec.loader is not None
        cls.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.module)

    def relay(self, opener):
        return self.module.AccessoryHttpRelay(
            endpoint="https://relay.example/v1/accessories/events",
            token="synthetic-device-token-with-at-least-32-characters",
            session_id="mw_boot-42",
            opener=opener,
        )

    def test_posts_minimal_authenticated_presence_with_stable_retry(self):
        requests = []

        def opener(request, timeout):
            requests.append((request, timeout))
            payload = json.loads(request.data)
            if len(requests) == 1:
                raise urllib.error.URLError("synthetic outage")
            return FakeResponse(202, json.dumps({"cursor": 7, **payload}).encode())

        relay = self.relay(opener)
        self.assertTrue(relay.deliver({"detected": True}))
        self.assertEqual(relay.sequence, 2)
        self.assertEqual(len(requests), 2)
        first = json.loads(requests[0][0].data)
        second = json.loads(requests[1][0].data)
        self.assertEqual(first, second)
        self.assertEqual(first, {
            "device_id": "mmwave_bridge",
            "session_id": "mw_boot-42",
            "sequence": 1,
            "event": "presence",
            "value": True,
        })
        self.assertEqual(
            requests[0][0].get_header("Authorization"),
            "Bearer synthetic-device-token-with-at-least-32-characters",
        )
        self.assertLessEqual(requests[0][1], 5)

    def test_specific_replay_response_closes_a_lost_ack(self):
        def opener(request, timeout):
            raise urllib.error.HTTPError(
                request.full_url,
                409,
                "Conflict",
                {},
                io.BytesIO(b'{"detail":"accessory_event_replayed"}'),
            )

        relay = self.relay(opener)
        self.assertTrue(relay.deliver({"detected": False}))
        self.assertEqual(relay.sequence, 2)

    def test_auth_schema_and_other_http_failures_fail_closed(self):
        for status, body in (
            (401, b'{"detail":"accessory_device_unauthorized"}'),
            (409, b'{"detail":"accessory_cursor_ahead"}'),
            (422, b'{"detail":"accessory_event_invalid"}'),
            (500, b"private server diagnostics"),
        ):
            with self.subTest(status=status):
                def opener(request, timeout, status=status, body=body):
                    raise urllib.error.HTTPError(
                        request.full_url, status, "failed", {}, io.BytesIO(body)
                    )

                relay = self.relay(opener)
                self.assertFalse(relay.deliver({"detected": True}))
                self.assertEqual(relay.sequence, 1)

    def test_rejects_extra_fields_bad_types_and_unsafe_setup(self):
        relay = self.relay(lambda *_args, **_kwargs: None)
        for observation in (
            {},
            {"detected": 1},
            {"detected": True, "distance": 123},
            {"detected": True, "identity": "private"},
        ):
            with self.subTest(observation=observation):
                with self.assertRaises(ValueError):
                    relay.encode(observation)

        invalid = (
            ("http://relay.example/v1/accessories/events",
             "synthetic-device-token-with-at-least-32-characters", "boot"),
            ("https://relay.example/wrong",
             "synthetic-device-token-with-at-least-32-characters", "boot"),
            ("https://relay.example/v1/accessories/events", "short", "boot"),
            ("https://relay.example/v1/accessories/events",
             "synthetic-device-token-with-at-least-32-characters", "bad session"),
        )
        for endpoint, token, session in invalid:
            with self.subTest(endpoint=endpoint, session=session):
                with self.assertRaises(ValueError):
                    self.module.AccessoryHttpRelay(endpoint, token, session)

    def test_stream_is_bounded_and_stops_after_failed_delivery(self):
        delivered = []

        class StubRelay:
            def deliver(self, observation):
                delivered.append(observation)
                return len(delivered) == 1

        source = io.BytesIO(
            b'{"detected":true}\n'
            b'{"detected":false}\n'
            b'{"detected":true}\n'
        )
        errors = io.StringIO()
        self.assertEqual(
            self.module.relay_stream(source, StubRelay(), errors), 1
        )
        self.assertEqual(delivered, [{"detected": True}, {"detected": False}])
        self.assertEqual(errors.getvalue(), "presence delivery unavailable\n")

        errors = io.StringIO()
        self.assertEqual(
            self.module.relay_stream(io.BytesIO(b"X" * 513), StubRelay(), errors),
            2,
        )
        self.assertEqual(errors.getvalue(), "presence input rejected\n")


if __name__ == "__main__":
    unittest.main()

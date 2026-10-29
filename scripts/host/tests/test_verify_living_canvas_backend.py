import contextlib
import importlib.util
import io
import pathlib
import sys
import unittest
import urllib.request
from unittest import mock


SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "verify_living_canvas_backend.py"
SPEC = importlib.util.spec_from_file_location("verify_living_canvas_backend", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class VerifyLivingCanvasBackendTest(unittest.TestCase):
    def test_rejects_public_cleartext_or_ambiguous_backend_urls(self):
        for value in (
            "http://api.example.invalid",
            "http://8.8.8.8:8765",
            "http://private-host.local:8765",
            "https://user:password@example.invalid",
            "https://example.invalid/?private=1",
            "https://example.invalid/#private",
            "https://example.invalid/prefix",
            "https://example.invalid\n/other",
            "file:///tmp/private",
            "https://example.invalid:bad",
        ):
            with self.subTest(value=value), self.assertRaisesRegex(
                RuntimeError, "invalid backend URL"
            ):
                MODULE.validate_base_url(value)

    def test_accepts_https_or_numeric_private_http_backend_urls(self):
        for value in (
            "https://api.example.invalid",
            "https://api.example.invalid:8443",
            "http://127.0.0.1:8765",
            "http://10.42.0.1:8765",
            "http://172.20.10.1:8765",
            "http://192.168.8.20:8765",
        ):
            with self.subTest(value=value):
                self.assertEqual(MODULE.validate_base_url(value + "/"), value)

    def test_default_opener_disables_environment_proxies_and_redirects(self):
        sentinel = object()
        with mock.patch.object(
            MODULE.urllib.request, "build_opener", return_value=sentinel
        ) as build_opener:
            self.assertIs(MODULE.direct_opener(), sentinel)
        handlers = build_opener.call_args.args
        proxy = next(
            handler for handler in handlers
            if isinstance(handler, MODULE.urllib.request.ProxyHandler)
        )
        self.assertEqual(proxy.proxies, {})
        self.assertTrue(any(isinstance(handler, MODULE.NoRedirect)
                            for handler in handlers))

    def test_product_post_uses_owner_device_bearer_without_printing_it(self):
        token = "lc-owner-verifier-device-token-0123456789abcdef"
        response = mock.MagicMock()
        response.__enter__.return_value.read.return_value = b'{"ok": true}'
        response.__enter__.return_value.__exit__.return_value = None

        opener = mock.MagicMock()
        opener.open.return_value = response
        self.assertEqual(
            MODULE.request_json(
                "https://api.example.invalid",
                "/v1/session",
                {"device_id": "gemini-s1"},
                device_token=token,
                opener=opener,
            ),
            {"ok": True},
        )

        request = opener.open.call_args.args[0]
        self.assertEqual(request.get_header("Authorization"), "Bearer " + token)

    def test_http_error_body_and_redirect_target_are_not_exposed_or_followed(self):
        private_body = io.BytesIO(b"PRIVATE_UPSTREAM_DETAIL")
        error = MODULE.urllib.error.HTTPError(
            "https://api.example.invalid/v1/session",
            307,
            "Temporary Redirect",
            {"Location": "https://proxy.invalid/private"},
            private_body,
        )
        opener = mock.MagicMock()
        opener.open.side_effect = error
        with self.assertRaises(RuntimeError) as failure:
            MODULE.request_json(
                "https://api.example.invalid",
                "/v1/session",
                {"device_id": "gemini-s1"},
                device_token="lc-owner-verifier-device-token-0123456789abcdef",
                opener=opener,
            )
        rendered = str(failure.exception)
        self.assertIn("HTTP 307", rendered)
        self.assertNotIn("PRIVATE_UPSTREAM_DETAIL", rendered)
        self.assertNotIn("proxy.invalid", rendered)
        self.assertEqual(opener.open.call_count, 1)

    def test_confirmed_compact_link_must_redirect_to_exact_eleme_url(self):
        target = "https://h5.ele.me/search/?keyword=%E7%9B%96%E9%A5%AD"
        error = MODULE.urllib.error.HTTPError(
            "https://api.example.invalid/c/123456789012",
            307,
            "Temporary Redirect",
            {"Location": target},
            None,
        )
        opener = mock.MagicMock()
        opener.open.side_effect = error
        MODULE.verify_redirect(
            "https://api.example.invalid", "123456789012", target,
            opener=opener,
        )

    def test_require_model_rejects_rules_fallback(self):
        with self.assertRaisesRegex(RuntimeError, "model-backed"):
            MODULE.validate_source("rules", True, require_model=True)

        MODULE.validate_source("rules", True, require_model=False)
        MODULE.validate_source("model", False, require_model=True)

    def test_verifier_exercises_the_same_dialogue_route_as_gemini_s1(self):
        calls = []

        def fake_request_json(base_url, path, body=None, device_token=""):
            calls.append((base_url, path, body, device_token))
            responses = {
                "/health": {
                    "service": "living-canvas-decision-backend",
                    "status": "ok",
                },
                "/v1/session": {"session_id": "meal_123456789012"},
                "/v1/dialogue": {
                    "session_id": "meal_123456789012",
                    "state": "candidate",
                    "choice": "takeout",
                    "recommendation": {
                        "candidate": {"candidate_id": "warm_rice"},
                        "reason": "within the bounded catalog",
                        "source": "model",
                        "rules_fallback": False,
                    },
                    "spoken_reply": "请确认后再继续。",
                    "requires_handoff": True,
                    "role_contributions": [
                        {"role_id": role, "summary": "bounded"}
                        for role in ("beagle", "cat", "pigeon", "pig", "alien")
                    ],
                },
                "/v1/device/event": {"state": "confirming"},
                "/v1/confirm": {
                    "state": "handed_off",
                    "external_url": "https://h5.ele.me/search/?keyword=warm_rice",
                    "requires_user_payment": True,
                },
            }
            if path not in responses:
                raise RuntimeError(f"unexpected route: {path}")
            return responses[path]

        argv = [str(SCRIPT), "--base-url", "https://api.example.invalid"]
        output = io.StringIO()
        with mock.patch.object(MODULE, "request_json", side_effect=fake_request_json), \
             mock.patch.object(MODULE, "verify_redirect") as verify_redirect, \
             mock.patch.object(sys, "argv", argv), \
             mock.patch.dict(
                 MODULE.os.environ,
                 {"LIVING_CANVAS_VOICE_DEVICE_TOKEN":
                  "lc-owner-verifier-device-token-0123456789abcdef"},
             ), \
             contextlib.redirect_stdout(output):
            self.assertEqual(MODULE.main(), 0)

        paths = [path for _, path, _, _ in calls]
        self.assertIn("/v1/dialogue", paths)
        self.assertNotIn("/v1/input", paths)
        dialogue_body = next(
            body for _, path, body, _ in calls if path == "/v1/dialogue"
        )
        self.assertEqual(dialogue_body["choice"], "takeout")
        self.assertTrue(dialogue_body["transcript"].strip())
        post_tokens = [
            device_token for _, _, body, device_token in calls if body is not None
        ]
        self.assertTrue(post_tokens)
        self.assertTrue(all(post_tokens))
        verify_redirect.assert_called_once_with(
            "https://api.example.invalid",
            "123456789012",
            "https://h5.ele.me/search/?keyword=warm_rice",
        )
        self.assertIn("source=model", output.getvalue())
        self.assertIn("roles=5", output.getvalue())


if __name__ == "__main__":
    unittest.main()

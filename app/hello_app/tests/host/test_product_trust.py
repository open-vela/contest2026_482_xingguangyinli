#!/usr/bin/env python3
"""Verify the embedded Living Canvas product trust anchor exactly."""

from __future__ import annotations

import hashlib
import pathlib
import re
import ssl


EXPECTED_ISRG_ROOT_X1_SHA256 = (
    "96bcec06264976f37460779acf28c5a7"
    "cfe8a3c0aae11a8ffc ee05c0bddf08c6".replace(" ", "")
)


def main() -> None:
    source = pathlib.Path(__file__).resolve().parents[2] / "src" / "lc_product_trust.c"
    text = source.read_text(encoding="utf-8")
    chunks = re.findall(
        r'^\s*"((?:[^"\\]|\\.)*)"\s*;?\s*$', text, re.MULTILINE
    )
    pem = "".join(bytes(chunk, "utf-8").decode("unicode_escape") for chunk in chunks)

    assert pem.startswith("-----BEGIN CERTIFICATE-----\n")
    assert pem.endswith("-----END CERTIFICATE-----\n")
    der = ssl.PEM_cert_to_DER_cert(pem)
    fingerprint = hashlib.sha256(der).hexdigest()
    assert fingerprint == EXPECTED_ISRG_ROOT_X1_SHA256, fingerprint
    print("PASS: pinned ISRG Root X1 trust anchor")


if __name__ == "__main__":
    main()

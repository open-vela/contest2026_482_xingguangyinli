import hashlib
from pathlib import Path
import re
import subprocess


REPO = Path(__file__).resolve().parents[3]
APACHE_2_SHA256 = (
    "c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4"
)


def delivery_candidates():
    """Return every non-ignored file that would be eligible for submission."""
    output = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        cwd=REPO, check=True, capture_output=True,
    ).stdout
    candidates = [Path(value.decode("utf-8", "surrogateescape"))
                  for value in output.split(b"\0") if value]
    # Index entries deleted in the working tree are not part of the proposed
    # delivery. Keep broken/real symlinks so the artifact guard can reject them.
    return [relative for relative in candidates
            if (REPO / relative).exists() or (REPO / relative).is_symlink()]


def test_top_level_license_is_canonical_apache_2():
    license_file = REPO / "LICENSE"
    assert license_file.is_file(), "submission must include a top-level LICENSE"
    assert hashlib.sha256(license_file.read_bytes()).hexdigest() == APACHE_2_SHA256


def test_dinner_assistant_skill_is_present_and_named():
    skill = REPO / "app" / "hello_app" / "skills" / "dinner_assistant" / "SKILL.md"
    text = skill.read_text(encoding="utf-8")
    assert text.startswith("---\n")
    assert "name: dinner-assistant-runtime-contract" in text
    assert "description: Use when" in text


def test_delivery_text_does_not_disclose_private_infrastructure():
    private_markers = (
        "".join(("/", "Users", "/")),
        "".join(("/", "home", "/")),
    )
    violations = []
    encoded_markers = tuple(value.encode() for value in private_markers)
    for relative in delivery_candidates():
        if "tests" in relative.parts or relative == Path(
                "scripts/device/collect_gemini_baseline.sh"):
            continue
        path = REPO / relative
        if not path.is_file() or path.is_symlink():
            continue
        try:
            content = path.read_bytes()
        except OSError:
            continue
        if any(marker in content for marker in encoded_markers):
            violations.append(str(relative))
    assert not violations, (
        "delivery files disclose private infrastructure: " +
        ", ".join(violations)
    )


def test_delivery_tree_excludes_private_and_generated_artifacts():
    tracked_modes = {}
    listed = subprocess.run(
        ["git", "ls-files", "-s", "-z"], cwd=REPO, check=True,
        capture_output=True,
    ).stdout.split(b"\0")
    for raw_line in listed:
        if not raw_line:
            continue
        metadata, raw_path = raw_line.split(b"\t", 1)
        tracked_modes[Path(raw_path.decode("utf-8", "surrogateescape"))] = \
            metadata.split(b" ", 1)[0].decode("ascii")
    forbidden_suffixes = {
        ".a", ".apk", ".bin", ".dll", ".dylib", ".elf", ".exe",
        ".fex", ".gz", ".hex", ".img", ".key", ".map", ".o",
        ".p12", ".pem", ".pfx", ".pyc", ".qcow2", ".so", ".tar",
        ".zip",
    }
    violations = []
    for candidate in delivery_candidates():
        path = REPO / candidate
        if tracked_modes.get(candidate) == "120000" or path.is_symlink():
            violations.append(str(candidate) + " (symlink)")
        if candidate.suffix.lower() in forbidden_suffixes:
            violations.append(str(candidate))
        if candidate.name == ".env" or (
                candidate.name.startswith(".env.") and
                candidate.name != ".env.example"):
            violations.append(str(candidate))
        if candidate.name in {"id_rsa", "id_ed25519"}:
            violations.append(str(candidate))
    assert not violations, "forbidden delivery artifacts: " + ", ".join(violations)


def test_delivery_tree_has_no_high_confidence_secrets():
    patterns = {
        "private-key": re.compile(
            rb"-----BEGIN (?:RSA |OPENSSH |EC |DSA )?PRIVATE KEY-----"),
        "aws-access-key": re.compile(rb"AKIA[0-9A-Z]{16}"),
        "github-token": re.compile(
            rb"(?:github_pat_[A-Za-z0-9_]{50,}|gh[pousr]_[A-Za-z0-9]{30,})"),
        "google-api-key": re.compile(rb"AIza[0-9A-Za-z_-]{35}"),
        "slack-token": re.compile(rb"xox[baprs]-[A-Za-z0-9-]{20,}"),
        "provider-key": re.compile(rb"sk-[A-Za-z0-9_-]{32,}"),
    }
    safe_fixture_markers = (b"test", b"synthetic", b"not-a-real", b"placeholder")
    violations = []
    for relative in delivery_candidates():
        path = REPO / relative
        if not path.is_file() or path.is_symlink():
            continue
        try:
            content = path.read_bytes()
        except OSError:
            continue
        for label, pattern in patterns.items():
            for match in pattern.finditer(content):
                token = match.group(0).lower()
                if label == "provider-key" and any(
                        marker in token for marker in safe_fixture_markers):
                    continue
                violations.append(f"{relative} ({label})")
    assert not violations, "possible secrets in delivery files: " + ", ".join(violations)

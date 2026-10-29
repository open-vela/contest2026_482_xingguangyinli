import importlib.util
import pathlib
import stat
import subprocess
import sys

import pytest


SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "apply_gemini_roadshow_config.py"


def load_module():
    spec = importlib.util.spec_from_file_location("roadshow_config", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def private_file(path: pathlib.Path, value: str) -> pathlib.Path:
    path.write_text(value, encoding="ascii")
    path.chmod(0o600)
    return path


@pytest.fixture
def valid_files(tmp_path):
    owner = private_file(
        tmp_path / "owner-token",
        "lc-owner-0123456789abcdef0123456789abcdef\n",
    )
    accessory = private_file(
        tmp_path / "accessory-token",
        "lc-accessory-0123456789abcdef0123456789abcdef\n",
    )
    keys = private_file(
        tmp_path / "local-keys",
        "cat_a=" + "a" * 64 + "\ncat_b=" + "b" * 64 + "\n",
    )
    return owner, accessory, keys


@pytest.mark.parametrize(
    "url",
    (
        "https://192.168.50.20:8765",
        "http://127.0.0.1:8765",
        "http://169.254.1.2:8765",
        "http://8.8.8.8:8765",
        "http://host.local:8765",
        "http://user@192.168.50.20:8765",
        "http://192.168.50.20:80",
        "http://192.168.50.20:8765/path",
        "http://192.168.50.20:8765?x=1",
    ),
)
def test_rejects_non_private_backend_urls(url):
    module = load_module()
    with pytest.raises(module.ConfigurationError, match="invalid_backend_url"):
        module.validate_backend_url(url)


def test_accepts_canonical_private_backend_url():
    module = load_module()
    assert (
        module.validate_backend_url("http://192.168.50.20:8765")
        == b"http://192.168.50.20:8765\n"
    )


@pytest.mark.parametrize("mutation", ("relative", "symlink", "mode", "multiline"))
def test_rejects_unsafe_secret_files(tmp_path, mutation):
    module = load_module()
    target = private_file(tmp_path / "secret", "lc-owner-" + "a" * 32 + "\n")
    selected = target
    if mutation == "relative":
        selected = pathlib.Path("secret")
    elif mutation == "symlink":
        selected = tmp_path / "link"
        selected.symlink_to(target)
    elif mutation == "mode":
        selected.chmod(0o644)
    elif mutation == "multiline":
        private_file(selected, "first\nsecond\n")
    with pytest.raises(module.ConfigurationError, match="invalid_private_file"):
        module.read_private_file(selected)


def test_validates_independent_tokens_and_pairing_keys(valid_files):
    module = load_module()
    owner_path, accessory_path, keys_path = valid_files
    config = module.load_config(
        "http://192.168.50.20:8765",
        owner_path,
        accessory_path,
        keys_path,
    )
    assert set(config) == set(module.REMOTE_TARGETS)
    assert config["backend_url"].startswith(b"http://")
    assert config["local_accessory_keys"].count(b"\n") == 2


def test_rejects_reused_tokens_and_pairing_keys(tmp_path, valid_files):
    module = load_module()
    owner_path, accessory_path, keys_path = valid_files
    private_file(accessory_path, owner_path.read_text(encoding="ascii"))
    with pytest.raises(module.ConfigurationError, match="reused_secret"):
        module.load_config(
            "http://192.168.50.20:8765",
            owner_path,
            accessory_path,
            keys_path,
        )
    private_file(accessory_path, "lc-accessory-" + "c" * 32 + "\n")
    private_file(
        keys_path,
        "cat_a=" + "d" * 64 + "\ncat_b=" + "d" * 64 + "\n",
    )
    with pytest.raises(module.ConfigurationError, match="reused_secret"):
        module.load_config(
            "http://192.168.50.20:8765",
            owner_path,
            accessory_path,
            keys_path,
        )


class FakeDevice:
    def __init__(self, initial=None, fail_replace_index=None, corrupt_stage=None):
        self.files = dict(initial or {})
        self.calls = []
        self.replace_count = 0
        self.fail_replace_index = fail_replace_index
        self.corrupt_stage = corrupt_stage

    def verify_target(self):
        self.calls.append(("verify_target",))

    def read(self, remote):
        self.calls.append(("read", remote))
        if remote not in self.files:
            return None
        return self.files[remote]

    def stage(self, remote, content):
        self.calls.append(("stage", remote))
        self.files[remote] = (
            b"corrupt" if self.corrupt_stage == remote else bytes(content)
        )

    def replace(self, staged, target):
        self.calls.append(("replace", staged, target))
        self.replace_count += 1
        if self.fail_replace_index == self.replace_count:
            raise module.ConfigurationError("device_replace_failed")
        self.files[target] = self.files.pop(staged)

    def restore(self, target, previous):
        self.calls.append(("restore", target, previous is not None))
        if previous is None:
            self.files.pop(target, None)
        else:
            self.files[target] = previous

    def cleanup(self, remote):
        self.calls.append(("cleanup", remote))
        self.files.pop(remote, None)


def test_plan_is_read_only_and_never_contains_secret_values(tmp_path, valid_files, capsys):
    global module
    module = load_module()
    owner_path, accessory_path, keys_path = valid_files
    config = module.load_config(
        "http://192.168.50.20:8765", owner_path, accessory_path, keys_path
    )
    device = FakeDevice()
    result = module.apply_transaction(
        device, config, tmp_path / "backup", apply=False, app_stopped=False
    )
    output = capsys.readouterr().out
    assert result == "PLAN"
    assert device.calls == [("verify_target",)]
    for content in config.values():
        assert content.strip().decode("ascii") not in output
    assert "sha256" in output


def test_apply_writes_all_files_and_records_locked_backup(tmp_path, valid_files):
    global module
    module = load_module()
    owner_path, accessory_path, keys_path = valid_files
    config = module.load_config(
        "http://192.168.50.20:8765", owner_path, accessory_path, keys_path
    )
    initial = {
        remote: ("old-" + name).encode("ascii")
        for name, remote in module.REMOTE_TARGETS.items()
    }
    device = FakeDevice(initial)
    backup = tmp_path / "fresh backup"
    assert module.apply_transaction(
        device, config, backup, apply=True, app_stopped=True
    ) == "APPLIED"
    assert stat.S_IMODE(backup.stat().st_mode) == 0o700
    assert all(
        stat.S_IMODE(path.stat().st_mode) == 0o600
        for path in backup.iterdir()
        if path.is_file()
    )
    for name, remote in module.REMOTE_TARGETS.items():
        assert device.files[remote] == config[name]


def test_staging_mismatch_causes_zero_formal_replaces(tmp_path, valid_files):
    global module
    module = load_module()
    owner_path, accessory_path, keys_path = valid_files
    config = module.load_config(
        "http://192.168.50.20:8765", owner_path, accessory_path, keys_path
    )
    first_remote = next(iter(module.REMOTE_TARGETS.values())) + ".next"
    device = FakeDevice(corrupt_stage=first_remote)
    with pytest.raises(module.ConfigurationError, match="staging_readback_mismatch"):
        module.apply_transaction(
            device, config, tmp_path / "backup", apply=True, app_stopped=True
        )
    assert not any(call[0] == "replace" for call in device.calls)


def test_partial_replace_failure_restores_original_existence(tmp_path, valid_files):
    global module
    module = load_module()
    owner_path, accessory_path, keys_path = valid_files
    config = module.load_config(
        "http://192.168.50.20:8765", owner_path, accessory_path, keys_path
    )
    targets = list(module.REMOTE_TARGETS.values())
    initial = {targets[0]: b"old-backend", targets[2]: b"old-accessory"}
    device = FakeDevice(initial, fail_replace_index=3)
    with pytest.raises(module.ConfigurationError, match="device_replace_failed"):
        module.apply_transaction(
            device, config, tmp_path / "backup", apply=True, app_stopped=True
        )
    assert {key: value for key, value in device.files.items() if not key.endswith(".next")} == initial


def test_apply_requires_explicit_stopped_application_confirmation(tmp_path, valid_files):
    global module
    module = load_module()
    owner_path, accessory_path, keys_path = valid_files
    config = module.load_config(
        "http://192.168.50.20:8765", owner_path, accessory_path, keys_path
    )
    with pytest.raises(module.ConfigurationError, match="application_state_unconfirmed"):
        module.apply_transaction(
            FakeDevice(), config, tmp_path / "backup", apply=True, app_stopped=False
        )


def test_cli_plan_uses_read_only_adb_probe_and_redacts_secrets(tmp_path, valid_files):
    owner_path, accessory_path, keys_path = valid_files
    log = tmp_path / "adb.log"
    adb = tmp_path / "adb"
    adb.write_text(
        "#!/bin/sh\n"
        f"printf '%s\\n' \"$*\" >> {log!s}\n"
        "if [ \"$*\" = 'devices -l' ]; then\n"
        "  printf 'List of devices attached\\n1234 device model:NuttX\\n'\n"
        "  exit 0\n"
        "fi\n"
        "exit 90\n",
        encoding="utf-8",
    )
    adb.chmod(0o700)
    result = subprocess.run(
        [
            sys.executable,
            str(SCRIPT),
            "--adb",
            str(adb),
            "--serial",
            "1234",
            "--backend-url",
            "http://192.168.50.20:8765",
            "--owner-token-file",
            str(owner_path),
            "--accessory-token-file",
            str(accessory_path),
            "--local-keys-file",
            str(keys_path),
            "--backup-dir",
            str(tmp_path / "backup"),
            "--plan",
        ],
        capture_output=True,
        text=True,
        timeout=10,
    )
    assert result.returncode == 0, result.stderr
    assert log.read_text(encoding="utf-8").splitlines() == ["devices -l"]
    assert "lc-owner-" not in result.stdout
    assert "cat_a=" not in result.stdout
    assert '"status": "PLAN"' in result.stdout


def test_adb_exists_handles_nuttx_missing_file_with_zero_exit(tmp_path):
    module = load_module()
    adb = tmp_path / "adb"
    adb.write_text(
        "#!/bin/sh\n"
        "case \"$*\" in\n"
        "  *backend_url) printf ' -rw-r--r--          28 backend_url\\n' ;;\n"
        "  *voice_device_token) printf 'nsh: ls: stat failed: 2\\n' ;;\n"
        "  *) exit 91 ;;\n"
        "esac\n"
        "exit 0\n",
        encoding="utf-8",
    )
    adb.chmod(0o700)
    device = module.AdbDevice(adb, "1234")

    assert device._exists("/data/living_canvas/backend_url") is True
    assert device._exists("/data/living_canvas/voice_device_token") is False

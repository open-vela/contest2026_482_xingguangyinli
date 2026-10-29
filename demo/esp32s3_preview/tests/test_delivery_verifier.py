import hashlib
import importlib.util
import pathlib
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "verify_device_delivery.py"


def load_verifier():
    spec = importlib.util.spec_from_file_location("delivery_verifier", MODULE_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class DeliveryVerifierTest(unittest.TestCase):
    def make_delivery(self, root: pathlib.Path, module) -> pathlib.Path:
        delivery = root / "delivery"
        delivery.mkdir()
        for name in module.ARTIFACTS:
            artifact = delivery / name
            artifact.write_bytes((name + "\n").encode("ascii"))
            artifact.chmod(0o600)
        self.write_manifest(delivery, module)
        return delivery

    def write_manifest(self, delivery: pathlib.Path, module) -> None:
        lines = []
        for name in module.ARTIFACTS:
            value = hashlib.sha256((delivery / name).read_bytes()).hexdigest()
            lines.append(f"{value}  {name}")
        manifest = delivery / "checksums.sha256"
        manifest.write_text("\n".join(lines) + "\n", encoding="ascii")
        manifest.chmod(0o600)

    def test_accepts_a_small_non_overlapping_delivery(self):
        module = load_verifier()
        with tempfile.TemporaryDirectory() as directory:
            delivery = self.make_delivery(pathlib.Path(directory), module)
            module.verify(delivery)

    def test_rejects_an_artifact_that_overlaps_the_next_flash_region(self):
        module = load_verifier()
        with tempfile.TemporaryDirectory() as directory:
            delivery = self.make_delivery(pathlib.Path(directory), module)
            application = delivery / "application.bin"
            with application.open("wb") as stream:
                stream.truncate(0x600001)
            application.chmod(0o600)
            self.write_manifest(delivery, module)

            with self.assertRaisesRegex(ValueError, "delivery_artifact_too_large"):
                module.verify(delivery)


if __name__ == "__main__":
    unittest.main()

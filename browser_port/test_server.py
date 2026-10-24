"""Unit and HTTP integration tests for the local-only Prism3D browser server.

Uses synthetic files only. No proprietary executables or game archives.
Run: python -m unittest discover -s browser_port -p 'test_*.py' -v
"""
from __future__ import annotations

import io
import tempfile
import threading
import unittest
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import Request, urlopen
from zipfile import ZipFile

from server import GameServer, validate_archive


def make_zip(destination: Path, entries: dict[str, bytes]) -> None:
    with ZipFile(destination, "w") as archive:
        for filename, data in entries.items():
            archive.writestr(filename, data)


class ArchiveValidationTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "test.zip"

    def test_installed_game_at_zip_root(self) -> None:
        make_zip(self.path, {
            "prism3d.exe": b"MZ",
            "p3shared.dll": b"MZ",
            "gfxshared.dll": b"MZ",
            "sndshared.dll": b"MZ",
            "inpshared.dll": b"MZ",
            "base.scs": b"data",
        })
        result = validate_archive(self.path, "game")
        self.assertEqual(result["files"], 6)
        self.assertEqual(result["warnings"], [])

    def test_reject_extra_directory(self) -> None:
        make_zip(self.path, {"Game/prism3d.exe": b"MZ"})
        with self.assertRaisesRegex(ValueError, "raiz"):
            validate_archive(self.path, "game")

    def test_reject_path_traversal(self) -> None:
        make_zip(self.path, {"../prism3d.exe": b"MZ"})
        with self.assertRaisesRegex(ValueError, "proibido"):
            validate_archive(self.path, "game")

    def test_reject_duplicate_windows_names(self) -> None:
        make_zip(self.path, {"prism3d.exe": b"MZ", "Prism3D.EXE": b"MZ"})
        with self.assertRaisesRegex(ValueError, "duplicado"):
            validate_archive(self.path, "game")

    def test_wine_root_does_not_require_game(self) -> None:
        make_zip(self.path, {"bin/wine": b"guest"})
        self.assertEqual(validate_archive(self.path, "wine")["files"], 1)

    def test_missing_game_data_raises_warning(self) -> None:
        make_zip(self.path, {"prism3d.exe": b"MZ"})
        result = validate_archive(self.path, "game")
        self.assertTrue(any("scs" in x for x in result["warnings"]))

    def test_invalid_zip(self) -> None:
        self.path.write_bytes(b"not a ZIP")
        with self.assertRaises(ValueError):
            validate_archive(self.path, "wine")


class LocalHTTPTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name) / "web"
        root.mkdir()
        (root / "index.html").write_text("<title>EmuWasm</title>", encoding="utf-8")
        (root / "boxedwine.html").write_text("<title>BoxedWine</title>", encoding="utf-8")
        (root / "boxedwine.js").write_text("console.log('mock');", encoding="utf-8")
        (root / "boxedwine.wasm").write_bytes(bytes([0, 97, 115, 109]))
        self.server = GameServer(("127.0.0.1", 0), root, root / ".local-game-data")
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.addCleanup(self.server.server_close)
        self.addCleanup(self.server.shutdown)
        self.base = f"http://127.0.0.1:{self.server.server_port}"

    def call(self, endpoint: str, body: bytes | None = None,
             kind: str = "application/zip", origin: str = "") -> tuple[int, bytes]:
        headers = {"Content-Type": kind}
        if origin:
            headers["Origin"] = origin
        request = Request(self.base + endpoint, data=body, headers=headers)
        try:
            with urlopen(request, timeout=5) as response:
                return response.status, response.read()
        except HTTPError as error:
            return error.code, error.read()

    def test_can_serve_wasm_with_correct_mime_and_cors_isolation(self) -> None:
        with urlopen(self.base + "/boxedwine.wasm", timeout=5) as response:
            self.assertEqual(response.headers["Content-Type"], "application/wasm")
            self.assertEqual(response.headers["Cross-Origin-Opener-Policy"], "same-origin")
            self.assertEqual(response.headers["Cross-Origin-Embedder-Policy"], "require-corp")

    def test_status_initially_empty(self) -> None:
        import json
        code, data = self.call("/api/status")
        self.assertEqual(code, 200)
        result = json.loads(data)
        self.assertFalse(result["archives"]["wine"]["ready"])
        self.assertTrue(result["runtime"]["boxedwine.wasm"])

    def test_upload_wine_and_game_then_serve_archives(self) -> None:
        import json
        for kind, entries in (
            ("wine", {"bin/wine": b"guest executable"}),
            ("game", {"prism3d.exe": b"MZ", "base.scs": b"fake assets"}),
        ):
            stream = io.BytesIO()
            with ZipFile(stream, "w") as archive:
                for name, value in entries.items():
                    archive.writestr(name, value)
            code, data = self.call("/api/upload/" + kind, stream.getvalue(),
                                   origin=self.base)
            self.assertEqual(code, 200, data)
            self.assertTrue(json.loads(data)["ok"])
        code, data = self.call("/api/status")
        self.assertEqual(code, 200)
        self.assertTrue(json.loads(data)["archives"]["game"]["ready"])
        code, data = self.call("/user-content/prism3d.zip")
        self.assertEqual(code, 200)
        self.assertGreater(len(data), 10)

    def test_block_cross_origin_upload(self) -> None:
        code, _ = self.call("/api/upload/game", b"ZIP", origin="https://example.com")
        self.assertEqual(code, 403)

    def test_block_static_traversal(self) -> None:
        code, _ = self.call("/%2e%2e/server.py")
        self.assertEqual(code, 404)

    def test_block_local_data_directory_listing(self) -> None:
        code, _ = self.call("/.local-game-data/private.zip")
        self.assertEqual(code, 404)

    def test_unsupported_mime(self) -> None:
        code, _ = self.call("/api/upload/wine", b"hello", kind="text/plain")
        self.assertEqual(code, 415)


if __name__ == "__main__":
    unittest.main()

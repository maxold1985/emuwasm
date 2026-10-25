#!/usr/bin/env python3
"""Private localhost server for the EmuWasm/Prism3D browser experiment.

Runs upstream BoxedWine's Emscripten HTML/WASM and accepts two user-owned ZIPs:
a complete compatible Wine filesystem and the installed game's ZIP.
Never exposes uploads on a public network interface; no game data is bundled.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import threading
import stat
import tempfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path, PurePosixPath
from urllib.parse import unquote, urlsplit
from zipfile import BadZipFile, ZipFile
import hashlib

MAX_UPLOAD = 3 * 1024**3  # ZIP bytes; game file packager limits expanded data to <4 GiB.
MAX_UNCOMPRESSED = 8 * 1024**3
MAX_MEMBERS = 200_000
READ_CHUNK = 1024 * 1024
MAX_DIAGNOSTIC_POST = 24 * 1024
MAX_DIAGNOSTIC_LOG_BYTES = 1024 * 1024
ALLOWED_UPLOADS = {"wine": "boxedwine.zip", "game": "prism3d.zip"}
WINE_ARTIFACT = "boxedwine-linux-i386-wine11-rootfs.zip"
MIME = {
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".mjs": "text/javascript; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".wasm": "application/wasm",
    ".zip": "application/zip",
    ".png": "image/png",
    ".svg": "image/svg+xml",
    ".ico": "image/x-icon",
    ".json": "application/json; charset=utf-8",
    ".txt": "text/plain; charset=utf-8",
}


def validate_archive(path: Path, kind: str) -> dict:
    """Read ZIP central directory, rejecting unsafe entries without extracting files."""
    if kind not in ALLOWED_UPLOADS:
        raise ValueError("Tipo de arquivo desconhecido")
    try:
        with ZipFile(path) as zf:
            members = zf.infolist()
            if not members or len(members) > MAX_MEMBERS:
                raise ValueError("ZIP vazio ou com arquivos demais")
            normalized = set()
            total = 0
            for item in members:
                name = item.filename
                parts = PurePosixPath(name).parts
                if (not name or name.startswith("/") or "\\" in name or
                        any(part in ("", ".", "..") for part in parts) or
                        ":" in name or "\x00" in name):
                    raise ValueError(f"Caminho proibido no ZIP: {name!r}")
                # Reject Unix symlink entries: do not mount host indirections.
                if (item.create_system == 3 and
                        stat.S_IFMT(item.external_attr >> 16) == stat.S_IFLNK):
                    raise ValueError(f"Link simbolico nao permitido: {name!r}")
                key = name.rstrip("/").casefold()
                if key in normalized:
                    raise ValueError(f"Nome duplicado no ZIP: {name!r}")
                normalized.add(key)
                total += item.file_size
                if total > MAX_UNCOMPRESSED:
                    raise ValueError("ZIP excede limite expandido de 8 GiB")
                if item.flag_bits & 0x1:
                    raise ValueError("ZIP criptografado nao suportado")
            warnings = []
            if kind == "wine":
                # BoxedWine launches /bin/wine INSIDE its guest Linux root.
                # A ZIP of Windows DLLs, a Wine source tree, or a ZIP with
                # a parent directory cannot satisfy this requirement.
                if "bin/wine" not in normalized:
                    nested = sorted(n for n in normalized if n.endswith("/bin/wine"))
                    example = nested[0] if nested else None
                    detail = (
                        f"Encontrado {example!r}, mas o ZIP tem pasta extra. "
                        "A pasta bin/ precisa ficar na raiz do ZIP."
                        if example else
                        "Nao existe bin/wine na raiz do ZIP. "
                        "Este arquivo nao e um sistema Linux/Wine compativel "
                        "com BoxedWine; DLLs Windows nao substituem /bin/wine."
                    )
                    raise ValueError("Wine ZIP incompativel: " + detail)
            if kind == "game":
                if "prism3d.exe" not in normalized:
                    raise ValueError("prism3d.exe precisa estar na raiz do ZIP")
                for filename in ("p3shared.dll", "gfxshared.dll", "sndshared.dll", "inpshared.dll"):
                    if filename not in normalized:
                        warnings.append(f"Arquivo esperado ausente: {filename}")
                if not any(n.endswith(".scs") for n in normalized):
                    warnings.append("Nenhum .scs encontrado; o jogo pode estar incompleto")
            return {
                "kind": kind,
                "files": len(members),
                "compressedBytes": path.stat().st_size,
                "uncompressedBytes": total,
                "warnings": warnings,
            }
    except BadZipFile as exc:
        raise ValueError("O arquivo selecionado nao e um ZIP valido") from exc



def import_wine_artifact(artifact: Path, destination: Path) -> bool:
    """Unwrap the GitHub Actions artifact (outer ZIP containing boxedwine.zip).

    Only the exact expected member is accepted; never extract arbitrary paths.
    """
    if not artifact.is_file():
        return False
    with ZipFile(artifact) as outer:
        entries = [member for member in outer.infolist() if not member.is_dir()]
        if len(entries) != 1 or entries[0].filename != "boxedwine.zip":
            raise ValueError("Artefato Wine invalido: esperado somente boxedwine.zip")
        member = entries[0]
        if member.file_size > MAX_UPLOAD:
            raise ValueError("Wine ZIP excede 3 GiB")
        fd, temporary = tempfile.mkstemp(prefix="wine-artifact-", suffix=".zip",
                                        dir=destination.parent)
        tmp = Path(temporary)
        try:
            with os.fdopen(fd, "wb") as target, outer.open(member) as source:
                shutil.copyfileobj(source, target, READ_CHUNK)
            validate_archive(tmp, "wine")
            os.replace(tmp, destination)
        finally:
            tmp.unlink(missing_ok=True)
    return True


def ensure_local_wine(user_data: Path) -> str | None:
    """Import a previously downloaded Actions artifact when Wine is requested.

    No remote requests or GitHub credentials: the artifact must be on disk.
    """
    destination = user_data / ALLOWED_UPLOADS["wine"]
    artifact = user_data / WINE_ARTIFACT
    if not artifact.is_file():
        return None
    # Prefer a newly downloaded build artifact over an older valid ZIP.
    # Store its digest to avoid re-importing on every HEAD/GET/status request.
    marker = user_data / ".wine-artifact.sha256"
    try:
        with artifact.open("rb") as source:
            digest = hashlib.file_digest(source, "sha256").hexdigest()
        if destination.is_file() and marker.is_file() and marker.read_text().strip() == digest:
            try:
                validate_archive(destination, "wine")
                return None
            except (ValueError, OSError):
                pass
        import_wine_artifact(artifact, destination)
        marker.write_text(digest, encoding="ascii")
        print(f"[browser-port] Wine importado do artefato local: {artifact.name}", flush=True)
        return None
    except (ValueError, OSError, BadZipFile) as exc:
        return f"Falha ao importar artefato local Wine: {exc}"


class GameServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address: tuple[str, int], webroot: Path, user_data: Path):
        super().__init__(address, GameHandler)
        self.webroot = webroot.resolve()
        self.user_data = user_data.resolve()
        self.user_data.mkdir(parents=True, exist_ok=True)
        self.diagnostics_dir = self.user_data / ".diagnostics"
        self.diagnostics_dir.mkdir(parents=True, exist_ok=True)
        self.diagnostics_lock = threading.Lock()
        self.browser_log = self.diagnostics_dir / "browser-console.log"


class GameHandler(BaseHTTPRequestHandler):
    server: GameServer

    def valid_host(self) -> bool:
        # Avoid DNS rebinding to loopback: users must address the private server
        # explicitly as 127.0.0.1 rather than as an attacker-controlled domain.
        return self.headers.get("Host", "") == f"127.0.0.1:{self.server.server_port}"

    def common_headers(self) -> None:
        # Needed for newer upstream BoxedWine Emscripten/pthread builds.
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("X-Frame-Options", "DENY")
        self.send_header("Cache-Control", "no-store")

    def reply_json(self, code: int, data: dict) -> None:
        raw = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.common_headers()
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def parse_route(self) -> str:
        return unquote(urlsplit(self.path).path)

    def safe_static_path(self, route: str) -> Path | None:
        if not route.startswith("/") or "\x00" in route or "\\" in route:
            return None
        parts = route.lstrip("/").split("/")
        if any(part in ("", ".", "..") for part in parts if route != "/"):
            return None
        if route == "/":
            route = "/index.html"
        if route.startswith("/user-content/"):
            key = route[len("/user-content/"):]
            if key not in ALLOWED_UPLOADS.values():
                return None
            return self.server.user_data / key
        if parts[0].startswith(".") or any(part.startswith(".") for part in parts):
            return None
        return self.server.webroot.joinpath(*route.lstrip("/").split("/"))

    def do_HEAD(self) -> None:
        if not self.valid_host():
            self.reply_json(403, {"error": "Host nao autorizado"})
            return
        route = self.parse_route()
        # Preflight requests must not stream the full ZIP into the browser.
        if route == "/user-content/boxedwine.zip":
            ensure_local_wine(self.server.user_data)
        file = self.safe_static_path(route)
        if file is None or not file.is_file():
            self.send_response(404)
            self.common_headers()
            self.end_headers()
            return
        size = file.stat().st_size
        self.send_response(200)
        self.common_headers()
        self.send_header("Content-Type", MIME.get(file.suffix.lower(), "application/octet-stream"))
        self.send_header("Content-Length", str(size))
        self.end_headers()

    def log_browser_events(self) -> None:
        # Browser messages must come from this exact local origin. Unlike an
        # external telemetry endpoint, the file remains private on the PC.
        expected_origin = f"http://127.0.0.1:{self.server.server_port}"
        if self.headers.get("Origin") != expected_origin:
            self.reply_json(403, {"error": "Origem de diagnostico nao autorizada"})
            return
        if self.headers.get("Content-Type", "").split(";")[0] != "application/json":
            self.reply_json(415, {"error": "Esperado application/json"})
            return
        try:
            size = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            size = 0
        if size < 2 or size > MAX_DIAGNOSTIC_POST or self.headers.get("Transfer-Encoding"):
            self.reply_json(413, {"error": "Mensagem de log invalida ou grande demais"})
            return
        try:
            raw = self.rfile.read(size)
            if len(raw) != size:
                raise ValueError("Log incompleto")
            event_data = json.loads(raw)
            events = event_data.get("events") if isinstance(event_data, dict) else None
            if not isinstance(events, list) or not 1 <= len(events) <= 24:
                raise ValueError("Formato de eventos invalido")
            lines = []
            for item in events:
                if not isinstance(item, dict):
                    raise ValueError("Evento invalido")
                level = item.get("level", "info")
                if level not in ("info", "warn", "error", "net"):
                    raise ValueError("Nivel de log invalido")
                raw_message = item.get("message", "")
                if not isinstance(raw_message, str) or len(raw_message) > 1200:
                    raise ValueError("Mensagem de log invalida")
                message = raw_message.replace("\r", " ").replace("\n", " ")
                line = f"[browser:{level}] {message}"
                lines.append(line)
            with self.server.diagnostics_lock:
                destination = self.server.browser_log
                if destination.exists() and destination.stat().st_size > MAX_DIAGNOSTIC_LOG_BYTES:
                    destination.replace(destination.with_suffix(".previous.log"))
                with destination.open("a", encoding="utf-8") as stream:
                    for line in lines:
                        stream.write(line + "\n")
            for line in lines:
                if line.startswith(("[browser:error]", "[browser:warn]")) or any(
                    term in line for term in ("initial setup", "user-content/", ".wasm", "Emulator params:")
                ):
                    print(line[:1000], flush=True)
            self.reply_json(200, {"ok": True, "events": len(lines)})
        except (ValueError, UnicodeError, OSError) as exc:
            self.reply_json(400, {"error": str(exc)})

    def do_GET(self) -> None:
        if not self.valid_host():
            self.reply_json(403, {"error": "Host nao autorizado; use 127.0.0.1"})
            return
        route = self.parse_route()
        if route == "/favicon.ico":
            self.send_response(204)
            self.common_headers()
            self.end_headers()
            return
        if route == "/api/wine-inspect":
            error = ensure_local_wine(self.server.user_data)
            path = self.server.user_data / ALLOWED_UPLOADS["wine"]
            if not path.is_file():
                self.reply_json(404, {"error": error or "boxedwine.zip nao encontrado"})
                return
            try:
                with ZipFile(path) as archive:
                    names = archive.namelist()
                    wine = archive.getinfo("bin/wine") if "bin/wine" in names else None
                    header = archive.open(wine).read(20) if wine else b""
                    nested = [n for n in names if n.endswith("/bin/wine")][:10]
                    self.reply_json(200, {
                        "bytes": path.stat().st_size,
                        "entries": len(names),
                        "binWinePresent": wine is not None,
                        "binWineSize": wine.file_size if wine else 0,
                        "binWineMagic": header[:4].hex(),
                        "binWineElfI386": (
                            len(header) >= 20 and header[:4] == b"\x7fELF"
                            and header[4] == 1 and header[5] == 1
                            and header[18:20] == b"\x03\x00"
                        ),
                        "nestedWinePaths": nested,
                        "sampleEntries": names[:15],
                        "importError": error,
                    })
            except (OSError, BadZipFile, KeyError, ValueError) as exc:
                self.reply_json(422, {"error": str(exc)})
            return
        if route == "/api/status":
            wine_import_error = ensure_local_wine(self.server.user_data)
            archives = {}
            for kind, name in ALLOWED_UPLOADS.items():
                p = self.server.user_data / name
                info = {"ready": p.is_file(), "bytes": p.stat().st_size if p.is_file() else 0}
                if kind == "wine" and wine_import_error:
                    info["error"] = wine_import_error
                if kind == "wine" and p.is_file():
                    # Existing user uploads may predate the stricter validator.
                    # Check only the central directory; never extract the ZIP.
                    try:
                        validate_archive(p, "wine")
                    except (ValueError, OSError) as exc:
                        info["ready"] = False
                        info["error"] = str(exc)
                archives[kind] = info
            binaries = {name: (self.server.webroot / name).is_file() for name in
                        ("boxedwine.html", "boxedwine.js", "boxedwine.wasm")}
            self.reply_json(200, {"archives": archives, "runtime": binaries})
            return
        if route.startswith("/api/"):
            self.reply_json(404, {"error": "Rota desconhecida"})
            return
        if route == "/user-content/boxedwine.zip":
            ensure_local_wine(self.server.user_data)
        file = self.safe_static_path(route)
        if file is None or not file.is_file():
            self.send_error(404, "Arquivo nao encontrado")
            return
        try:
            size = file.stat().st_size
            self.send_response(200)
            self.common_headers()
            self.send_header("Content-Type", MIME.get(file.suffix.lower(), "application/octet-stream"))
            self.send_header("Content-Length", str(size))
            self.end_headers()
            with file.open("rb") as handle:
                shutil.copyfileobj(handle, self.wfile, length=READ_CHUNK)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_POST(self) -> None:
        if not self.valid_host():
            self.reply_json(403, {"error": "Host nao autorizado; use 127.0.0.1"})
            return
        route = self.parse_route()
        if route == "/api/browser-log":
            self.log_browser_events()
            return
        if not route.startswith("/api/upload/"):
            self.reply_json(404, {"error": "Rota desconhecida"})
            return
        kind = route.removeprefix("/api/upload/")
        if kind not in ALLOWED_UPLOADS:
            self.reply_json(404, {"error": "Tipo desconhecido"})
            return
        # A malicious webpage must not be able to upload to this localhost server.
        expected_origin = f"http://127.0.0.1:{self.server.server_port}"
        origin = self.headers.get("Origin", "")
        if origin not in ("", expected_origin):
            self.reply_json(403, {"error": "Origem nao autorizada"})
            return
        if self.headers.get("Content-Type", "").split(";")[0] != "application/zip":
            self.reply_json(415, {"error": "Envie um arquivo ZIP (application/zip)"})
            return
        try:
            size = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            size = 0
        if size <= 0 or size > MAX_UPLOAD or self.headers.get("Transfer-Encoding"):
            self.reply_json(413, {"error": "ZIP vazio, muito grande ou tamanho desconhecido"})
            return
        fd, tempname = tempfile.mkstemp(prefix="upload-", suffix=".zip", dir=self.server.user_data)
        tmp = Path(tempname)
        try:
            with os.fdopen(fd, "wb") as target:
                remaining = size
                while remaining:
                    chunk = self.rfile.read(min(READ_CHUNK, remaining))
                    if not chunk:
                        raise ValueError("Upload incompleto")
                    target.write(chunk)
                    remaining -= len(chunk)
            result = validate_archive(tmp, kind)
            destination = self.server.user_data / ALLOWED_UPLOADS[kind]
            os.replace(tmp, destination)
            self.reply_json(200, {"ok": True, **result})
        except (ValueError, OSError) as exc:
            self.reply_json(400, {"error": str(exc)})
        finally:
            tmp.unlink(missing_ok=True)

    def log_message(self, format: str, *args: object) -> None:
        print(f"[browser-port] {self.client_address[0]} {format % args}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument("--user-data", type=Path, default=None)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("Porta invalida")
    webroot = args.root.resolve()
    if not webroot.is_dir():
        parser.error(f"Diretorio nao encontrado: {webroot}")
    data = (args.user_data or (webroot / ".local-game-data")).resolve()
    # Bind only loopback; never expose copyrighted ZIPs on your home network.
    server = GameServer(("127.0.0.1", args.port), webroot, data)
    print(f"Prism3D browser port: http://127.0.0.1:{args.port}/")
    print(f"ZIPs locais (NAO publicar): {data}")
    print("O servidor usa somente localhost. Ctrl+C para encerrar.")
    print(f"Log do navegador (privado): {server.browser_log}")
    try:
        server.serve_forever(poll_interval=0.3)
    except KeyboardInterrupt:
        print("\nEncerrando.")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()

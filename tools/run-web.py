#!/usr/bin/env python3
import http.server
import argparse
import os
from pathlib import Path
import subprocess
import sys
import webbrowser
from urllib.parse import urlencode


ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "web"
URL = "http://127.0.0.1:8000/voxellancer-web.html"


def main():
    parser = argparse.ArgumentParser(description="Build and serve the browser game or effect lab.")
    parser.add_argument("--effect-lab", action="store_true")
    parser.add_argument("--group", default="pulselaser")
    parser.add_argument("--effect", default="muzzle")
    args = parser.parse_args()
    url = URL
    if args.effect_lab:
        url += "?" + urlencode({"scenario": "effect-lab", "group": args.group, "effect": args.effect})
    emsdk = Path(os.environ.get("EMSDK") or Path.home() / "emsdk").expanduser().resolve()
    vcpkg = Path(os.environ.get("VCPKG_ROOT") or Path.home() / "vcpkg").expanduser().resolve()
    emscripten = emsdk / "upstream" / "emscripten"
    chainload = emscripten / "cmake" / "Modules" / "Platform" / "Emscripten.cmake"
    toolchain = vcpkg / "scripts" / "buildsystems" / "vcpkg.cmake"
    em_config = Path(os.environ.get("EM_CONFIG") or emsdk / ".emscripten").expanduser()

    if not chainload.is_file() or not em_config.is_file():
        raise RuntimeError(
            f"No activated Emscripten SDK found at {emsdk}. "
            "Install and activate it, then set EMSDK if it is not in ~/emsdk."
        )
    if not toolchain.is_file() or not (vcpkg / ("vcpkg.exe" if os.name == "nt" else "vcpkg")).is_file():
        raise RuntimeError(
            f"No bootstrapped vcpkg found at {vcpkg}. "
            "Set VCPKG_ROOT if it is not in ~/vcpkg."
        )

    env = os.environ.copy()
    env["EMSDK"] = str(emsdk)
    env["EM_CONFIG"] = str(em_config.resolve())
    env["VCPKG_ROOT"] = str(vcpkg)
    env["PATH"] = str(emscripten) + os.pathsep + env.get("PATH", "")
    configure = [
        "cmake", "-S", str(ROOT / "web"), "-B", str(BUILD),
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
        f"-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE={chainload}",
        "-DVCPKG_TARGET_TRIPLET=wasm32-emscripten",
    ]
    if os.name == "nt":
        configure += ["-G", "Ninja"]
    subprocess.run(configure, check=True, cwd=ROOT, env=env)
    subprocess.run(["cmake", "--build", str(BUILD), "--parallel", "4"], check=True, cwd=ROOT, env=env)

    handler = lambda *args, **kwargs: http.server.SimpleHTTPRequestHandler(
        *args, directory=str(BUILD), **kwargs
    )
    with http.server.ThreadingHTTPServer(("127.0.0.1", 8000), handler) as server:
        print(f"Serving {url} (stop the task to quit).", flush=True)
        if not webbrowser.open(url):
            print("Could not open a browser automatically; open the URL above.", file=sys.stderr)
        server.serve_forever()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nBrowser game server stopped.")
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Browser game task failed: {error}", file=sys.stderr)
        sys.exit(1)

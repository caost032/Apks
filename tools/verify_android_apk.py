#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

EXPECTED_ODG = {
    "odg_host_abi_version",
    "odg_service_create",
    "odg_service_destroy",
    "odg_service_start",
    "odg_service_stop",
    "odg_service_submit_input",
    "odg_service_copy_ui_snapshot",
    "odg_service_set_render_extent",
}
EXPECTED_JNI = {
    "Java_com_odpar_territorial_1domain_greenfield_NativeRenderPlugin_nativeAttachSurface",
    "Java_com_odpar_territorial_1domain_greenfield_NativeRenderPlugin_nativeDetachSurface",
    "Java_com_odpar_territorial_1domain_greenfield_NativeRenderPlugin_nativeRetainService",
    "Java_com_odpar_territorial_1domain_greenfield_NativeRenderPlugin_nativeReleaseService",
}
REQUIRED_LIBS = {"libodpar_greenfield.so", "libflutter.so", "libapp.so"}


def run(cmd: list[str]) -> str:
    proc = subprocess.run(cmd, check=True, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return proc.stdout


def load_alignments(path: Path) -> list[int]:
    output = run(["readelf", "-lW", str(path)])
    aligns: list[int] = []
    for line in output.splitlines():
        stripped = line.strip()
        if not stripped.startswith("LOAD "):
            continue
        token = stripped.split()[-1]
        try:
            aligns.append(int(token, 0))
        except ValueError as exc:
            raise RuntimeError(f"cannot parse LOAD alignment from: {line}") from exc
    if not aligns:
        raise RuntimeError(f"no PT_LOAD program headers in {path}")
    return aligns


def dynamic_symbols(path: Path) -> set[str]:
    output = run(["nm", "-D", "--defined-only", str(path)])
    symbols: set[str] = set()
    for line in output.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            symbols.add(parts[-1])
    return symbols


def hardening(path: Path) -> dict[str, bool]:
    program = run(["readelf", "-lW", str(path)])
    dynamic = run(["readelf", "-dW", str(path)])
    relro = "GNU_RELRO" in program
    stack_lines = [line for line in program.splitlines() if "GNU_STACK" in line]
    non_exec_stack = bool(stack_lines)
    for line in stack_lines:
        parts = line.split()
        # readelf -lW prints flags immediately before the final alignment field.
        if len(parts) >= 2 and "E" in parts[-2]:
            non_exec_stack = False
    bind_now = "BIND_NOW" in dynamic or "NOW" in dynamic
    return {
        "relro": relro,
        "bind_now": bind_now,
        "non_exec_stack": non_exec_stack,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--apk", required=True, type=Path)
    parser.add_argument("--exact-abis", required=True, help="comma-separated ABI set")
    parser.add_argument("--build-tools", required=True, type=Path)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    apk = args.apk.resolve()
    exact = {item for item in args.exact_abis.split(",") if item}
    zipalign = args.build_tools / "zipalign"
    apksigner = args.build_tools / "apksigner"
    if not apk.is_file():
        raise SystemExit(f"APK missing: {apk}")
    for tool in (zipalign, apksigner):
        if not tool.is_file():
            raise SystemExit(f"Android build tool missing: {tool}")
    if shutil.which("readelf") is None or shutil.which("nm") is None:
        raise SystemExit("readelf and nm are required for native artifact verification")

    run([str(zipalign), "-c", "-P", "16", "-v", "4", str(apk)])
    run([str(apksigner), "verify", "--verbose", "--print-certs", str(apk)])

    report: dict[str, object] = {"apk": str(apk), "abis": {}, "zipalign_16k": True, "signed": True}
    with zipfile.ZipFile(apk) as archive, tempfile.TemporaryDirectory(prefix="odpar-apk-") as temp_dir:
        names = set(archive.namelist())
        actual = {
            name.split("/")[1]
            for name in names
            if name.startswith("lib/") and name.count("/") >= 2 and name.endswith("/libodpar_greenfield.so")
        }
        if actual != exact:
            raise SystemExit(f"ABI mismatch: expected {sorted(exact)}, found {sorted(actual)}")

        temp = Path(temp_dir)
        abi_reports: dict[str, object] = {}
        for abi in sorted(exact):
            abi_names = {Path(name).name for name in names if name.startswith(f"lib/{abi}/") and name.endswith(".so")}
            missing_libs = REQUIRED_LIBS - abi_names
            if missing_libs:
                raise SystemExit(f"{abi}: missing native libraries: {sorted(missing_libs)}")
            member = f"lib/{abi}/libodpar_greenfield.so"
            info = archive.getinfo(member)
            if info.compress_type != zipfile.ZIP_STORED:
                raise SystemExit(f"{abi}: libodpar_greenfield.so is compressed; mmap/page alignment contract cannot be trusted")
            out = temp / f"{abi}-libodpar_greenfield.so"
            out.write_bytes(archive.read(member))
            aligns = load_alignments(out)
            if min(aligns) < 0x4000:
                raise SystemExit(f"{abi}: PT_LOAD alignment below 16 KiB: {aligns}")
            hardening_status = hardening(out)
            failed_hardening = [name for name, passed in hardening_status.items() if not passed]
            if failed_hardening:
                raise SystemExit(
                    f"{abi}: native hardening failed: {', '.join(sorted(failed_hardening))}"
                )
            symbols = dynamic_symbols(out)
            odg = {symbol for symbol in symbols if symbol.startswith("odg_")}
            if odg != EXPECTED_ODG:
                raise SystemExit(
                    f"{abi}: ODG export mismatch; missing={sorted(EXPECTED_ODG-odg)} extra={sorted(odg-EXPECTED_ODG)}"
                )
            jni = {symbol for symbol in symbols if symbol.startswith("Java_com_odpar_")}
            if jni != EXPECTED_JNI:
                raise SystemExit(
                    f"{abi}: JNI export mismatch; missing={sorted(EXPECTED_JNI-jni)} extra={sorted(jni-EXPECTED_JNI)}"
                )
            abi_reports[abi] = {
                "load_alignments": aligns,
                "required_libs": sorted(REQUIRED_LIBS),
                "odg_exports": sorted(odg),
                "jni_exports": sorted(jni),
                "stored_uncompressed": True,
                "hardening": hardening_status,
            }
        report["abis"] = abi_reports

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(f"APK artifact verification: OK ({apk.name}; ABIs={','.join(sorted(exact))}; 16KiB; signature; hardening; symbols)")


if __name__ == "__main__":
    main()

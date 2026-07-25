#!/usr/bin/env python3

from __future__ import annotations

import argparse
import datetime
import hashlib
import importlib.util
import json
import os
import shlex
import shutil
import stat
import subprocess
import sys
import zipfile
from pathlib import Path


class PipelineError(RuntimeError):
    pass


SOURCE_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ARTIFACT_ROOT = SOURCE_ROOT / "bin" / "outpostia-artifacts"
ARCHITECTURE = "x86_64"
WINDOWS_PLATFORM = "windows"
LINUX_PLATFORM = "linuxbsd"


def log(message: str) -> None:
    print(f"[outpostia-build] {message}", flush=True)


def resolve_path(value: str | Path) -> Path:
    path = Path(value).expanduser()
    if not path.is_absolute():
        path = SOURCE_ROOT / path
    return path.resolve()


def run_capture(command: list[str], context: str) -> str:
    try:
        result = subprocess.run(
            command,
            cwd=SOURCE_ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
    except FileNotFoundError as exception:
        raise PipelineError(f"{context}: command not found: {command[0]}") from exception
    except subprocess.CalledProcessError as exception:
        stdout = exception.stdout.strip()
        stderr = exception.stderr.strip()
        details = "\n".join(part for part in [stdout, stderr] if part)
        if details:
            details = f"\n{details}"
        raise PipelineError(
            f"{context}: command failed with exit code {exception.returncode}: "
            f"{shlex.join(command)}{details}"
        ) from exception
    return result.stdout.strip()


def run_command(command: list[str], context: str, dry_run: bool) -> None:
    log(f"{'[dry-run] ' if dry_run else ''}{context}: {shlex.join(command)}")
    if dry_run:
        return
    try:
        subprocess.run(command, cwd=SOURCE_ROOT, check=True)
    except FileNotFoundError as exception:
        raise PipelineError(f"{context}: command not found: {command[0]}") from exception
    except subprocess.CalledProcessError as exception:
        raise PipelineError(
            f"{context}: command failed with exit code {exception.returncode}: "
            f"{shlex.join(command)}"
        ) from exception


def load_engine_version() -> str:
    version_path = SOURCE_ROOT / "version.py"
    spec = importlib.util.spec_from_file_location("outpostia_engine_version", version_path)
    if spec is None or spec.loader is None:
        raise PipelineError(f"Unable to load engine version from {version_path}")
    version = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(version)

    release = f"{version.major}.{version.minor}.{version.patch}"
    fork = str(getattr(version, "fork", "")).strip()
    if fork:
        release += f"-{fork}"
    return release


def get_source_identity() -> tuple[str, int]:
    sha = run_capture(["git", "rev-parse", "HEAD"], "Read engine SHA")
    timestamp = run_capture(
        ["git", "show", "-s", "--format=%ct", "HEAD"],
        "Read engine commit timestamp",
    )
    if len(sha) != 40:
        raise PipelineError(f"Unexpected engine SHA: {sha}")
    return sha, int(timestamp)


def ensure_clean_checkout(dry_run: bool) -> None:
    if dry_run:
        log("[dry-run] Would require a clean tracked engine checkout")
        return
    status = run_capture(
        ["git", "status", "--porcelain", "--untracked-files=no"],
        "Check engine checkout",
    )
    if status:
        raise PipelineError(
            "Tracked engine changes are present. Commit or otherwise resolve them "
            "before creating distribution artifacts."
        )


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_file(path: Path, context: str) -> None:
    if not path.is_file():
        raise PipelineError(f"{context}: required file is missing: {path}")


def require_directory(path: Path, context: str) -> None:
    if not path.is_dir():
        raise PipelineError(f"{context}: required directory is missing: {path}")


def binary_contains_sha(path: Path, engine_sha: str) -> None:
    require_file(path, "Validate binary provenance")
    needle = engine_sha.encode("ascii")
    overlap = b""
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            payload = overlap + chunk
            if needle in payload:
                return
            overlap = payload[-(len(needle) - 1) :]
    raise PipelineError(
        f"Built binary does not contain the expected engine SHA {engine_sha}: {path}"
    )


def validate_editor(editor: Path, version: str, engine_sha: str) -> str:
    reported = run_capture([str(editor), "--version"], "Validate editor version")
    if version not in reported:
        raise PipelineError(
            f"Editor reported '{reported}', expected version '{version}'."
        )
    if engine_sha[:9] not in reported:
        raise PipelineError(
            f"Editor reported '{reported}', expected SHA prefix '{engine_sha[:9]}'."
        )
    binary_contains_sha(editor, engine_sha)
    log(f"Validated editor identity: {reported}; full SHA {engine_sha}")
    return reported


def package_names(version: str) -> set[str]:
    return {
        f"Godot.NET.Sdk.{version}.nupkg",
        f"Godot.SourceGenerators.{version}.nupkg",
        f"GodotSharp.{version}.nupkg",
        f"GodotSharp.{version}.snupkg",
        f"GodotSharpEditor.{version}.nupkg",
        f"GodotSharpEditor.{version}.snupkg",
    }


def validate_package(path: Path, engine_sha: str) -> None:
    try:
        with zipfile.ZipFile(path, "r") as archive:
            nuspecs = sorted(
                name for name in archive.namelist() if name.lower().endswith(".nuspec")
            )
            if len(nuspecs) != 1:
                raise PipelineError(
                    f"Expected one .nuspec in {path}, found {len(nuspecs)}."
                )
            nuspec = archive.read(nuspecs[0])
    except zipfile.BadZipFile as exception:
        raise PipelineError(f"Invalid NuGet package: {path}") from exception
    if engine_sha.encode("ascii") not in nuspec:
        raise PipelineError(
            f"NuGet package provenance does not match {engine_sha}: {path}"
        )


def collect_godotsharp_files(
    godotsharp_dir: Path, version: str, engine_sha: str
) -> tuple[list[tuple[str, Path]], list[str]]:
    require_directory(godotsharp_dir, "Package GodotSharp distribution")
    expected_packages = package_names(version)
    package_dir = godotsharp_dir / "Tools" / "nupkgs"
    require_directory(package_dir, "Package current-version NuGet artifacts")

    for name in sorted(expected_packages):
        package_path = package_dir / name
        require_file(package_path, "Package current-version NuGet artifacts")
        validate_package(package_path, engine_sha)

    files: list[tuple[str, Path]] = []
    excluded_packages: list[str] = []
    for path in sorted(
        (path for path in godotsharp_dir.rglob("*") if path.is_file()),
        key=lambda item: item.relative_to(godotsharp_dir).as_posix(),
    ):
        relative = path.relative_to(godotsharp_dir)
        if relative.parent.as_posix() == "Tools/nupkgs":
            if path.name not in expected_packages:
                excluded_packages.append(path.name)
                continue
        files.append((f"GodotSharp/{relative.as_posix()}", path))

    return files, excluded_packages


def zip_timestamp(commit_timestamp: int) -> tuple[int, int, int, int, int, int]:
    value = datetime.datetime.fromtimestamp(commit_timestamp, datetime.timezone.utc)
    if value.year < 1980:
        value = value.replace(year=1980, month=1, day=1, hour=0, minute=0, second=0)
    return (value.year, value.month, value.day, value.hour, value.minute, value.second)


def write_deterministic_zip(
    output_path: Path,
    files: list[tuple[str, Path]],
    commit_timestamp: int,
) -> list[dict[str, str | int]]:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path = output_path.with_name(f"{output_path.name}.tmp")
    if temporary_path.exists():
        temporary_path.unlink()

    manifest_files: list[dict[str, str | int]] = []
    try:
        with zipfile.ZipFile(
            temporary_path,
            "w",
            compression=zipfile.ZIP_DEFLATED,
            compresslevel=9,
            strict_timestamps=False,
        ) as archive:
            for archive_name, source_path in sorted(files, key=lambda item: item[0]):
                normalized_name = archive_name.replace("\\", "/")
                file_stat = source_path.stat()
                info = zipfile.ZipInfo(
                    normalized_name,
                    date_time=zip_timestamp(commit_timestamp),
                )
                info.compress_type = zipfile.ZIP_DEFLATED
                info.create_system = 3
                executable = source_path.suffix.lower() in {"", ".exe"}
                permissions = 0o755 if executable else 0o644
                info.external_attr = (stat.S_IFREG | permissions) << 16
                with source_path.open("rb") as source, archive.open(info, "w") as target:
                    shutil.copyfileobj(source, target, length=1024 * 1024)
                manifest_files.append(
                    {
                        "path": normalized_name,
                        "size": file_stat.st_size,
                        "sha256": sha256_file(source_path),
                    }
                )
        temporary_path.replace(output_path)
    except Exception:
        if temporary_path.exists():
            temporary_path.unlink()
        raise
    return manifest_files


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def write_sha256_sidecar(path: Path, digest: str) -> Path:
    sidecar = path.with_name(f"{path.name}.sha256")
    sidecar.write_text(f"{digest}  {path.name}\n", encoding="utf-8", newline="\n")
    return sidecar


def source_timestamp_iso(commit_timestamp: int) -> str:
    return datetime.datetime.fromtimestamp(
        commit_timestamp, datetime.timezone.utc
    ).strftime("%Y-%m-%dT%H:%M:%SZ")


def scons_command(args: argparse.Namespace, platform: str, target: str) -> list[str]:
    command = [args.scons]
    if args.jobs:
        command.append(f"-j{args.jobs}")
    command.extend(
        [
            f"platform={platform}",
            f"target={target}",
            f"arch={ARCHITECTURE}",
            "module_mono_enabled=yes",
        ]
    )
    return command


def assembly_command(platform: str) -> list[str]:
    return [
        sys.executable,
        str(SOURCE_ROOT / "modules" / "mono" / "build_scripts" / "build_assemblies.py"),
        "--godot-output-dir",
        str(SOURCE_ROOT / "bin"),
        f"--godot-platform={platform}",
        "--no-deprecated",
        "--werror",
    ]


def prepare_platform_paths(
    args: argparse.Namespace, version: str, engine_sha: str, platform_key: str
) -> tuple[Path, Path, Path, Path]:
    artifact_root = resolve_path(args.artifact_root)
    platform_dir = artifact_root / version / engine_sha / platform_key
    archive_name = (
        f"Godot_v{version}-win64_editor.zip"
        if platform_key == "windows"
        else f"Godot_v{version}-linux64_editor.zip"
    )
    archive_path = platform_dir / archive_name
    manifest_path = platform_dir / "manifest.json"
    templates_dir = platform_dir / "templates"

    existing = [
        path
        for path in [
            archive_path,
            archive_path.with_name(f"{archive_path.name}.sha256"),
            manifest_path,
            templates_dir,
        ]
        if path.exists()
    ]
    if existing and not args.force and not args.dry_run:
        rendered = "\n".join(f"  {path}" for path in existing)
        raise PipelineError(
            "Distribution outputs already exist. Use --force to replace only these "
            f"versioned outputs:\n{rendered}"
        )

    if args.dry_run:
        log(f"[dry-run] Editor archive: {archive_path}")
        log(f"[dry-run] Platform manifest: {manifest_path}")
        log(f"[dry-run] Template staging: {templates_dir}")
    return platform_dir, archive_path, manifest_path, templates_dir


def replace_platform_outputs(
    archive_path: Path, manifest_path: Path, templates_dir: Path, force: bool
) -> None:
    if not force:
        return
    for path in [
        archive_path,
        archive_path.with_name(f"{archive_path.name}.sha256"),
        manifest_path,
    ]:
        if path.exists():
            path.unlink()
    if templates_dir.exists():
        shutil.rmtree(templates_dir)


def platform_build(args: argparse.Namespace, platform_key: str) -> None:
    is_windows = platform_key == "windows"
    scons_platform = WINDOWS_PLATFORM if is_windows else LINUX_PLATFORM
    if not args.dry_run:
        if is_windows and os.name != "nt":
            raise PipelineError("The Windows pipeline must run on Windows.")
        if not is_windows and not sys.platform.startswith("linux"):
            raise PipelineError("The Linux pipeline must run on Linux.")

    version = load_engine_version()
    engine_sha, commit_timestamp = get_source_identity()
    ensure_clean_checkout(args.dry_run)
    _, archive_path, manifest_path, templates_dir = prepare_platform_paths(
        args, version, engine_sha, platform_key
    )

    bin_dir = SOURCE_ROOT / "bin"
    if is_windows:
        editor_name = "godot.windows.editor.x86_64.mono.exe"
        console_editor_name = "godot.windows.editor.x86_64.mono.console.exe"
        template_names = [
            "godot.windows.template_release.x86_64.mono.exe",
            "godot.windows.template_release.x86_64.mono.console.exe",
        ]
    else:
        editor_name = "godot.linuxbsd.editor.x86_64.mono"
        console_editor_name = ""
        template_names = ["godot.linuxbsd.template_release.x86_64.mono"]

    editor_path = bin_dir / editor_name
    glue_editor_path = bin_dir / (console_editor_name or editor_name)
    commands = [
        scons_command(args, scons_platform, "editor"),
        [
            str(glue_editor_path),
            "--headless",
            "--generate-mono-glue",
            str(SOURCE_ROOT / "modules" / "mono" / "glue"),
        ],
        assembly_command(scons_platform),
        scons_command(args, scons_platform, "template_release"),
    ]
    contexts = [
        f"Build {platform_key} Mono editor",
        "Generate Mono glue",
        "Build Mono assemblies and current-version packages",
        f"Build {platform_key} Mono release template",
    ]
    for command, context in zip(commands, contexts):
        run_command(command, context, args.dry_run)

    if args.dry_run:
        log(
            f"[dry-run] Would validate editor version {version}, SHA {engine_sha}, "
            "current packages, archive contents, and release templates"
        )
        return

    reported_version = validate_editor(editor_path, version, engine_sha)
    editor_files: list[tuple[str, Path]] = [(editor_name, editor_path)]
    if is_windows:
        console_editor = bin_dir / console_editor_name
        d3d12_runtime = bin_dir / "D3D12Core.dll"
        d3d12_layers = bin_dir / "d3d12SDKLayers.dll"
        for path in [console_editor, d3d12_runtime, d3d12_layers]:
            require_file(path, "Package Windows editor")
        editor_files.extend(
            [
                (console_editor_name, console_editor),
                (d3d12_runtime.name, d3d12_runtime),
                (d3d12_layers.name, d3d12_layers),
            ]
        )

    godotsharp_files, excluded_packages = collect_godotsharp_files(
        bin_dir / "GodotSharp", version, engine_sha
    )
    editor_files.extend(godotsharp_files)

    template_sources = [bin_dir / name for name in template_names]
    for template_path in template_sources:
        require_file(template_path, "Stage release template")
        if not template_path.name.endswith(".console.exe"):
            binary_contains_sha(template_path, engine_sha)

    replace_platform_outputs(
        archive_path, manifest_path, templates_dir, args.force
    )
    templates_dir.mkdir(parents=True, exist_ok=True)
    template_manifest: list[dict[str, str | int]] = []
    for template_path in template_sources:
        staged_path = templates_dir / template_path.name
        shutil.copy2(template_path, staged_path)
        template_manifest.append(
            {
                "path": f"templates/{staged_path.name}",
                "size": staged_path.stat().st_size,
                "sha256": sha256_file(staged_path),
            }
        )

    archive_files = write_deterministic_zip(
        archive_path, editor_files, commit_timestamp
    )
    archive_sha = sha256_file(archive_path)
    sidecar_path = write_sha256_sidecar(archive_path, archive_sha)
    manifest = {
        "schema": 1,
        "version": version,
        "engine_sha": engine_sha,
        "source_timestamp": source_timestamp_iso(commit_timestamp),
        "platform": platform_key,
        "architecture": ARCHITECTURE,
        "editor_reported_version": reported_version,
        "build_targets": [
            {"context": context, "command": command}
            for context, command in zip(contexts, commands)
        ],
        "editor_archive": {
            "path": archive_path.name,
            "size": archive_path.stat().st_size,
            "sha256": archive_sha,
            "sha256_sidecar": sidecar_path.name,
            "files": archive_files,
        },
        "templates": template_manifest,
        "excluded_stale_package_files": sorted(excluded_packages),
    }
    write_json(manifest_path, manifest)
    log(f"Editor archive ready: {archive_path}")
    log(f"Archive SHA-256: {archive_sha}")
    log(f"Release template staging ready: {templates_dir}")
    log(f"Manifest ready: {manifest_path}")


def read_platform_manifest(
    platform_dir: Path,
    platform_key: str,
    version: str,
    engine_sha: str,
    expected_templates: list[str],
) -> tuple[dict, list[tuple[str, Path]]]:
    manifest_path = platform_dir / "manifest.json"
    require_file(manifest_path, f"Validate {platform_key} artifacts")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exception:
        raise PipelineError(f"Invalid platform manifest: {manifest_path}") from exception

    expected_identity = {
        "version": version,
        "engine_sha": engine_sha,
        "platform": platform_key,
        "architecture": ARCHITECTURE,
    }
    for key, expected in expected_identity.items():
        if manifest.get(key) != expected:
            raise PipelineError(
                f"{manifest_path}: {key} is {manifest.get(key)!r}, expected {expected!r}."
            )

    records = {
        Path(record["path"]).name: record
        for record in manifest.get("templates", [])
    }
    if set(records) != set(expected_templates):
        raise PipelineError(
            f"{manifest_path}: template set {sorted(records)} does not match "
            f"{sorted(expected_templates)}."
        )

    files: list[tuple[str, Path]] = []
    for name in expected_templates:
        record = records[name]
        template_path = platform_dir / record["path"]
        require_file(template_path, f"Validate {platform_key} template")
        actual_sha = sha256_file(template_path)
        if actual_sha != record["sha256"]:
            raise PipelineError(
                f"{template_path}: SHA-256 is {actual_sha}, expected {record['sha256']}."
            )
        if not template_path.name.endswith(".console.exe"):
            binary_contains_sha(template_path, engine_sha)
        files.append((name, template_path))
    return manifest, files


def combine_templates(args: argparse.Namespace) -> None:
    version = load_engine_version()
    engine_sha, commit_timestamp = get_source_identity()
    ensure_clean_checkout(args.dry_run)
    artifact_root = resolve_path(args.artifact_root)
    version_root = artifact_root / version / engine_sha
    windows_dir = resolve_path(args.windows_artifacts) if args.windows_artifacts else version_root / "windows"
    linux_dir = resolve_path(args.linux_artifacts) if args.linux_artifacts else version_root / "linux"
    output_dir = resolve_path(args.output_dir) if args.output_dir else version_root
    archive_path = output_dir / f"Godot_v{version}-templates.zip"
    manifest_path = output_dir / f"Godot_v{version}-templates.manifest.json"
    sidecar_path = archive_path.with_name(f"{archive_path.name}.sha256")

    if args.dry_run:
        log(f"[dry-run] Would validate Windows artifacts: {windows_dir}")
        log(f"[dry-run] Would validate Linux artifacts: {linux_dir}")
        log(f"[dry-run] Combined templates archive: {archive_path}")
        return

    existing = [path for path in [archive_path, manifest_path, sidecar_path] if path.exists()]
    if existing and not args.force:
        rendered = "\n".join(f"  {path}" for path in existing)
        raise PipelineError(
            "Combined template outputs already exist. Use --force to replace only "
            f"these outputs:\n{rendered}"
        )

    windows_templates = [
        "godot.windows.template_release.x86_64.mono.exe",
        "godot.windows.template_release.x86_64.mono.console.exe",
    ]
    linux_templates = ["godot.linuxbsd.template_release.x86_64.mono"]
    _, windows_files = read_platform_manifest(
        windows_dir, "windows", version, engine_sha, windows_templates
    )
    _, linux_files = read_platform_manifest(
        linux_dir, "linux", version, engine_sha, linux_templates
    )

    if args.force:
        for path in [archive_path, manifest_path, sidecar_path]:
            if path.exists():
                path.unlink()

    archive_files = write_deterministic_zip(
        archive_path, windows_files + linux_files, commit_timestamp
    )
    archive_sha = sha256_file(archive_path)
    write_sha256_sidecar(archive_path, archive_sha)
    write_json(
        manifest_path,
        {
            "schema": 1,
            "version": version,
            "engine_sha": engine_sha,
            "source_timestamp": source_timestamp_iso(commit_timestamp),
            "platform": "windows+linux",
            "architecture": ARCHITECTURE,
            "archive": {
                "path": archive_path.name,
                "size": archive_path.stat().st_size,
                "sha256": archive_sha,
                "sha256_sidecar": sidecar_path.name,
                "files": archive_files,
            },
        },
    )
    log(f"Combined templates archive ready: {archive_path}")
    log(f"Archive SHA-256: {archive_sha}")


def add_build_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--artifact-root",
        default=str(DEFAULT_ARTIFACT_ROOT),
        help=(
            "Root for versioned archives, manifests, and template staging "
            f"(default: {DEFAULT_ARTIFACT_ROOT})"
        ),
    )
    parser.add_argument(
        "--scons",
        default="scons",
        help="SCons executable or path (default: scons)",
    )
    parser.add_argument(
        "-j",
        "--jobs",
        type=int,
        help="Optional SCons parallel job count",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Replace matching versioned distribution outputs; never cleans build caches",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print commands and output locations without building or writing artifacts",
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Build and package canonical Outpostia Godot distributions."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    windows = subparsers.add_parser(
        "windows",
        help="Build/package the Windows x86_64 Mono editor and release template",
    )
    add_build_arguments(windows)
    windows.set_defaults(func=lambda args: platform_build(args, "windows"))

    linux = subparsers.add_parser(
        "linux",
        help="Build/package the Linux x86_64 Mono editor and release template",
    )
    add_build_arguments(linux)
    linux.set_defaults(func=lambda args: platform_build(args, "linux"))

    templates = subparsers.add_parser(
        "templates",
        help="Validate and combine matching Windows/Linux release templates",
    )
    templates.add_argument(
        "--artifact-root",
        default=str(DEFAULT_ARTIFACT_ROOT),
        help=f"Root containing versioned platform artifacts (default: {DEFAULT_ARTIFACT_ROOT})",
    )
    templates.add_argument(
        "--windows-artifacts",
        help="Override the Windows platform artifact directory",
    )
    templates.add_argument(
        "--linux-artifacts",
        help="Override the Linux platform artifact directory",
    )
    templates.add_argument(
        "--output-dir",
        help="Override the combined archive output directory",
    )
    templates.add_argument(
        "--force",
        action="store_true",
        help="Replace matching combined archive outputs",
    )
    templates.add_argument(
        "--dry-run",
        action="store_true",
        help="Print validation inputs and output locations without writing artifacts",
    )
    templates.set_defaults(func=combine_templates)
    return parser


def main() -> int:
    if SOURCE_ROOT.joinpath("SConstruct").is_file() is False:
        raise PipelineError(f"Godot source root could not be resolved: {SOURCE_ROOT}")
    args = build_parser().parse_args()
    if getattr(args, "jobs", None) is not None and args.jobs < 1:
        raise PipelineError("--jobs must be a positive integer.")
    args.func(args)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except PipelineError as exception:
        print(f"[outpostia-build] FAILED: {exception}", file=sys.stderr)
        raise SystemExit(1)

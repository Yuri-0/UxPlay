#!/usr/bin/env python3
"""Package unmodified FDH2/UxPlay and its MSYS2 UCRT64 GStreamer runtime.

Runs on the GitHub Actions Windows runner. Requires: pip install pefile.
"""
from __future__ import annotations

import argparse
from collections import deque
from pathlib import Path
import shutil
import subprocess
import zipfile

try:
    import pefile
except ImportError as exc:
    raise SystemExit('Install pefile using: python -m pip install pefile') from exc


def imports_of(binary: Path) -> set[str]:
    """Read native and delay-loaded DLL imports from Windows PE binaries."""
    pe = pefile.PE(str(binary), fast_load=True)
    try:
        pe.parse_data_directories(directories=[
            pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT'],
            pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT'],
        ])
        found = set()
        for attr in ('DIRECTORY_ENTRY_IMPORT', 'DIRECTORY_ENTRY_DELAY_IMPORT'):
            for item in getattr(pe, attr, []):
                found.add(item.dll.decode('utf-8', errors='replace').lower())
        return found
    finally:
        pe.close()


# Select GStreamer plugins by the elements actually needed by UxPlay.
# GStreamer modules are loaded dynamically, so the normal EXE PE import
# closure is not enough to choose which modules to ship.
REQUIRED_GST_ELEMENTS = (
    'appsrc', 'queue', 'typefind', 'decodebin',
    'h264parse', 'avdec_h264', 'avdec_aac', 'avdec_alac',
    'videoconvert', 'videoscale', 'autovideosink',
    'audioconvert', 'audioresample', 'volume', 'level', 'autoaudiosink',
    'jpegdec', 'imagefreeze', 'textoverlay', 'videoflip',
)
OPTIONAL_GST_ELEMENTS = (
    'd3d11videosink', 'd3d12videosink', 'd3dvideosink',
    'glimagesink', 'directdrawsink', 'directsoundsink',
    'wasapisink', 'wasapi2sink', 'waveformsink',
    'd3d11h264dec', 'd3d12h264dec', 'd3d11h265dec',
    'avdec_h265', 'h265parse',
    'playbin', 'playbin3', 'uridecodebin', 'uridecodebin3',
    'decodebin3', 'souphttpsrc', 'hlsdemux', 'hlsdemux2',
    'qtdemux', 'tsdemux', 'mpegtsparse', 'aacparse',
    'videorate', 'pngdec', 'rtph264pay', 'rtph265pay', 'rtpL16pay',
)


def selected_gst_plugins(inspect: Path, plugin_dir: Path,
                         bin_dir: Path) -> tuple[list[Path], list[str]]:
    """Map GStreamer element factories to the installed module DLLs."""
    import os
    import re
    env = os.environ.copy()
    env['PATH'] = str(bin_dir) + os.pathsep + env.get('PATH', '')
    env['GST_PLUGIN_PATH_1_0'] = str(plugin_dir)
    env['GST_PLUGIN_SYSTEM_PATH_1_0'] = str(plugin_dir)
    selected = {}
    missing_optional = []
    all_elements = dict.fromkeys((*REQUIRED_GST_ELEMENTS, *OPTIONAL_GST_ELEMENTS))
    for element in all_elements:
        result = subprocess.run([str(inspect), element], env=env,
                                capture_output=True, text=True,
                                encoding='utf-8', errors='replace',
                                timeout=35)
        match = re.search(r'(?im)^\s*Filename\s*:?\s+(.+?\.dll)\s*$', result.stdout)
        if result.returncode != 0 or not match:
            if element in REQUIRED_GST_ELEMENTS:
                raise RuntimeError(
                    f'Missing required GStreamer element {element}: '
                    f'{result.stderr[-350:]} {result.stdout[-350:]}'
                )
            missing_optional.append(element)
            continue
        filename = match.group(1).strip().strip('"')
        candidate = Path(filename)
        if not candidate.is_file():
            candidate = plugin_dir / Path(filename).name
        if not candidate.is_file():
            if element in REQUIRED_GST_ELEMENTS:
                raise FileNotFoundError(
                    f'Required GStreamer plugin DLL for {element}: {filename}')
            missing_optional.append(element)
            continue
        if candidate.parent.resolve() != plugin_dir.resolve():
            raise RuntimeError(
                f'Unexpected GStreamer plugin outside prefix: {candidate}')
        selected[candidate.name.lower()] = candidate
    if not selected:
        raise RuntimeError('GStreamer plugin selection produced no DLLs')
    return sorted(selected.values()), missing_optional


def copy_and_zip(original: Path, launcher: Path, source: Path,
                 prefix: Path, output: Path, beacon_dir: Path):
    bin_dir = prefix / 'bin'
    plugin_dir = prefix / 'lib' / 'gstreamer-1.0'
    gio_dir = prefix / 'lib' / 'gio' / 'modules'
    scanner_locations = [
        prefix / 'libexec' / 'gstreamer-1.0' / 'gst-plugin-scanner.exe',
        bin_dir / 'gst-plugin-scanner.exe',
        prefix / 'lib' / 'gstreamer-1.0' / 'gst-plugin-scanner.exe',
    ]
    scanner = next((p for p in scanner_locations if p.is_file()), None)
    for path in (original, launcher, source / 'LICENSE',
                 prefix, bin_dir, plugin_dir):
        if not path.exists():
            raise FileNotFoundError(f'Missing: {path}')
    if scanner is None:
        raise FileNotFoundError('Missing gst-plugin-scanner.exe in MSYS2 UCRT64')
    if not (beacon_dir / 'UxPlayBLEBeacon.exe').is_file():
        raise FileNotFoundError(f'Missing bundled Windows BLE beacon: {beacon_dir}')
    if not (beacon_dir / '_internal').is_dir():
        raise FileNotFoundError(f'Missing bundled BLE Python/WinRT dependencies: {beacon_dir / "_internal"}')

    if output.exists():
        shutil.rmtree(output)
    rt = output / 'runtime'
    rt.mkdir(parents=True)

    # The click-to-run wrapper remains separate from the unmodified upstream binary.
    shutil.copy2(launcher, output / 'UxPlay.exe')
    shutil.copy2(original, rt / 'uxplay.exe')
    shutil.copytree(beacon_dir, output / 'ble')
    shutil.copy2(scanner, rt / 'gst-plugin-scanner.exe')
    inspect = bin_dir / 'gst-inspect-1.0.exe'
    if not inspect.is_file():
        raise FileNotFoundError(f'Missing inspector: {inspect}')
    shutil.copy2(inspect, rt / 'gst-inspect-1.0.exe')

    plugin_files, missing_optional = selected_gst_plugins(inspect, plugin_dir, bin_dir)
    all_plugin_files = list(plugin_dir.glob('*.dll'))
    (rt / 'plugins').mkdir()
    for file in plugin_files:
        shutil.copy2(file, rt / 'plugins' / file.name)
    print(f'GStreamer: selected {len(plugin_files)} of {len(all_plugin_files)} '
          f'plugins; excluded {len(all_plugin_files) - len(plugin_files)}')
    if missing_optional:
        print('Optional GStreamer elements not installed: '
              + ', '.join(missing_optional))

    # Optional GIO TLS/proxy modules for network playback (e.g., HLS).
    gio_files = sorted(gio_dir.glob('*.dll')) if gio_dir.is_dir() else []
    if gio_files:
        (rt / 'gio').mkdir()
        for file in gio_files:
            shutil.copy2(file, rt / 'gio' / file.name)

    schema = prefix / 'share' / 'glib-2.0' / 'schemas' / 'gschemas.compiled'
    if schema.is_file():
        (rt / 'schemas').mkdir()
        shutil.copy2(schema, rt / 'schemas' / schema.name)

    for candidate in [
        prefix / 'ssl' / 'certs' / 'ca-bundle.crt',
        prefix / 'etc' / 'ssl' / 'cert.pem',
        prefix / 'etc' / 'pki' / 'tls' / 'cert.pem',
    ]:
        if candidate.is_file():
            shutil.copy2(candidate, rt / 'ca-bundle.crt')
            break

    # A GStreamer plugin may depend on DLLs that the main executable does not
    # reference. Recursively inspect every packaged PE, not just uxplay.exe.
    runtime_binaries = [
        rt / 'uxplay.exe',
        rt / 'gst-plugin-scanner.exe',
        rt / 'gst-inspect-1.0.exe',
        *sorted((rt / 'plugins').glob('*.dll')),
        *sorted((rt / 'gio').glob('*.dll')),
    ]
    available = {p.name.lower(): p for p in bin_dir.glob('*.dll')}
    visited: set[str] = set()
    pending = deque(runtime_binaries)
    copied = 0
    unresolved: set[str] = set()

    while pending:
        current = pending.popleft()
        for dll in imports_of(current):
            if dll in visited:
                continue
            visited.add(dll)
            dependency = available.get(dll)
            if dependency is None:
                # Windows supplies kernel32/user32/ucrtbase/API-set DLLs.
                if dll.startswith(('lib', 'zlib', 'gstreamer')):
                    unresolved.add(dll)
                continue
            destination = rt / dependency.name
            if not destination.exists():
                shutil.copy2(dependency, destination)
                pending.append(destination)
                copied += 1

    if unresolved:
        raise RuntimeError(
            'Missing non-system DLL imports: ' + ', '.join(sorted(unresolved))
        )

    print(f'Packaged {len(plugin_files)} GStreamer plugins, '
          f'{len(gio_files)} GIO modules and {copied} runtime DLLs')

    commit = subprocess.check_output(
        ['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True
    ).strip()
    (output / 'PACKAGED-GSTREAMER-PLUGINS.txt').write_text(
        'Included GStreamer plugins selected for UxPlay:\n'
        + '\n'.join(file.name for file in plugin_files) + '\n',
        encoding='utf-8',
    )
    (output / 'BUILD-INFO.txt').write_text(
        'Original upstream: https://github.com/FDH2/UxPlay\n'
        f'Exact upstream commit: {commit}\n'
        f'Source: https://github.com/FDH2/UxPlay/tree/{commit}\n'
        'Target: Windows x64 / MSYS2 UCRT64 / internal mDNS + optional BLE beacon\n'
        'runtime/uxplay.exe is compiled from unchanged upstream code.\n',
        encoding='utf-8',
    )
    shutil.copy2(source / 'LICENSE', output / 'LICENSE-UxPlay.txt')
    (output / 'README-PORTABLE.txt').write_text(
        'UxPlay Windows Portable (original FDH2/UxPlay)\n'
        '\n'
        '1. Extract the whole ZIP, including the runtime subfolder.\n'
        '2. Double-click UxPlay.exe to run the receiver (LAN/mDNS + BLE).\n'
        '   If a Bluetooth LE advertising adapter is available and enabled,\n'
        '   the separate BLE helper automatically starts alongside UxPlay.\n'
        '   The helper is optional: mDNS continues to work without Bluetooth.\n'
        '3. If Windows Firewall asks, allow runtime/uxplay.exe on PRIVATE networks.\n'
        '4. Put iPhone/iPad and Windows PC on the same local network.\n'
        '5. On iOS: Control Center > Screen Mirroring > select the PC.\n'
        '6. To stop the receiver, close the console window or press Ctrl+C.\n'
        '\n'
        'No BAT file, terminal, command arguments, or MSYS2 installation needed.\n'
        'Keep both runtime/ and ble/ directories alongside UxPlay.exe.\n'
        'BLE advertising status or errors are written to UxPlay-BLE.log.\n'
        'Bluetooth is for discovery only; video/audio streams over Wi-Fi/LAN.\n'
        'A repeater that blocks unicast IP access still prevents mirroring.\n'
        'See BUILD-INFO.txt to verify the upstream commit used.\n'
        '\n'
        'Troubleshooting: Check firewall, network set to Private, same LAN,\n'
        'and Wi-Fi router client/AP isolation if your device is missing.\n'
        '\n'
        'Source: https://github.com/FDH2/UxPlay (GPL-3.0)\n',
        encoding='utf-8',
    )

    archive = output.with_suffix('.zip')
    archive.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED,
                         compresslevel=6, allowZip64=True) as zf:
        for file in sorted(output.rglob('*')):
            if file.is_file():
                zf.write(file, file.relative_to(output.parent))
    print(f'Created {archive} ({archive.stat().st_size / 1024**2:.1f} MiB)')
    print(f'Compiled from FDH2/UxPlay commit {commit}')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--original', type=Path, required=True)
    parser.add_argument('--launcher', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--beacon-dir', type=Path, required=True)
    args = parser.parse_args()
    copy_and_zip(
        args.original.resolve(),
        args.launcher.resolve(),
        args.source.resolve(),
        args.prefix.resolve(),
        args.output.resolve(),
        args.beacon_dir.resolve(),
    )


if __name__ == '__main__':
    main()

#!/usr/bin/env bash
# sotc: builds dist/SotC-PC/ and dist/SotC-PC.zip: SotC.exe (the launcher, frozen with PyInstaller
# so players need no Python), bb-probe.exe with the MSYS2 CLANG64 DLLs it needs, the preparation
# scripts, sotc.py and SotC.bat. Players need their own game dump; sound needs their own console's
# libSceNgs2/libSceUlt (never packaged here). Run after `bash build.sh`.
# Freezing uses a Windows Python with PyInstaller and Pillow (WINPYTHON, default C:\PyBuild312).
# Install certifi there too: the updater adds its CA bundle to the Windows roots (issue #359);
# PyInstaller bundles it (and its cacert.pem) by itself when it is installed.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
export MSYS2_ROOT=${MSYS2_ROOT:-/c/msys64}
export PATH="$MSYS2_ROOT/clang64/bin:$MSYS2_ROOT/usr/bin:$PATH"
[[ -f out/bb-probe.exe && -f out/bb-probe-compat.exe && -f out/bb-gpu-capabilities.exe ]] ||
    { echo 'Build first: bash build.sh (with the x86-64-v2 build, BB_COMPAT=1)' >&2; exit 1; }
python=${WINPYTHON:-/c/PyBuild312/python.exe}
"$python" -m PyInstaller --version >/dev/null || { echo "Need PyInstaller in $python (WINPYTHON)" >&2; exit 1; }
# The scripts run inside SotC.exe (--script): the standard modules they import come along.
hidden=()
for module in argparse base64 collections hashlib json re shutil struct; do
    hidden+=(--hidden-import "$module")
done
# The updater's HTTPS checks (launcher/sotc_update.py ssl_context): Windows' own chain verification
# (truststore) and certifi's roots, each only when installed in this Python.
for module in truststore certifi; do
    if "$python" -c "import $module" 2>/dev/null; then
        hidden+=(--hidden-import "$module")
    else
        echo "updater: no $module in $python, not packaged" >&2
    fi
done
"$python" -m PyInstaller --noconfirm --clean --log-level WARN --windowed --name SotC \
    --icon "$(cygpath -w "$PWD/launcher/sotc.ico")" --distpath out/pyi-dist --workpath out/pyi-work \
    --specpath out/pyi-work --paths "$(cygpath -w "$PWD/scripts")" "${hidden[@]}" \
    "$(cygpath -w "$PWD/launcher/sotc_launcher_win.py")"

dest=out/stage/SotC-PC
rm -rf -- out/stage
mkdir -p "$dest/bin" "$dest/scripts" "$dest/launcher"
cp -r out/pyi-dist/SotC/. "$dest/"
cp launcher/sotc.ico launcher/sotc.png launcher/window_icon.png "$dest/launcher/"
# The launcher theme's fonts (Cinzel, Inter: SIL Open Font License, licences next to them).
cp -r launcher/assets "$dest/launcher/"
# Without debug information (out/ keeps the symbols for crash reports).
for exe in bb-probe.exe bb-probe-compat.exe bb-gpu-capabilities.exe; do
    llvm-strip --strip-debug -o "$dest/bin/$exe" "out/$exe"
done
# Every DLL the loader takes from the CLANG64 tree (SDL3, FFmpeg, Vulkan loader, ...).
ldd "$dest/bin/bb-probe.exe" "$dest/bin/bb-probe-compat.exe" "$dest/bin/bb-gpu-capabilities.exe" | awk '/\/clang64\/bin\// {print $3}' | sort -u | while read -r dll; do
    cp -u "$dll" "$dest/bin/"
done
# DLSS Frame Generation (RTX 40/50): the official NVIDIA Streamline runtime next to bb-probe.exe,
# with NVIDIA's licenses (redistributable under the NVIDIA RTX SDKs license and Streamline's MIT
# license). Not in git: STREAMLINE_DIR (default ../SotC-deps/streamline-2.14.16) holds the files
# taken from github.com/NVIDIA-RTX/Streamline releases v2.14.16 (bin/x64). Without it the package
# still works; frame generation then offers FSR 3 only.
streamline=${STREAMLINE_DIR:-../SotC-deps/streamline-2.14.16}
if [[ -f "$streamline/sl.interposer.dll" ]]; then
    cp "$streamline"/*.dll "$dest/bin/"
    mkdir -p "$dest/bin/licenses-nvidia"
    cp "$streamline"/licenses-nvidia/* "$dest/bin/licenses-nvidia/"
    echo "DLSS-G: NVIDIA Streamline runtime included"
else
    echo "DLSS-G: no Streamline runtime in $streamline (FSR 3 only)" >&2
fi
# FSR 3 frame generation (vk_fsrfg.cpp): AMD's signed FidelityFX runtime next to bb-probe.exe, with
# its MIT license. Not in git: FFX_DIR (default ../SotC-deps/fidelityfx-vk-1.1.4) holds
# amd_fidelityfx_vk.dll and LICENSE.txt of the FidelityFX SDK 1.1.4 release
# (github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK, PrebuiltSignedDLL). Without it the
# package offers DLSS-G only.
ffx=${FFX_DIR:-../SotC-deps/fidelityfx-vk-1.1.4}
if [[ -f "$ffx/amd_fidelityfx_vk.dll" ]]; then
    cp "$ffx/amd_fidelityfx_vk.dll" "$dest/bin/"
    mkdir -p "$dest/bin/licenses-amd"
    cp "$ffx/LICENSE.txt" "$dest/bin/licenses-amd/FidelityFX-SDK-LICENSE.txt"
    echo "FSR 3: AMD FidelityFX runtime included"
else
    echo "FSR 3: no FidelityFX runtime in $ffx (DLSS-G only)" >&2
fi
# AMD FSR 4.1 / 3.1 upscaling (vk_fsr4.cpp): the official signed AMD FSR SDK 2.4.0 DLLs next to
# bb-probe.exe (redistributable in binary form under AMD's license, kept with them). Not in git:
# AMD_FSR_DIR (default ../SotC-deps/amd-fsr-sdk-2.4.0) holds Kits/FidelityFX/signedbin's
# amd_fidelityfx_loader_vk.dll and amd_fidelityfx_upscaler_vk.dll from
# github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK tag v2.4.0. Without it, FSR 1 instead.
amd_fsr=${AMD_FSR_DIR:-../SotC-deps/amd-fsr-sdk-2.4.0}
if [[ -f "$amd_fsr/amd_fidelityfx_loader_vk.dll" ]]; then
    cp "$amd_fsr"/amd_fidelityfx_loader_vk.dll "$amd_fsr"/amd_fidelityfx_upscaler_vk.dll "$dest/bin/"
    mkdir -p "$dest/bin/licenses-amd"
    cp "$amd_fsr"/licenses-amd/* "$dest/bin/licenses-amd/"
    echo "AMD FSR: FSR SDK 2.4.0 upscaler included"
else
    echo "AMD FSR: no FSR SDK in $amd_fsr (FSR 1 instead of FSR 4)" >&2
fi
cp scripts/prepare.py scripts/link_libc.py scripts/link_modules.py scripts/content_profile.py \
   scripts/sotc_cpu.py scripts/sotc_modes.py "$dest/scripts/"
cp sotc.py SotC.bat LICENSE CHANGELOG.md "$dest/"
# sotc: PkgTool (LibOrbisPkg, LGPL-3.0, with its license) unpacks the player's .pkg dumps
# (launcher/sotc_pkg.py); PKGTOOL_DIR, default tools/PkgTool (build.cmd downloads it there).
pkgtool=${PKGTOOL_DIR:-tools/PkgTool}
if [[ -f $pkgtool/PkgTool.exe ]]; then
    mkdir -p "$dest/tools/PkgTool"
    cp "$pkgtool/PkgTool.exe" "$pkgtool/LibOrbisPkg.dll" "$pkgtool/LICENSE.txt" "$dest/tools/PkgTool/"
else
    echo "warning: no PkgTool in $pkgtool, the launcher will not unpack .pkg dumps" >&2
fi
# sotc: the shader pack (SHADER_PACK_DIR) is seeded into the player's cache at the first boot.
if [[ -d ${SHADER_PACK_DIR:-} ]]; then
    cp -r "$SHADER_PACK_DIR" "$dest/shaderpack"
fi
cp README.md "$dest/README.md"
# A template, copied to sotc.ini at the first start (sotc.py, the launcher): a sotc.ini in the zip
# replaced the player's own when extracted over an older copy (#136).
cat > "$dest/sotc.ini.example" <<'EOF'
# Shadow of the Colossus PC - settings (see README.md)
# Folder of your game dump (the one with eboot.bin), CUSA08809 (EU) or CUSA08034 (US); by default
# ..\CUSA08809, else ..\CUSA08034:
# SOTC_GAME_DIR=..\CUSA08809
# Folder with libSceNgs2.sprx and libSceUlt.sprx from YOUR OWN console (for sound):
# SOTC_SYS_MODULES=C:\path\to\sys_modules
# Optional modes: af16 (sharp textures), fps60 (no 30 FPS cap, experimental):
# SOTC_MODES=af16
EOF
# Sony firmware must never be in the package.
if find "$dest" -iname '*.sprx' -o -iname '*.prx' | grep -q .; then
    echo 'refusing to package firmware modules' >&2; exit 1
fi

# The updater's file list (SHA-256 per file): an update downloads only the files that changed.
version=$(sed -n "s/^VERSION = '\([0-9.]*\)'\r*$/\1/p" launcher/sotc_launcher_win.py)
[[ -n $version ]] || { echo 'no VERSION in launcher/sotc_launcher_win.py' >&2; exit 1; }
python packaging/windows/update_manifest.py "$dest" "$version"

mkdir -p dist
rm -rf dist/SotC-PC.zip
python -c 'import shutil, sys; shutil.make_archive("dist/SotC-PC", "zip", "out/stage", "SotC-PC")'
# The playable copy keeps its settings and saves.
mkdir -p dist/SotC-PC
cp -r "$dest/bin" "$dest/scripts" "$dest/launcher" "$dest/_internal" dist/SotC-PC/
[[ -d $dest/tools ]] && cp -r "$dest/tools" dist/SotC-PC/
[[ -d $dest/shaderpack ]] && cp -r "$dest/shaderpack" dist/SotC-PC/
cp "$dest/SotC.exe" "$dest/sotc.py" "$dest/SotC.bat" "$dest/README.md" "$dest/LICENSE" "$dest/CHANGELOG.md" \
   "$dest/sotc.ini.example" dist/SotC-PC/
echo "dist/SotC-PC.zip ($(du -h dist/SotC-PC.zip | cut -f1)) and dist/SotC-PC/"

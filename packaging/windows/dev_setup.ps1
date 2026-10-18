# One command from a fresh clone to a running launcher on Windows 10/11 (run through build.cmd):
#   build.cmd             MSYS2 + packages, submodules, PkgTool, build, then the launcher
#   build.cmd -NoLaunch   stop after the build
#   build.cmd -Package    also dist\SotC-PC.zip and dist\SotC-PC-Setup-<version>.exe
# Everything it downloads stays in .deps\ (a private MSYS2, a Python for packaging, Inno Setup);
# nothing is installed system-wide. MSYS2_ROOT points it to an MSYS2 of your own instead.
param([switch]$NoLaunch, [switch]$Package)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$Repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$Deps = Join-Path $Repo '.deps'
New-Item -ItemType Directory -Force $Deps | Out-Null
Set-Location $Repo

function Step([string]$text) { Write-Host "==> $text" -ForegroundColor Cyan }

function Fetch([string]$url, [string]$file, [string]$sha256) {
    if (-not (Test-Path $file)) {
        Invoke-WebRequest -UseBasicParsing $url -OutFile "$file.part"
        Move-Item -Force "$file.part" $file
    }
    if ($sha256 -and (Get-FileHash -Algorithm SHA256 $file).Hash -ne $sha256) {
        Remove-Item -Force $file
        throw "checksum mismatch: $url"
    }
}

# --- MSYS2 CLANG64: compiler and libraries -------------------------------------------------------
$OwnMsys = [bool]$env:MSYS2_ROOT
$Msys = if ($OwnMsys) { $env:MSYS2_ROOT } else { Join-Path $Deps 'msys64' }
$Bash = Join-Path $Msys 'usr\bin\bash.exe'

function InMsys([string]$command) {
    $env:MSYSTEM = 'CLANG64'
    $env:CHERE_INVOKING = '1'
    $env:MSYS2_PATH_TYPE = 'minimal'
    & $Bash -lc $command
    if ($LASTEXITCODE) { throw "failed in MSYS2: $command" }
}

if (-not (Test-Path $Bash)) {
    if ($OwnMsys) { throw "MSYS2_ROOT=$Msys has no usr\bin\bash.exe" }
    Step 'Downloading MSYS2 into .deps (once, ~100 MB)'
    $sfx = Join-Path $Deps 'msys2-base.sfx.exe'
    Fetch 'https://github.com/msys2/msys2-installer/releases/download/nightly-x86_64/msys2-base-x86_64-latest.sfx.exe' $sfx ''
    Start-Process -Wait -NoNewWindow -FilePath $sfx -ArgumentList '-y', "-o$Deps"
    InMsys 'true'  # first start: keyring and home directory
}

# Bump the stamp when the package list changes.
$stamp = Join-Path $Msys '.sotc-packages-2'
if (-not (Test-Path $stamp)) {
    Step 'Installing the toolchain and libraries (once, ~1.5 GB)'
    if (-not $OwnMsys) {
        # A new MSYS2 updates its core first; the second pass updates the rest.
        InMsys 'pacman -Syu --noconfirm --noprogressbar || true'
        InMsys 'pacman -Syu --noconfirm --noprogressbar'
    }
    InMsys ('pacman -S --needed --noconfirm --noprogressbar git patch unzip ' +
            'mingw-w64-clang-x86_64-{toolchain,cmake,ninja,pkgconf,python,python-pillow,tk} ' +
            'mingw-w64-clang-x86_64-{vulkan-headers,vulkan-loader,vulkan-memory-allocator,glslang} ' +
            'mingw-w64-clang-x86_64-{sdl3,fmt,boost,robin-map,xxhash,ffmpeg,zydis} ' +
            'mingw-w64-clang-x86_64-{spirv-tools,spirv-headers,spirv-cross}')
    New-Item -ItemType File -Force $stamp | Out-Null
}
$MsysPosix = (& (Join-Path $Msys 'usr\bin\cygpath.exe') -u $Msys).Trim()

# --- PkgTool (LibOrbisPkg, LGPL-3.0): the launcher unpacks .pkg dumps with it --------------------
$PkgTool = Join-Path $Repo 'tools\PkgTool'
if (-not (Test-Path (Join-Path $PkgTool 'PkgTool.exe'))) {
    Step 'Downloading PkgTool (LibOrbisPkg 0.2)'
    $zip = Join-Path $Deps 'PkgTool-0.2.231.zip'
    Fetch 'https://github.com/maxton/LibOrbisPkg/releases/download/v0.2/PkgTool-0.2.231.zip' $zip `
        'C639E591E35C2431F68410D1771541D95F7308863638F9E3A6EEDF1818530097'
    Expand-Archive -Force $zip $PkgTool
}

# --- Build: out\bb-probe.exe and out\bb-gpu-capabilities.exe ------------------------------------
Step 'Building (the first build takes a few minutes)'
InMsys "export MSYS2_ROOT='$MsysPosix'; git submodule update --init --recursive && bash build.sh"

# --- Packages for players: zip and installer ----------------------------------------------------
if ($Package) {
    $version = (Select-String -Path 'launcher\sotc_launcher_win.py' -Pattern "^VERSION = '(.+)'").Matches[0].Groups[1].Value
    Step "Packaging $version"
    # PyInstaller freezes the launcher with a regular Windows Python (py launcher or python.exe).
    $venv = Join-Path $Deps 'pyvenv'
    $winPython = Join-Path $venv 'Scripts\python.exe'
    if (-not (Test-Path $winPython)) {
        $py = Get-Command py -ErrorAction SilentlyContinue
        if ($py) { & py -3 -m venv $venv } else { & python -m venv $venv }
        if ($LASTEXITCODE -or -not (Test-Path $winPython)) {
            throw 'Packaging needs Python for Windows (python.org or "winget install Python.Python.3.13")'
        }
        & $winPython -m pip install --quiet --disable-pip-version-check pyinstaller pillow
        if ($LASTEXITCODE) { throw 'pip install pyinstaller pillow failed' }
    }
    $winPythonPosix = (& (Join-Path $Msys 'usr\bin\cygpath.exe') -u $winPython).Trim()
    InMsys "export MSYS2_ROOT='$MsysPosix' WINPYTHON='$winPythonPosix' PKGTOOL_DIR=tools/PkgTool; bash packaging/windows/package_sotc.sh"
    Rename-Item -Force (Join-Path $Repo 'dist\SotC-PC.zip') "SotC-PC-$version.zip"

    # Inno Setup, portable in .deps: the per-user installer (no administrator rights).
    $iscc = Join-Path $Deps 'inno\ISCC.exe'
    if (-not (Test-Path $iscc)) {
        Step 'Downloading Inno Setup (portable, into .deps)'
        $setup = Join-Path $Deps 'innosetup.exe'
        Fetch 'https://jrsoftware.org/download.php/is.exe' $setup ''
        Start-Process -Wait -FilePath $setup -ArgumentList '/PORTABLE=1', '/VERYSILENT', '/SUPPRESSMSGBOXES',
            '/CURRENTUSER', '/NOICONS', "/DIR=$(Join-Path $Deps 'inno')"
    }
    & $iscc /Q "/DAppVersion=$version" "/DSourceDir=$(Join-Path $Repo 'out\stage\SotC-PC')" `
        "/O$(Join-Path $Repo 'dist')" (Join-Path $Repo 'packaging\windows\sotc_installer.iss')
    if ($LASTEXITCODE) { throw 'Inno Setup failed' }
    Step "Done: dist\SotC-PC-$version.zip, dist\SotC-PC-Setup-$version.exe"
}

# --- The launcher, from the source tree ----------------------------------------------------------
if (-not $NoLaunch) {
    Step 'Starting the launcher'
    $env:MSYS2_ROOT = $Msys  # sotc.py and the launcher take the DLLs from its clang64\bin
    $env:PATH = (Join-Path $Msys 'clang64\bin') + ';' + $env:PATH
    Start-Process -FilePath (Join-Path $Msys 'clang64\bin\pythonw.exe') `
        -ArgumentList (Join-Path $Repo 'launcher\sotc_launcher_win.py') -WorkingDirectory $Repo
}

#!/usr/bin/env pwsh
#
# Install the Bebop compiler, libraries, and schemas.
# https://bebop.sh
#
# Apache License 2.0

[CmdletBinding()]
param(
    [Parameter(Position = 0, HelpMessage = 'Version to install. Defaults to the latest release.')]
    [string]$Version,

    [Parameter(HelpMessage = 'Install system-wide instead of per-user.')]
    [switch]$System
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

function Write-Info($Message) {
    Write-Host '==> ' -ForegroundColor Blue -NoNewline
    Write-Host $Message
}

function Write-Warn($Message) {
    Write-Host 'Warning: ' -ForegroundColor Red -NoNewline
    Write-Host $Message
}

function Write-Step($Message) {
    Write-Host "  $Message " -NoNewline
}

function Write-Ok {
    Write-Host ([char]::ConvertFromUtf32(0x2705))
}

function Write-Fail($Message) {
    Write-Host ([char]::ConvertFromUtf32(0x274C))
    throw $Message
}

$Repo = '6over3/bebop-next'

# Platform detection — PowerShell 5.1 (Desktop) is always Windows
if ($PSVersionTable.PSEdition -eq 'Desktop' -or $IsWindows) {
    $os = 'windows'
} elseif ($IsMacOS) {
    $os = 'darwin'
} elseif ($IsLinux) {
    $os = 'linux'
} else {
    throw 'Unsupported operating system.'
}

# Architecture — RuntimeInformation on .NET 4.7.1+/Core, env fallback for older Windows
try {
    $arch = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
} catch {
    $arch = $env:PROCESSOR_ARCHITECTURE
}
$arch = switch ($arch) {
    'X64'   { 'x64' }
    'AMD64' { 'x64' }
    'Arm64' { 'arm64' }
    'ARM64' { 'arm64' }
    default { throw "Unsupported architecture: $arch" }
}

# Install prefix
if ($System) {
    $Prefix = if ($os -eq 'windows') { Join-Path $env:ProgramFiles 'bebop' } else { '/usr/local' }
} else {
    $Prefix = if ($os -eq 'windows') { Join-Path $env:LOCALAPPDATA 'Programs\bebop' } else { Join-Path $HOME '.local' }
}

$BinDir = Join-Path $Prefix 'bin'
$Exe = if ($os -eq 'windows') { 'bebopc.exe' } else { 'bebopc' }

# Environment validation
if ($PSVersionTable.PSVersion.Major -lt 5) {
    throw 'PowerShell 5 or later is required.'
}

# TLS 1.2 — required for GitHub, not enabled by default on older .NET
try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor 3072
} catch {
    Write-Warn 'Failed to enable TLS 1.2. Connections to GitHub may fail.'
}

# Admin check for system-wide install on Windows
if ($System -and $os -eq 'windows') {
    $principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'System-wide install requires an elevated PowerShell session.'
    }
}

# Root guard on Unix — allow CI containers
if ($os -ne 'windows' -and (& id -u) -eq '0' -and -not $env:CI) {
    if (-not (Test-Path /proc/1/cgroup) -or
        -not (Select-String -Quiet -Pattern 'azpl_job|actions_job|docker|garden|kubepods' -Path /proc/1/cgroup)) {
        throw 'Do not run this installer as root.'
    }
}

Write-Info 'Checking dependencies...'

# tar is required for extraction
if (-not (Get-Command tar -ErrorAction SilentlyContinue)) {
    throw 'tar is required. On Windows, tar is built in since Windows 10 1803.'
}

# Resolve version
if (-not $Version) {
    Write-Info 'Resolving latest version...'
    $release = Invoke-RestMethod "https://api.github.com/repos/$Repo/releases/latest"
    $Version = $release.tag_name
    if (-not $Version) { throw 'Failed to determine latest version from GitHub.' }
}

$ReleaseUrl = "https://github.com/$Repo/releases/download/$Version"

# Check existing installation
$existing = Get-Command bebopc -ErrorAction SilentlyContinue
if ($existing) {
    $installed = (& bebopc --version 2>$null).Trim()

    # Compare base version, then pre-release suffix
    $instBase, $instPre = $installed -split '-', 2
    $targetBase, $targetPre = $Version -split '-', 2
    $instVer = [Version]$instBase
    $targetVer = [Version]$targetBase

    if ($instVer -eq $targetVer -and $instPre -eq $targetPre) {
        Write-Info "bebopc $installed is already installed and up to date."
        exit 0
    }

    if ($instVer -gt $targetVer) {
        throw "bebopc $installed is newer than $Version. Uninstall first to downgrade."
    }

    Write-Warn "Upgrading bebopc from $installed to $Version."
} else {
    Write-Info "This will install to ${Prefix}:"
    Write-Host '  bin/          bebopc compiler and code generators'
    Write-Host '  lib/          static library'
    Write-Host '  include/      C and C++ headers'
    Write-Host '  share/bebop/  schemas and runtime support files'
}

# Download and install
$target = "$os-$arch"
$archive = "bebop@${Version}+${target}.tar.gz"

Write-Info "Installing Bebop $Version for $target..."

$tmpDir = Join-Path ([IO.Path]::GetTempPath()) "bebop-$([Guid]::NewGuid().ToString('N').Substring(0,8))"
New-Item -ItemType Directory -Path $tmpDir -Force | Out-Null

try {
    Write-Step 'Creating temp directory'
    Write-Ok

    Write-Step "Downloading $archive"
    $archivePath = Join-Path $tmpDir $archive
    try {
        Invoke-WebRequest -Uri "$ReleaseUrl/$archive" -OutFile $archivePath -UseBasicParsing
    } catch {
        Write-Fail "Download failed: $ReleaseUrl/$archive"
    }
    Write-Ok

    Write-Step "Extracting to $Prefix"
    New-Item -ItemType Directory -Path $Prefix -Force | Out-Null
    & tar xzf $archivePath -C $Prefix 2>$null
    if ($LASTEXITCODE -ne 0) {
        Write-Fail 'Extraction failed. The archive may be corrupt.'
    }
    Write-Ok

    $bebopcPath = Join-Path $BinDir $Exe
    if (-not (Test-Path $bebopcPath)) {
        throw 'bebopc binary not found after extraction.'
    }

    if ($os -ne 'windows') {
        Get-ChildItem (Join-Path $BinDir 'bebopc*') | ForEach-Object { & chmod +x $_.FullName }
    }

    Write-Host '  Installed:'
    Get-ChildItem (Join-Path $BinDir 'bebopc*') | ForEach-Object {
        Write-Host "    $($_.Name)"
    }

    # Persist PATH on Windows
    if ($os -eq 'windows') {
        $scope = if ($System) { 'Machine' } else { 'User' }
        $currentPath = [Environment]::GetEnvironmentVariable('PATH', $scope)
        if (($currentPath -split ';') -notcontains $BinDir) {
            [Environment]::SetEnvironmentVariable('PATH', "$currentPath;$BinDir", $scope)
            $env:PATH = "$env:PATH;$BinDir"
            Write-Host "  Added $BinDir to $scope PATH."
        }
    }

    Write-Host
    Write-Info 'Installation complete.'
    Write-Host

    Write-Info 'Next steps:'

    if ($os -ne 'windows' -and $env:PATH -notmatch [regex]::Escape($BinDir)) {
        $shellProfile = switch -Wildcard ($env:SHELL) {
            '*/zsh'  { '~/.zprofile' }
            '*/fish' { '~/.config/fish/config.fish' }
            '*/nu'   { '~/.config/nushell/env.nu' }
            '*/bash' { if (Test-Path "$HOME/.bash_profile") { '~/.bash_profile' } else { '~/.profile' } }
            default  { '~/.profile' }
        }
        Write-Host '  Add bebop to your PATH:'
        switch -Wildcard ($env:SHELL) {
            '*/fish' { Write-Host "    fish_add_path $BinDir" }
            '*/nu'   { Write-Host "    `$env.PATH = (`$env.PATH | prepend `"$BinDir`") | save --append $shellProfile" }
            default {
                Write-Host "    echo 'export PATH=`"$BinDir`:`$PATH`"' >> $shellProfile"
                Write-Host "    export PATH=`"$BinDir`:`$PATH`""
            }
        }
        Write-Host
    }

    Write-Host '  bebopc --help to get started'
    Write-Host '  https://bebop.sh for documentation'
} finally {
    Remove-Item -Path $tmpDir -Recurse -Force -ErrorAction SilentlyContinue
}

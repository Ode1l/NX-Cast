param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('clean', 'build', 'trace', 'release-build', 'upload', 'upload-log', 'package', 'publish')]
    [string]$Action
)
$ErrorActionPreference = 'Stop'

# Select MSYS2 explicitly; PATH's bash.exe may be WSL or Git Bash.
$bash = $env:NXCAST_MSYS_BASH
if (-not $bash) {
    $candidates = @()
    if ($env:DEVKITPRO -and [IO.Path]::IsPathRooted($env:DEVKITPRO) -and $env:DEVKITPRO -match '^[A-Za-z]:') {
        $candidates += Join-Path $env:DEVKITPRO 'msys2\usr\bin\bash.exe'
    }
    $candidates += 'C:\devkitPro\msys2\usr\bin\bash.exe', 'C:\msys64\usr\bin\bash.exe'
    $bash = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}
if (-not $bash -or -not (Test-Path -LiteralPath $bash -PathType Leaf)) {
    throw 'Set NXCAST_MSYS_BASH to your devkitPro MSYS2 usr\bin\bash.exe (not WSL/Git Bash).'
}
if (-not $env:DEVKITPRO -and (Test-Path -LiteralPath 'C:\devkitPro\switchvars.sh')) {
    $env:DEVKITPRO = 'C:/devkitPro'
}
$env:MSYS2_PATH_TYPE = 'inherit'
$script = (Join-Path $PSScriptRoot 'dev.sh').Replace('\', '/')
& $bash --login $script $Action
exit $LASTEXITCODE

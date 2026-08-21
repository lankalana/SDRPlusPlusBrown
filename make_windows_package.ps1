param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,

    # Kept for compatibility with existing callers. Resources are installed by CMake.
    [string]$RootDirectory
)

$ErrorActionPreference = "Stop"
$configuration = "RelWithDebInfo"
$stageDirectory = Join-Path $PWD "sdrpp_windows_x64"
$archivePath = Join-Path $PWD "sdrpp_windows_x64.zip"

if (Test-Path -LiteralPath $stageDirectory) {
    Remove-Item -LiteralPath $stageDirectory -Recurse -Force
}
if (Test-Path -LiteralPath $archivePath) {
    Remove-Item -LiteralPath $archivePath -Force
}

cmake --install $BuildDirectory --config $configuration --prefix $stageDirectory
if ($LASTEXITCODE -ne 0) {
    throw "CMake install failed with exit code $LASTEXITCODE"
}

Compress-Archive -Path "$stageDirectory\*" -DestinationPath $archivePath
Remove-Item -LiteralPath $stageDirectory -Recurse -Force

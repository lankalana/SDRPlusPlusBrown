$rt = (Resolve-Path ".\out\build\windows-vs2022\Debug").Path

$env:E2E_BINARY    = Join-Path $rt "sdrpp.exe"
$env:E2E_BUILD_DIR = $rt
$env:E2E_ROOT_DEV  = $rt

python .\e2e\e2e_common.py
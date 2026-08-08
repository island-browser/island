$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
& python3 (Join-Path $root "scripts/deps.py") --root $root install @args
exit $LASTEXITCODE

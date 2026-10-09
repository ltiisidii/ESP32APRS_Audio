# Runs the host tests inside Docker (gcc + AddressSanitizer). Needs Docker Desktop running.
#   powershell -File test\host\run.ps1            all tests
#   powershell -File test\host\run.ps1 modem300   only tests whose name contains "modem300"
param([string]$Filter = "")
$root = (Resolve-Path "$PSScriptRoot\..\..").Path
docker run --rm -v "${root}:/src" -w /src/test/host gcc:13 make T="$Filter"
exit $LASTEXITCODE

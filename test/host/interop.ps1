# Interoperability test against Dire Wolf (see interop.sh). Needs Docker Desktop running.
#   powershell -File test\host\interop.ps1
$root = (Resolve-Path "$PSScriptRoot\..\..").Path
docker run --rm -v "${root}:/src" -w /src/test/host gcc:13 sh interop.sh
exit $LASTEXITCODE

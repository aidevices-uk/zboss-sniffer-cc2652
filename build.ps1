$ErrorActionPreference = "Stop"
$gmake = "C:\ti\ccs1280\ccs\utils\bin\gmake.exe"
if (-not (Test-Path $gmake)) {
    throw "CCS gmake was not found at $gmake"
}
& $gmake -C "$PSScriptRoot\firmware\ticlang" all
exit $LASTEXITCODE

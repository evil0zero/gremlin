param(
    [ValidateSet('Debug', 'Release')][string]$Profile = 'Release',
    [string]$Name = 'sweet_nothings'
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$profileName = $Profile.ToLowerInvariant()
$gremlinBuild = Join-Path $root "out/build/windows-$profileName"
$exe = Join-Path $gremlinBuild 'bin/gremlin.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "GREMLIN has not been built at $exe. See the Windows build instructions in README.md."
}
$cache = Get-Content -LiteralPath (Join-Path $gremlinBuild 'CMakeCache.txt')
function Get-CachedPath([string]$Key) {
    foreach ($line in $cache) {
        if ($line -match "^$($Key):[^=]+=(.+)$") { return $Matches[1] }
    }
    throw "$Key was not found in the GREMLIN build cache."
}

$runId = "$profileName-$([guid]::NewGuid().ToString('N').Substring(0, 12))"
$run = Join-Path $root "out/smoke/$runId"
$source = Join-Path $run 'source'
$build = Join-Path $run 'build'
$tempPath = Join-Path $run 'tmp'
New-Item -ItemType Directory -Path $tempPath | Out-Null
$runLog = Join-Path $run 'smoke.log'
$initArgs = @('init', '--source', $source, '--name', $Name)
$buildArgs = @(
    'build', '--trust-source', '--source', $source, '--build-dir', $build,
    '--cmake', (Get-CachedPath 'CMAKE_COMMAND'), '--ctest', (Get-CachedPath 'CMAKE_CTEST_COMMAND'),
    '--ninja', (Get-CachedPath 'CMAKE_MAKE_PROGRAM'), '--compiler', (Get-CachedPath 'CMAKE_CXX_COMPILER'),
    '--profile', $Profile)
ConvertTo-Json -Compress -InputObject (@($exe) + $initArgs) | Set-Content -LiteralPath $runLog

$savedTemp = $env:TEMP
$savedTmp = $env:TMP
try {
    $env:TEMP = $tempPath
    $env:TMP = $tempPath
    & $exe @initArgs 2>&1 | Tee-Object -FilePath $runLog -Append
    $code = $LASTEXITCODE
    "init_exit=$code" | Add-Content -LiteralPath $runLog
    if ($code -ne 0) { exit $code }

    ConvertTo-Json -Compress -InputObject (@($exe) + $buildArgs) | Add-Content -LiteralPath $runLog
    & $exe @buildArgs 2>&1 | Tee-Object -FilePath $runLog -Append
    $code = $LASTEXITCODE
    "gremlin_exit=$code" | Add-Content -LiteralPath $runLog
    if ($code -ne 0) { exit $code }

    $app = Join-Path $build "bin/$Name.exe"
    ConvertTo-Json -Compress -InputObject @($app) | Add-Content -LiteralPath $runLog
    & $app 2>&1 | Tee-Object -FilePath $runLog -Append
    $code = $LASTEXITCODE
    "app_exit=$code" | Add-Content -LiteralPath $runLog
    "Smoke log $runLog"
    exit $code
} finally {
    $env:TEMP = $savedTemp
    $env:TMP = $savedTmp
}

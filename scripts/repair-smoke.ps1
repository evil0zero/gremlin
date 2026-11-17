param(
    [ValidateSet('Debug', 'Release')][string]$Profile = 'Release',
    [string]$Model = 'qwen2.5-coder:7b'
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$profileName = $Profile.ToLowerInvariant()
$gremlinBuild = Join-Path $root "out/build/windows-$profileName"
$exe = Join-Path $gremlinBuild 'bin/gremlin.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "GREMLIN has not been built at $exe. See README.md."
}

$cache = Get-Content -LiteralPath (Join-Path $gremlinBuild 'CMakeCache.txt')
function Get-CachedPath([string]$Key) {
    foreach ($line in $cache) {
        if ($line -match "^$($Key):[^=]+=(.+)$") { return $Matches[1] }
    }
    throw "$Key was not found in the GREMLIN build cache."
}

$source = Join-Path $root 'examples/line-counter'
$evidence = New-Item -ItemType Directory -Path (Join-Path $root ("out/logs/repair-smoke/" + [guid]::NewGuid().ToString('N')))
$temporary = New-Item -ItemType Directory -Path (Join-Path $evidence 'tmp')
$runLog = Join-Path $evidence 'invocation.log'
$repairArgs = @(
    'repair-cmake', '--trust-source', '--source', $source, '--initial-source', 'main.cpp',
    '--model', $Model, '--max-repairs', '1', '--profile', $Profile,
    '--cmake', (Get-CachedPath 'CMAKE_COMMAND'), '--ctest', (Get-CachedPath 'CMAKE_CTEST_COMMAND'),
    '--ninja', (Get-CachedPath 'CMAKE_MAKE_PROGRAM'), '--compiler', (Get-CachedPath 'CMAKE_CXX_COMPILER'))
ConvertTo-Json -Compress -InputObject (@($exe) + $repairArgs) | Set-Content -LiteralPath $runLog

$savedTemp = $env:TEMP
$savedTmp = $env:TMP
Push-Location $root
try {
    $env:TEMP = $temporary.FullName
    $env:TMP = $temporary.FullName
    & $exe @repairArgs 2>&1 | Tee-Object -FilePath $runLog -Append
    $code = $LASTEXITCODE
    "gremlin_exit=$code" | Add-Content -LiteralPath $runLog
    "Invocation log $runLog"
    exit $code
} finally {
    Pop-Location
    $env:TEMP = $savedTemp
    $env:TMP = $savedTmp
}
